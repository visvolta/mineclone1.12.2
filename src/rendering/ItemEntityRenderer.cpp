#include "rendering/ItemEntityRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

#include <glm/ext/matrix_transform.hpp>
#include <glm/matrix.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

#include "items/ItemRegistry.hpp"
#include "world/ItemEntitySystem.hpp"

namespace {
GLuint shader(GLenum type,const char* source){GLuint s=glCreateShader(type);glShaderSource(s,1,&source,nullptr);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok){glDeleteShader(s);throw std::runtime_error("Item entity shader compile failed");}return s;}
}
ItemEntityRenderer::ItemEntityRenderer(const std::filesystem::path& root,const ItemRegistry& items):assetRoot_(root),items_(items){
    const char* vs="#version 330 core\nlayout(location=0)in vec3 p;layout(location=1)in vec2 uv;uniform mat4 mvp;out vec2 t;void main(){gl_Position=mvp*vec4(p,1);t=uv;}";
    const char* fs="#version 330 core\nin vec2 t;out vec4 c;uniform sampler2D tex;void main(){c=texture(tex,t);if(c.a<0.1)discard;}";
    GLuint a=shader(GL_VERTEX_SHADER,vs),b=shader(GL_FRAGMENT_SHADER,fs);program_=glCreateProgram();glAttachShader(program_,a);glAttachShader(program_,b);glLinkProgram(program_);glDeleteShader(a);glDeleteShader(b);
    const float verts[]={-0.25F,-0.25F,0,0,1, 0.25F,-0.25F,0,1,1, 0.25F,0.25F,0,1,0, -0.25F,-0.25F,0,0,1, 0.25F,0.25F,0,1,0, -0.25F,0.25F,0,0,0};
    glGenVertexArrays(1,&vao_);glGenBuffers(1,&vbo_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,vbo_);glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)0);glEnableVertexAttribArray(0);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,5*sizeof(float),(void*)(3*sizeof(float)));glEnableVertexAttribArray(1);glBindVertexArray(0);
}
ItemEntityRenderer::~ItemEntityRenderer(){for(auto&[id,t]:textures_){(void)id;if(t)glDeleteTextures(1,&t);}if(vbo_)glDeleteBuffers(1,&vbo_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);}
GLuint ItemEntityRenderer::textureFor(std::uint16_t id){if(auto it=textures_.find(id);it!=textures_.end())return it->second;const auto& def=items_.get(id);std::string res=def.iconResource;if(res.rfind("minecraft:",0)==0)res.erase(0,10);auto path=assetRoot_/"assets/minecraft/textures"/(res+".png");int w=0,h=0,n=0;stbi_uc* px=stbi_load(path.string().c_str(),&w,&h,&n,4);if(!px){textures_[id]=0;return 0;}GLuint t=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,px);stbi_image_free(px);textures_[id]=t;return t;}
void ItemEntityRenderer::render(const ItemEntitySystem& sys,const glm::mat4& view,const glm::mat4& proj,float partial){
    glUseProgram(program_);glBindVertexArray(vao_);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDisable(GL_CULL_FACE);
    const glm::mat4 inv=glm::inverse(view);const glm::vec3 right(inv[0]),up(inv[1]);
    for(const auto&e:sys.entities()){GLuint tex=textureFor(e.stack.itemId);if(!tex)continue;const glm::dvec3 ip=e.previousPosition+(e.position-e.previousPosition)*static_cast<double>(std::clamp(partial,0.0F,1.0F));glm::mat4 m(1.0F);const float bob=std::sin((static_cast<float>(e.age)+partial)/10.0F)*0.1F+0.1F;m=glm::translate(m,glm::vec3(ip)+glm::vec3(0,bob,0));m[0]=glm::vec4(right*0.8F,0);m[1]=glm::vec4(up*0.8F,0);m=glm::rotate(m,glm::radians(e.rotation),glm::vec3(0,0,1));const glm::mat4 mvp=proj*view*m;glUniformMatrix4fv(glGetUniformLocation(program_,"mvp"),1,GL_FALSE,glm::value_ptr(mvp));glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);glUniform1i(glGetUniformLocation(program_,"tex"),0);glDrawArrays(GL_TRIANGLES,0,6);}
    glEnable(GL_CULL_FACE);glDisable(GL_BLEND);glBindVertexArray(0);glUseProgram(0);
}
