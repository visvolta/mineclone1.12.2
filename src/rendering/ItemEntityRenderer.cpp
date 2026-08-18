#include "rendering/ItemEntityRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "items/ItemRegistry.hpp"
#include "rendering/BlockRenderPath.hpp"
#include "rendering/BlockRenderResources.hpp"
#include "rendering/BlockStateModelMap.hpp"
#include "rendering/TextureAtlas.hpp"
#include "world/ItemEntitySystem.hpp"

namespace {
struct Vertex { float x,y,z,u,v,shade; };

GLuint compileShader(GLenum type,const char* source){
    GLuint shader=glCreateShader(type);
    glShaderSource(shader,1,&source,nullptr);
    glCompileShader(shader);
    GLint ok=0;
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){ glDeleteShader(shader); throw std::runtime_error("Item entity shader compile failed"); }
    return shader;
}

float faceShade(Face face) {
    constexpr std::array<float,6> shades={0.50F,1.0F,0.80F,0.80F,0.60F,0.60F};
    return shades[static_cast<std::size_t>(face)];
}

void appendBaked(std::vector<Vertex>& out,const BakedBlockModel& model) {
    constexpr std::array<std::size_t,6> indices={0,1,2,0,2,3};
    for(const BakedModelQuad& quad:model.quads){
        const float shade=quad.shade?faceShade(quad.face):1.0F;
        for(const std::size_t i:indices){
            const glm::vec3 p=(quad.positions[i]-glm::vec3(0.5F))*0.5F;
            out.push_back({p.x,p.y,p.z,quad.uvs[i].x,quad.uvs[i].y,shade});
        }
    }
}

void appendSpriteQuad(std::vector<Vertex>& out,
                      const std::array<glm::vec3,4>& p,const AtlasBounds& uv,float shade,bool reverse=false){
    const std::array<glm::vec2,4> tex=reverse
        ? std::array<glm::vec2,4>{{{uv.u1,uv.v1},{uv.u0,uv.v1},{uv.u0,uv.v0},{uv.u1,uv.v0}}}
        : std::array<glm::vec2,4>{{{uv.u0,uv.v1},{uv.u1,uv.v1},{uv.u1,uv.v0},{uv.u0,uv.v0}}};
    constexpr std::array<std::size_t,6> indices={0,1,2,0,2,3};
    for(const std::size_t i:indices) out.push_back({p[i].x,p[i].y,p[i].z,tex[i].x,tex[i].y,shade});
}

std::vector<Vertex> extrudedSprite(const AtlasBounds& uv){
    // RenderItem's generated model is one pixel thick. The displayed item is
    // 0.5 world units across, so one source pixel is 0.5/16 = 1/32 units.
    constexpr float half=0.25F;
    constexpr float halfDepth=1.0F/64.0F;
    std::vector<Vertex> out;
    out.reserve(36);
    appendSpriteQuad(out,{{{-half,-half,halfDepth},{half,-half,halfDepth},{half,half,halfDepth},{-half,half,halfDepth}}},uv,1.0F);
    appendSpriteQuad(out,{{{half,-half,-halfDepth},{-half,-half,-halfDepth},{-half,half,-halfDepth},{half,half,-halfDepth}}},uv,0.82F,true);
    appendSpriteQuad(out,{{{-half,-half,-halfDepth},{-half,-half,halfDepth},{-half,half,halfDepth},{-half,half,-halfDepth}}},uv,0.72F);
    appendSpriteQuad(out,{{{half,-half,halfDepth},{half,-half,-halfDepth},{half,half,-halfDepth},{half,half,halfDepth}}},uv,0.86F);
    appendSpriteQuad(out,{{{-half,half,halfDepth},{half,half,halfDepth},{half,half,-halfDepth},{-half,half,-halfDepth}}},uv,1.0F);
    appendSpriteQuad(out,{{{-half,-half,-halfDepth},{half,-half,-halfDepth},{half,-half,halfDepth},{-half,-half,halfDepth}}},uv,0.62F);
    return out;
}

std::vector<Vertex> simpleBlockCube(const AtlasBounds& uv){
    constexpr float half=0.25F;
    std::vector<Vertex> out;
    out.reserve(36);
    appendSpriteQuad(out,{{{-half,-half,half},{half,-half,half},{half,half,half},{-half,half,half}}},uv,0.80F);
    appendSpriteQuad(out,{{{half,-half,-half},{-half,-half,-half},{-half,half,-half},{half,half,-half}}},uv,0.80F,true);
    appendSpriteQuad(out,{{{-half,-half,-half},{-half,-half,half},{-half,half,half},{-half,half,-half}}},uv,0.60F);
    appendSpriteQuad(out,{{{half,-half,half},{half,-half,-half},{half,half,-half},{half,half,half}}},uv,0.60F);
    appendSpriteQuad(out,{{{-half,half,half},{half,half,half},{half,half,-half},{-half,half,-half}}},uv,1.00F);
    appendSpriteQuad(out,{{{-half,-half,-half},{half,-half,-half},{half,-half,half},{-half,-half,half}}},uv,0.50F);
    return out;
}

int modelCount(int count){ return count>48?5:count>32?4:count>16?3:count>1?2:1; }
}

ItemEntityRenderer::ItemEntityRenderer(const std::filesystem::path& root,const ItemRegistry& items,
                                       const BlockRenderResources& resources,const TextureAtlas& atlas)
    :assetRoot_(root),items_(items),resources_(resources),atlas_(atlas){
    const char* vs="#version 330 core\nlayout(location=0)in vec3 p;layout(location=1)in vec2 uv;layout(location=2)in float shade;uniform mat4 mvp;out vec2 t;out float s;void main(){gl_Position=mvp*vec4(p,1);t=uv;s=shade;}";
    const char* fs="#version 330 core\nin vec2 t;in float s;out vec4 c;uniform sampler2D tex;void main(){vec4 q=texture(tex,t);if(q.a<0.1)discard;c=vec4(q.rgb*s,q.a);}";
    const GLuint a=compileShader(GL_VERTEX_SHADER,vs),b=compileShader(GL_FRAGMENT_SHADER,fs);
    program_=glCreateProgram();
    glAttachShader(program_,a); glAttachShader(program_,b); glLinkProgram(program_);
    glDeleteShader(a); glDeleteShader(b);
}

ItemEntityRenderer::~ItemEntityRenderer(){
    for(auto&[key,g]:geometries_){ (void)key; if(g.vbo)glDeleteBuffers(1,&g.vbo); if(g.vao)glDeleteVertexArrays(1,&g.vao); }
    if(program_)glDeleteProgram(program_);
}

std::uint32_t ItemEntityRenderer::geometryKey(const ItemStack& stack) const {
    return (static_cast<std::uint32_t>(stack.itemId)<<16U)|static_cast<std::uint32_t>(stack.damage);
}

const ItemEntityRenderer::Geometry& ItemEntityRenderer::geometryFor(const ItemStack& stack){
    const std::uint32_t key=geometryKey(stack);
    if(const auto found=geometries_.find(key);found!=geometries_.end()) return found->second;

    std::vector<Vertex> vertices;
    bool gui3d=false;
    const ItemDefinition& item=items_.get(stack.itemId);
    if(item.placedBlock && blockRenderPath(*item.placedBlock)==BlockRenderPath::JsonModel){
        std::vector<const BakedBlockModel*> models;
        if((stack.damage&15U)==0U){
            const BlockModelManager& modelManager=resources_.models();
            const BakedBlockModel* itemModel=modelManager.itemModel(item.name);
            if(itemModel!=nullptr&&!itemModel->quads.empty()) models.push_back(itemModel);
        }
        if(models.empty()){
            const BlockState state=makeBlockState(static_cast<std::uint16_t>(*item.placedBlock),static_cast<std::uint8_t>(stack.damage&15U));
            const auto air=[](int,int,int){return makeBlockState(0);};
            models=resources_.models().select(resolveBlockModelState(state,air),0);
        }
        for(const BakedBlockModel* model:models) if(model) appendBaked(vertices,*model);
        gui3d=!vertices.empty();
    }

    if(vertices.empty()){
        std::string icon=item.iconResource;
        if(stack.itemId==349){
            constexpr std::array<const char*,4> fish={"fish_cod_raw","fish_salmon_raw","fish_clownfish_raw","fish_pufferfish_raw"};
            icon=std::string("minecraft:items/")+fish[std::min<std::size_t>(stack.damage,3)];
        }else if(stack.itemId==350){
            icon=std::string("minecraft:items/")+(stack.damage==1?"fish_salmon_cooked":"fish_cod_cooked");
        }else if(stack.itemId==351){
            constexpr std::array<const char*,16> dyes={"black","red","green","brown","blue","purple","cyan","silver","gray","pink","lime","yellow","light_blue","magenta","orange","white"};
            icon=std::string("minecraft:items/dye_powder_")+dyes[std::min<std::size_t>(stack.damage,15)];
        }
        if(!icon.empty()&&atlas_.data().contains(icon)) vertices=extrudedSprite(atlas_.data().sprite(icon).bounds);
    }

    // Builtin/entity item models (chests, beds and shulkers) do not expose
    // baked quads. Keep them three-dimensional rather than falling back to a
    // camera-facing sprite; their exact GUI TESR path is handled by GameHud.
    if(vertices.empty()&&item.placedBlock){
        const AtlasBounds uv=atlas_.data().sprite(BlockRegistry::texture(
            makeBlockState(static_cast<std::uint16_t>(*item.placedBlock),static_cast<std::uint8_t>(stack.damage&15U)),Face::Up)).bounds;
        vertices=simpleBlockCube(uv);
        gui3d=true;
    }

    Geometry geometry;
    geometry.gui3d=gui3d;
    geometry.vertexCount=static_cast<GLsizei>(vertices.size());
    glGenVertexArrays(1,&geometry.vao); glGenBuffers(1,&geometry.vbo);
    glBindVertexArray(geometry.vao); glBindBuffer(GL_ARRAY_BUFFER,geometry.vbo);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(vertices.size()*sizeof(Vertex)),vertices.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(3*sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(5*sizeof(float))); glEnableVertexAttribArray(2);
    return geometries_.emplace(key,geometry).first->second;
}

void ItemEntityRenderer::render(const ItemEntitySystem& system,const glm::mat4& view,const glm::mat4& projection,float partial){
    glUseProgram(program_); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glUniform1i(glGetUniformLocation(program_,"tex"),0);
    for(const ItemEntity& entity:system.entities()){
        const Geometry& geometry=geometryFor(entity.stack);
        if(geometry.vertexCount<=0) continue;
        const glm::dvec3 interpolated=entity.previousPosition+(entity.position-entity.previousPosition)*static_cast<double>(std::clamp(partial,0.0F,1.0F));
        const float bob=std::sin((static_cast<float>(entity.age)+partial)/10.0F+entity.hoverStart)*0.1F+0.1F;
        const float yaw=((static_cast<float>(entity.age)+partial)/20.0F+entity.hoverStart)*57.2957795F;
        const int copies=modelCount(entity.stack.count);
        std::mt19937 random(static_cast<unsigned>(entity.stack.itemId+entity.stack.damage));
        std::uniform_real_distribution<float> offset(-0.15F,0.15F);

        glActiveTexture(GL_TEXTURE0);
        atlas_.bind(0);
        for(int k=0;k<copies;++k){
            glm::mat4 model(1.0F);
            model=glm::translate(model,glm::vec3(interpolated)+glm::vec3(0,bob+0.25F,0));
            model=glm::rotate(model,glm::radians(yaw),glm::vec3(0,1,0));
            if(k>0){
                if(geometry.gui3d) model=glm::translate(model,glm::vec3(offset(random),offset(random),offset(random)));
                else model=glm::translate(model,glm::vec3(offset(random)*0.5F,offset(random)*0.5F,0.09375F*static_cast<float>(k)));
            }
            model=glm::scale(model,glm::vec3(0.8F));
            const glm::mat4 mvp=projection*view*model;
            glUniformMatrix4fv(glGetUniformLocation(program_,"mvp"),1,GL_FALSE,glm::value_ptr(mvp));
            glBindVertexArray(geometry.vao); glDrawArrays(GL_TRIANGLES,0,geometry.vertexCount);
        }
    }
    glEnable(GL_CULL_FACE); glDisable(GL_BLEND); glBindVertexArray(0); glUseProgram(0);
}
