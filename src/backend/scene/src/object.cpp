#include "object.h"
#include "common.h"
#include "pkg.h"
#include "wallpaper.h"
#include <iostream>
#include <string>
/*
    Visible: controls whether a renderable is drawn to the screen. Useful to prerender 2D renderables offscreen and use them as inputs to other renderables.
    Name: defines a name to identify the renderable more easily in the scene graph.
    Origin: is the position in XYZ coordinates.
    Angles: describes the rotation in degrees and euler angles.
    Scale: is the size along the XYZ axis.
    Blend mode: works like photoshop blend modes for 2D renderables.
    Parallax depth: for 2D renderables if the camera has parallax enabled in the scene options. This controls how much a renderable is affected by the effect, making it possible to create an illusion of depth.
    Materials: a list of materials that are used by the renderable.
*/

using json = nlohmann::json;
using namespace wallpaper;

std::unique_ptr<RenderObject> wallpaper::CreateObject(const json& obj_json) {
	std::string name;
	if(obj_json.contains("name") && obj_json.at("name").is_string())
		name = obj_json.at("name");

    if(obj_json.contains("image") && !obj_json.at("image").is_null()) {
        auto obj = std::make_unique<ImageObject>();
        if(obj->From_json(obj_json)) return obj;
		else LOG_ERROR("Read image object " + name + " from json failed");
    }
    else if(obj_json.contains("particle") && !obj_json.at("particle").is_null()) {
        auto obj = std::make_unique<ParticleObject>();
        if(obj->From_json(obj_json)) return obj;
    }
    return nullptr;
}

bool wallpaper::RenderObject::From_json(const json& obj)
{
	if(!obj.contains("name")) return false;
    m_name = obj.at("name");
	if(obj.contains("origin") && obj.at("origin").is_string())
		if(!StringToVec<float>(obj.at("origin"), m_origin)) return false;

	if(obj.contains("scale") && obj.at("scale").is_string())
		if(!StringToVec<float>(obj.at("scale"), m_scale)) return false;

	if(obj.contains("angles") && obj.at("angles").is_string())
		if(!StringToVec<float>(obj.at("angles"), m_angles)) return false;

    if(obj.contains("visible") && obj.at("visible").is_boolean()) m_visible = obj.at("visible");
    return true;
}

ImageObject::~ImageObject() {
}

bool ImageObject::From_json(const json& obj)
{
    if(!RenderObject::From_json(obj) || !obj.contains("image")) return false;
	if(Name() == "Compose")
		m_compose = true;
	if(obj.contains("size")) {
		std::vector<float> fsize;
		if(!StringToVec<float>(obj.at("size"), fsize)) return false;
		size_ = std::vector<int>(fsize.begin(), fsize.end());
	}
	if(obj.contains("copybackground"))
		copybackground_ = obj.at("copybackground");

	if(obj.contains("brightness") && obj.at("brightness").is_number())
		m_brightness = obj.at("brightness");

	if(obj.contains("colorBlendMode"))
		m_blendmode = obj.at("colorBlendMode");


	if(obj.contains("alpha") && obj.at("alpha").is_number())
		m_alpha = obj.at("alpha");

	if(obj.contains("color") && obj.at("color").is_string())
		if(!StringToVec<float>(obj.at("color"), m_color)) return false;

    std::string image_str = fs::GetContent(WallpaperGL::GetPkgfs(), obj.at("image"));
    /*
    {
        "autosize" : true,
        "material" : "*.json"
    }
    */
    auto image = json::parse(image_str);
    if(!image.contains("material")) return false;
	if(!obj.contains("size")) {
		if(image.contains("width")) {
			size_[0] = image.at("width");
			size_[1] = image.at("height");
		}
		else if(image.contains("fullscreen")) {
			m_fullscreen = true;
		}
		else return false;
	}
		
	if(image.contains("autosize"))
		autosize_ = image.at("autosize").get<bool>();
	// gen combos before material and effect
	GenBaseCombos();

    std::string material_str = fs::GetContent(WallpaperGL::GetPkgfs(), image.at("material"));
	auto mat_j = json::parse(material_str);
    if(!m_material.From_json(mat_j)) return false;
	

	m_material.SetSize(size_);
	if(obj.contains("effects")) {
		for(auto& e:obj.at("effects")) {
			effects_.push_back(Effect(*this, size_));
			if(!effects_.back().From_json(e)) {
				LOG_INFO("object: " + Name() + "'s effect: " + effects_.back().Name() + " not load");
				effects_.pop_back();
			}
		}
	}
    return true;
}

void ImageObject::Load(WPRender& wpRender)
{
	// fullscreen
	if(m_fullscreen) {
		size_ = wpRender.shaderMgr.globalUniforms.Ortho();
		Origin() = std::vector<float>({size_[0]/2.0f, size_[1]/2.0f, 0.0f});
	}
	auto ori = Origin();
	ori[1] = wpRender.shaderMgr.globalUniforms.Ortho()[1] - ori[1];
	auto& verArry = Vertices();
	verArry = gl::VerticeArray::GenSizedBox(&wpRender.glWrapper, std::vector<float>(size_.begin(),size_.end()));
    verArry.Update();

    m_material.Load(wpRender);

	// fbo shadervalues	
	auto viewpro_mat = wpRender.shaderMgr.globalUniforms.GetViewProjectionMatrix();
	auto scale = Scale();
	//2. move to origin
	auto model_mat = glm::translate(glm::mat4(1.0f), glm::vec3(ori[0],ori[1],ori[2]));
	//2. rotation
	model_mat = glm::rotate(model_mat, glm::radians(Angles()[2]), glm::vec3(0,0,1)); // z
	model_mat = glm::rotate(model_mat, glm::radians(Angles()[0]), glm::vec3(1,0,0)); // x
	model_mat = glm::rotate(model_mat, glm::radians(Angles()[1]), glm::vec3(0,1,0)); // y
	//1. scale
	model_mat = glm::scale(model_mat, glm::vec3(scale[0], scale[1], scale[2]));
	auto modelviewpro_mat = viewpro_mat * model_mat;
	gl::Shadervalue::SetShadervalues(shadervalues_, "fboTrans", modelviewpro_mat);

	// material shadervalues
	model_mat = glm::mat4(1.0f);
	if(m_material.GetShadervalues().count("g_Texture0Resolution") != 0) {
		// remove black edge
		auto& sv = m_material.GetShadervalues().at("g_Texture0Resolution");
		if(sv.value[0] != sv.value[2] || sv.value[1] != sv.value[3]) {
			model_mat = glm::translate(glm::mat4(1.0f), -glm::vec3(size_[0]/2.0f,size_[1]/2.0f,0.0f));
			model_mat = glm::scale(model_mat, glm::vec3(sv.value[0]/sv.value[2], sv.value[1]/sv.value[3], 1.0f));
			model_mat = glm::translate(model_mat, glm::vec3(size_[0]/2.0f,size_[1]/2.0f,0.0f));
		}
	}
	if(IsCompose()) {
		// compose need to know wordcoord and use global ViewProjectionMatrix
		// this is ok for fullscreen as ori at (0,0,0)
		model_mat = glm::translate(glm::mat4(1.0f), glm::vec3(ori[0], ori[1], ori[2])) * model_mat;
	} else {
		model_mat = glm::translate(glm::mat4(1.0f), glm::vec3(size_[0]/2.0f, size_[1]/2.0f, 0.0f)) * model_mat;
		viewpro_mat = glm::ortho(0.0f, (float)size_[0], 0.0f, (float)size_[1], -100.0f, 100.0f);
	}
	modelviewpro_mat = viewpro_mat * model_mat;
	gl::Shadervalue::SetShadervalues(m_material.GetShadervalues(), "g_ModelViewProjectionMatrix", modelviewpro_mat);
	gl::Shadervalue::SetShadervalues(m_material.GetShadervalues(), "g_UserAlpha", std::vector<float>({m_alpha}));
	gl::Shadervalue::SetShadervalues(m_material.GetShadervalues(), "g_Brightness", std::vector<float>({m_brightness}));
	gl::Shadervalue::SetShadervalues(m_material.GetShadervalues(), "g_Alpha", std::vector<float>({m_alpha}));
	gl::Shadervalue::SetShadervalues(m_material.GetShadervalues(), "g_Color", m_color);


	int index = 0;
	for(auto& e:effects_){
		if(index++ == WallpaperGL::EffNum()) break;
		LOG_INFO("\n---Loading effect---");
		e.Load(wpRender);
	}
	m_fbo1 = std::unique_ptr<gl::GLFramebuffer>(wpRender.glWrapper.CreateFramebuffer(size_[0], size_[1]));
	m_fbo2 = std::unique_ptr<gl::GLFramebuffer>(wpRender.glWrapper.CreateFramebuffer(size_[0], size_[1]));
}

void ImageObject::Render(WPRender& wpRender)
{
	m_curFbo = m_fbo1.get();

	wpRender.glWrapper.BindFramebufferViewport(m_curFbo);

	if(copybackground_ || m_fullscreen)	{
		wpRender.Clear(0.0f);
		if(IsCompose() || m_fullscreen) {
			wpRender.glWrapper.ActiveTexture(0);
			wpRender.glWrapper.BindFramebufferTex(wpRender.GlobalFbo());
		}
	}
	else {
		wpRender.Clear();
	}		

	glBlendFunc(GL_ONE, GL_ZERO);
	SetCurVertices(&Vertices());
    m_material.Render(wpRender);


	glBlendFunc(GL_ONE, GL_ZERO);
	int index = 0;
	for(auto& e:effects_) {
		if(index++ == WallpaperGL::EffNum()) break;
		e.Render(wpRender);
	}

	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
	wpRender.UseGlobalFbo(shadervalues_);
	wpRender.glWrapper.ActiveTexture(0);
	wpRender.glWrapper.BindTexture(&CurFbo()->color_texture);
	Vertices().Draw();
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}


bool ImageObject::IsCompose() const {
	return m_compose;
}

void ImageObject::GenBaseCombos() {
	m_basecombos.clear();
//{"material":"ui_editor_properties_composite","combo":"COMPOSITE","type":"options","default":0,"options":{"ui_editor_properties_normal":0,"ui_editor_properties_blend":1,"ui_editor_properties_under":2,"ui_editor_properties_cutout":3}}
	m_basecombos["COMPOSITE"] = IsCompose()?0:1;
	if(m_fullscreen)
		m_basecombos["TRANSFORM"] = 1;
}


gl::GLFramebuffer* ImageObject::CurFbo() {
	if(m_curFbo == nullptr)
		m_curFbo = m_fbo1.get();
	return m_curFbo;
}

gl::GLFramebuffer* ImageObject::TargetFbo() {
	if(m_fbo1.get() == m_curFbo)
		return m_fbo2.get();
	else return m_fbo1.get();
}

void ImageObject::SwitchFbo() {
	if(m_fbo1.get() == m_curFbo)	
		m_curFbo = m_fbo2.get();
	else m_curFbo = m_fbo1.get();
}


ParticleObject::ParticleObject() {
}

bool ParticleObject::From_json(const json& obj) {
    if(!RenderObject::From_json(obj)) return false;
    return true;
}

void ParticleObject::Load(WPRender& wpRender) {
    LOG_INFO("Particle Object(not supported): " + Name());
}
