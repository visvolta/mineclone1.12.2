#include "rendering/BlockEntityRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <stb_image.h>

#include "blocks/BlockRegistry.hpp"
#include "world/World.hpp"

namespace {

constexpr std::string_view vertexSource = R"glsl(
#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 textureUv;
layout(location=2) in float shade;
out vec2 uv;
out float vertexShade;
uniform mat4 transform;
void main(){ uv=textureUv; vertexShade=shade; gl_Position=transform*vec4(position,1.0); }
)glsl";

constexpr std::string_view fragmentSource = R"glsl(
#version 330 core
in vec2 uv;
in float vertexShade;
out vec4 fragmentColor;
uniform sampler2D entityTexture;
void main(){ vec4 c=texture(entityTexture,uv); if(c.a<0.05) discard; fragmentColor=vec4(c.rgb*vertexShade,c.a); }
)glsl";

struct V { float x,y,z,u,v,shade; };

float lightAt(const World& world, const glm::ivec3& p) {
    // TESRs use the block lightmap, not BlockModelRenderer AO/directional face
    // multipliers. Sample the tile position and its six neighbours so opaque
    // block-entity states do not self-shadow to near-black.
    int best = 0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const glm::ivec3 q = p + glm::ivec3(dx, dy, dz);
                best = std::max(best, std::max<int>(world.getSkyLight(q.x,q.y,q.z),
                                                    world.getBlockLight(q.x,q.y,q.z)));
            }
        }
    }
    // Vanilla's lightmap is not a linear RGB multiplier. This smooth curve is
    // intentionally bright enough to preserve texture detail at low light while
    // still reaching exactly full brightness at level 15.
    const float n = std::clamp(static_cast<float>(best) / 15.0F, 0.0F, 1.0F);
    return 0.35F + 0.65F * std::sqrt(n);
}

void pushModelQuad(std::vector<V>& out, const std::array<glm::vec3,4>& p,
                   int u1,int v1,int u2,int v2,int tw,int th,float shade) {
    // TexturedQuad(vertices,u1,v1,u2,v2) in 1.12.2 assigns:
    // v0=(u2,v1), v1=(u1,v1), v2=(u1,v2), v3=(u2,v2).
    const float a=u1/static_cast<float>(tw), b=v1/static_cast<float>(th);
    const float c=u2/static_cast<float>(tw), d=v2/static_cast<float>(th);
    const std::array<std::array<float,2>,4> uv={{{c,b},{a,b},{a,d},{c,d}}};
    constexpr std::array<int,6> idx={0,1,2,0,2,3};
    for(int i:idx) out.push_back({p[i].x,p[i].y,p[i].z,uv[i][0],uv[i][1],shade});
}


void pushRectQuad(std::vector<V>& out, const std::array<glm::vec3,4>& p,
                  float u0,float v0,float u1,float v1,int tw,int th,float shade) {
    const float a=u0/static_cast<float>(tw), b=v0/static_cast<float>(th);
    const float c=u1/static_cast<float>(tw), d=v1/static_cast<float>(th);
    const std::array<std::array<float,2>,4> uv={{{a,b},{a,d},{c,d},{c,b}}};
    constexpr std::array<int,6> idx={0,1,2,0,2,3};
    for(int i:idx) out.push_back({p[i].x,p[i].y,p[i].z,uv[i][0],uv[i][1],shade});
}

// Exact ModelBox texture-unwrapping layout used by Minecraft 1.12.2.
void modelBox(std::vector<V>& out, int tw,int th,int texU,int texV,
              float x,float y,float z,int dx,int dy,int dz,
              const glm::mat4& m,float light) {
    const float x0=x/16.0F,y0=y/16.0F,z0=z/16.0F;
    const float x1=(x+dx)/16.0F,y1=(y+dy)/16.0F,z1=(z+dz)/16.0F;
    auto t=[&](float X,float Y,float Z){ glm::vec4 q=m*glm::vec4(X,Y,Z,1); return glm::vec3(q); };
    const glm::vec3 p0=t(x0,y0,z0),p1=t(x1,y0,z0),p2=t(x1,y1,z0),p3=t(x0,y1,z0);
    const glm::vec3 p4=t(x0,y0,z1),p5=t(x1,y0,z1),p6=t(x1,y1,z1),p7=t(x0,y1,z1);
    pushModelQuad(out,{p5,p1,p2,p6},texU+dz+dx,texV+dz,texU+dz+dx+dz,texV+dz+dy,tw,th,light);
    pushModelQuad(out,{p0,p4,p7,p3},texU,texV+dz,texU+dz,texV+dz+dy,tw,th,light);
    pushModelQuad(out,{p5,p4,p0,p1},texU+dz,texV,texU+dz+dx,texV+dz,tw,th,light);
    pushModelQuad(out,{p2,p3,p7,p6},texU+dz+dx,texV+dz,texU+dz+dx+dx,texV,tw,th,light);
    pushModelQuad(out,{p1,p0,p3,p2},texU+dz,texV+dz,texU+dz+dx,texV+dz+dy,tw,th,light);
    pushModelQuad(out,{p4,p5,p6,p7},texU+dz+dx+dz,texV+dz,texU+dz+dx+dz+dx,texV+dz+dy,tw,th,light);
}

int chestYaw(std::uint8_t meta) {
    if (meta==2) return 180; if(meta==4) return 90; if(meta==5) return -90; return 0;
}

std::string_view dyeName(int metadata) {
    constexpr std::array<std::string_view,16> n={"white","orange","magenta","light_blue","yellow","lime","pink","gray","silver","cyan","purple","blue","brown","green","red","black"};
    return n[static_cast<std::size_t>(std::clamp(metadata,0,15))];
}

} // namespace

BlockEntityRenderer::BlockEntityRenderer(const std::filesystem::path& root, BlockEntitySystem& entities)
    : entities_(entities), shader_(vertexSource, fragmentSource) {
    glGenVertexArrays(1,&vao_); glGenBuffers(1,&vbo_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(V),nullptr);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(V),reinterpret_cast<void*>(3*sizeof(float)));
    glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,sizeof(V),reinterpret_cast<void*>(5*sizeof(float)));
    glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
    const auto entity=root/"assets/minecraft/textures/entity";
    chestNormal_=loadTexture(entity/"chest/normal.png",64,64);
    chestTrapped_=loadTexture(entity/"chest/trapped.png",64,64);
    chestNormalDouble_=loadTexture(entity/"chest/normal_double.png",128,64);
    chestTrappedDouble_=loadTexture(entity/"chest/trapped_double.png",128,64);
    chestEnder_=loadTexture(entity/"chest/ender.png",64,64);
    bannerTexture_=loadTexture(entity/"banner_base.png",64,64);
    enchantingBookTexture_=loadTexture(entity/"enchanting_table_book.png",64,32);
    beaconBeamTexture_=loadTexture(entity/"beacon_beam.png",16,16);
    glBindTexture(GL_TEXTURE_2D, beaconBeamTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    signTexture_=loadTexture(entity/"sign.png",64,32);
    asciiTexture_=loadTexture(root/"assets/minecraft/textures/font/ascii.png",128,128);
    {
        int w=0,h=0,c=0; unsigned char* px=stbi_load((root/"assets/minecraft/textures/font/ascii.png").string().c_str(),&w,&h,&c,STBI_rgb_alpha);
        charWidths_.fill(1);
        if(px){
            for(int ch=0;ch<256;++ch){
                if(ch==32){charWidths_[32]=4;continue;}
                int cx=(ch&15)*8, cy=(ch>>4)*8, right=7;
                for(;right>=0;--right){ bool clear=true; for(int row=0;row<8;++row){ if(px[((cy+row)*128+cx+right)*4+3]!=0){clear=false;break;} } if(!clear)break; }
                charWidths_[static_cast<std::size_t>(ch)]=std::max(1,right+2);
            }
            stbi_image_free(px);
        }
    }
    for(int i=0;i<16;++i){
        bedTextures_[i]=loadTexture(entity/("bed/"+std::string(dyeName(i))+".png"),64,64);
        shulkerTextures_[i]=loadTexture(entity/("shulker/shulker_"+std::string(dyeName(i))+".png"),64,64);
    }
}

BlockEntityRenderer::~BlockEntityRenderer(){
    glDeleteTextures(1,&chestNormal_); glDeleteTextures(1,&chestTrapped_);
    glDeleteTextures(1,&chestNormalDouble_); glDeleteTextures(1,&chestTrappedDouble_);
    glDeleteTextures(1,&chestEnder_); glDeleteTextures(1,&bannerTexture_);
    glDeleteTextures(1,&enchantingBookTexture_); glDeleteTextures(1,&beaconBeamTexture_);
    glDeleteTextures(1,&signTexture_); glDeleteTextures(1,&asciiTexture_);
    glDeleteTextures(16,bedTextures_.data()); glDeleteTextures(16,shulkerTextures_.data());
    glDeleteBuffers(1,&vbo_); glDeleteVertexArrays(1,&vao_);
}

GLuint BlockEntityRenderer::loadTexture(const std::filesystem::path& path,int ew,int eh){
    int w=0,h=0,c=0; unsigned char* pixels=stbi_load(path.string().c_str(),&w,&h,&c,STBI_rgb_alpha);
    if(!pixels||w!=ew||h!=eh){ if(pixels)stbi_image_free(pixels); throw std::runtime_error("Missing/invalid Minecraft 1.12.2 block-entity asset: "+path.string()); }
    GLuint t=0; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    stbi_image_free(pixels); return t;
}

void BlockEntityRenderer::render(const World& world,const glm::mat4& view,const glm::mat4& projection,float partialTick){
    shader_.use(); shader_.setMat4("transform",projection*view); shader_.setInt("entityTexture",0);
    glActiveTexture(GL_TEXTURE0); glBindVertexArray(vao_);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    // ModelRenderer/TESR geometry is authored as closed model boxes and signs;
    // vanilla TESRs are not chunk face-culling candidates. Keep raster culling
    // off while drawing them so mirrored renderer transforms cannot punch holes.
    glDisable(GL_CULL_FACE);
    for(const auto& [unused,e]:entities_.entities()){
        (void)unused;
        const glm::mat4 identity(1.0F);
        switch(e.type){
            case RuntimeBlockEntityType::Chest: case RuntimeBlockEntityType::TrappedChest: case RuntimeBlockEntityType::EnderChest: renderChest(world,e,identity,partialTick); break;
            case RuntimeBlockEntityType::Sign: renderSign(world,e,identity); break;
            case RuntimeBlockEntityType::Bed: renderBed(world,e,identity); break;
            case RuntimeBlockEntityType::ShulkerBox: renderShulker(world,e,identity,partialTick); break;
            case RuntimeBlockEntityType::Banner: renderBanner(world,e,identity,partialTick); break;
            case RuntimeBlockEntityType::EnchantingTable: renderEnchantingBook(world,e,identity,partialTick); break;
            case RuntimeBlockEntityType::Beacon: renderBeaconBeam(world,e,identity,partialTick); break;
            case RuntimeBlockEntityType::Furnace: case RuntimeBlockEntityType::Hopper: case RuntimeBlockEntityType::BrewingStand:
            case RuntimeBlockEntityType::Jukebox: case RuntimeBlockEntityType::FlowerPot: case RuntimeBlockEntityType::MobSpawner:
                break; // normal JSON block model or deferred entity model
        }
    }
    glEnable(GL_CULL_FACE);
}

void BlockEntityRenderer::renderChest(const World& world,const RuntimeBlockEntity& e,const glm::mat4&,float partialTick){
    const BlockId id=static_cast<BlockId>(blockId(e.state));
    const glm::ivec3 west=e.position+glm::ivec3(-1,0,0), north=e.position+glm::ivec3(0,0,-1);
    if(static_cast<BlockId>(blockId(world.getBlock(west.x,west.y,west.z)))==id ||
       static_cast<BlockId>(blockId(world.getBlock(north.x,north.y,north.z)))==id) return;

    glm::ivec3 pair{}; bool large=false;
    for(glm::ivec3 d: {glm::ivec3(1,0,0),glm::ivec3(0,0,1)}){
        const glm::ivec3 p=e.position+d;
        if(static_cast<BlockId>(blockId(world.getBlock(p.x,p.y,p.z)))==id){ pair=p; large=true; break; }
    }
    float open=entities_.animation(e.position,partialTick);
    if(large) open=std::max(open,entities_.animation(pair,partialTick));
    float closed=1.0F-open;
    const float eased=1.0F-closed*closed*closed;

    // TileEntityChestRenderer transform, retained literally so ModelChest UVs,
    // hinge position and double-chest geometry line up with Mojang's textures.
    glm::mat4 root=glm::translate(glm::mat4(1),glm::vec3(e.position)+glm::vec3(0.0F,1.0F,1.0F));
    root=glm::scale(root,glm::vec3(1.0F,-1.0F,-1.0F));
    root=glm::translate(root,glm::vec3(0.5F));
    const std::uint8_t chestMeta = blockMetadata(e.state);
    const bool eastPair = large && pair.x > e.position.x;
    const bool southPair = large && pair.z > e.position.z;
    // TileEntityChestRenderer applies these two pre-rotation offsets for the
    // positive half of specific facing orientations. Without them the double
    // chest texture seam and latch are shifted by one block.
    if (chestMeta == 2U && eastPair) root=glm::translate(root,glm::vec3(1.0F,0.0F,0.0F));
    if (chestMeta == 5U && southPair) root=glm::translate(root,glm::vec3(0.0F,0.0F,-1.0F));
    root=glm::rotate(root,glm::radians(static_cast<float>(chestYaw(chestMeta))),glm::vec3(0,1,0));
    root=glm::translate(root,glm::vec3(-0.5F));

    const float light=lightAt(world,e.position);
    const int width=large?30:14, tw=large?128:64;
    std::vector<V> v; v.reserve(108);

    glm::mat4 body=glm::translate(root,glm::vec3(1.0F,6.0F,1.0F)/16.0F);
    modelBox(v,tw,64,0,19,0,0,0,width,10,14,body,light);

    glm::mat4 lid=glm::translate(root,glm::vec3(1.0F,7.0F,15.0F)/16.0F);
    lid=glm::rotate(lid,-eased*1.57079632679F,glm::vec3(1,0,0));
    modelBox(v,tw,64,0,0,0,-5,-14,width,5,14,lid,light);

    glm::mat4 knob=glm::translate(root,glm::vec3(large?16.0F:8.0F,7.0F,15.0F)/16.0F);
    knob=glm::rotate(knob,-eased*1.57079632679F,glm::vec3(1,0,0));
    modelBox(v,tw,64,0,0,-1,-2,-15,2,4,1,knob,light);

    GLuint tex = id==BlockId::EnderChest ? chestEnder_ :
        id==BlockId::TrappedChest ? (large?chestTrappedDouble_:chestTrapped_) : (large?chestNormalDouble_:chestNormal_);
    glBindTexture(GL_TEXTURE_2D,tex); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(V),v.data(),GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));
}

void BlockEntityRenderer::renderSign(const World& world,const RuntimeBlockEntity& e,const glm::mat4&){
    const BlockId id=static_cast<BlockId>(blockId(e.state));
    const std::uint8_t meta=blockMetadata(e.state);

    // TileEntitySignRenderer base transform. Keep this separate from the
    // ModelSign scale because sign text is rendered in the unscaled base frame.
    glm::mat4 base=glm::translate(glm::mat4(1),glm::vec3(e.position)+glm::vec3(0.5F));
    if(id==BlockId::StandingSign){
        const float degrees=static_cast<float>(meta)*360.0F/16.0F;
        base=glm::rotate(base,glm::radians(-degrees),glm::vec3(0,1,0));
    } else {
        float degrees=0.0F;
        if(meta==2)degrees=180.0F;
        else if(meta==4)degrees=90.0F;
        else if(meta==5)degrees=-90.0F;
        base=glm::rotate(base,glm::radians(-degrees),glm::vec3(0,1,0));
        base=glm::translate(base,glm::vec3(0.0F,-0.3125F,-0.4375F));
    }

    glm::mat4 model=glm::scale(base,glm::vec3(2.0F/3.0F,-2.0F/3.0F,-2.0F/3.0F));
    const float light=lightAt(world,e.position);
    std::vector<V> v;
    // Exact ModelSign boxes from MCP 9.42.
    modelBox(v,64,32,0,0,-12,-14,-1,24,12,2,model,light);
    if(id==BlockId::StandingSign)
        modelBox(v,64,32,0,14,-1,-2,-1,2,14,2,model,light);
    glBindTexture(GL_TEXTURE_2D,signTexture_);
    glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(V),v.data(),GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));

    const auto* lines=entities_.signLines(e.position);
    if(lines){
        std::vector<V> glyphs;
        glm::mat4 textM=glm::translate(base,glm::vec3(0.0F,0.33333334F,0.046666667F));
        textM=glm::scale(textM,glm::vec3(1.0F/96.0F,-1.0F/96.0F,1.0F/96.0F));
        for(int line=0;line<4;++line){
            const std::string& text=(*lines)[static_cast<std::size_t>(line)];
            int width=0; for(unsigned char ch:text) width+=charWidths_[ch];
            float cursor=-static_cast<float>(width)*0.5F;
            const float py=static_cast<float>(line*10-20);
            for(unsigned char ch:text){
                const int advance=charWidths_[ch];
                if(ch!=' '){
                    const int gx=(ch&15)*8, gy=(ch>>4)*8, visible=std::clamp(advance-1,1,8);
                    auto q=[&](float X,float Y){glm::vec4 r=textM*glm::vec4(X,Y,0,1);return glm::vec3(r);};
                    const std::array<glm::vec3,4> pos={q(cursor,py),q(cursor,py+8),q(cursor+visible,py+8),q(cursor+visible,py)};
                    pushRectQuad(glyphs,pos,static_cast<float>(gx),static_cast<float>(gy),static_cast<float>(gx+visible),static_cast<float>(gy+8),128,128,1.0F);
                }
                cursor+=static_cast<float>(advance);
            }
        }
        if(!glyphs.empty()){
            glDepthMask(GL_FALSE);
            glBindTexture(GL_TEXTURE_2D,asciiTexture_);
            glBindBuffer(GL_ARRAY_BUFFER,vbo_);
            glBufferData(GL_ARRAY_BUFFER,glyphs.size()*sizeof(V),glyphs.data(),GL_DYNAMIC_DRAW);
            glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(glyphs.size()));
            glDepthMask(GL_TRUE);
        }
    }
}

void BlockEntityRenderer::renderBed(const World& world,const RuntimeBlockEntity& e,const glm::mat4&){
    const std::uint8_t meta=blockMetadata(e.state);
    const bool head=(meta&8U)!=0U;
    const int facing=meta&3U;
    float rotation=0.0F, offsetX=0.0F, offsetZ=0.0F;
    if(facing==2){ rotation=180.0F; offsetX=1.0F; offsetZ=1.0F; }
    else if(facing==1){ rotation=-90.0F; offsetZ=1.0F; }
    else if(facing==3){ rotation=90.0F; offsetX=1.0F; }

    // TileEntityBedRenderer#renderPiece: translate, X+90, then facing rotation
    // around Z. ModelBed itself uses raw ModelRenderer/ModelBox coordinates.
    glm::mat4 root=glm::translate(glm::mat4(1),glm::vec3(e.position)+glm::vec3(offsetX,0.5625F,offsetZ));
    root=glm::rotate(root,glm::radians(90.0F),glm::vec3(1,0,0));
    root=glm::rotate(root,glm::radians(rotation),glm::vec3(0,0,1));
    const float light=lightAt(world,e.position);
    std::vector<V> v; v.reserve(108);

    if(head) modelBox(v,64,64,0,0,0,0,0,16,16,6,root,light);
    else     modelBox(v,64,64,0,22,0,0,0,16,16,6,root,light);

    struct Leg { int texV; float x,y,z; float rz; bool showHead; };
    constexpr std::array<Leg,4> legs = {{{0,0,6,-16,0,false},{6,0,6,0,90,true},
                                          {12,-16,6,-16,270,false},{18,-16,6,0,180,true}}};
    for(const Leg& leg:legs){
        if(leg.showHead!=head) continue;
        glm::mat4 lm=root;
        lm=glm::rotate(lm,glm::radians(leg.rz),glm::vec3(0,0,1));
        lm=glm::rotate(lm,glm::radians(90.0F),glm::vec3(1,0,0));
        modelBox(v,64,64,50,leg.texV,leg.x,leg.y,leg.z,3,3,3,lm,light);
    }

    glBindTexture(GL_TEXTURE_2D,bedTextures_[e.color&15U]); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(V),v.data(),GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));
}

void BlockEntityRenderer::renderShulker(const World& world,const RuntimeBlockEntity& e,const glm::mat4&,float partialTick){
    const float progress=entities_.animation(e.position,partialTick);
    const std::uint8_t facing=blockMetadata(e.state)&7U;

    // TileEntityShulkerBoxRenderer transform copied in the same order as MCP.
    glm::mat4 root=glm::translate(glm::mat4(1),glm::vec3(e.position)+glm::vec3(0.5F,1.5F,0.5F));
    root=glm::scale(root,glm::vec3(1.0F,-1.0F,-1.0F));
    root=glm::translate(root,glm::vec3(0.0F,1.0F,0.0F));
    root=glm::scale(root,glm::vec3(0.9995F));
    root=glm::translate(root,glm::vec3(0.0F,-1.0F,0.0F));
    switch(facing){
        case 0: root=glm::translate(root,glm::vec3(0,2,0)); root=glm::rotate(root,glm::radians(180.0F),glm::vec3(1,0,0)); break;
        case 2: root=glm::translate(root,glm::vec3(0,1,1)); root=glm::rotate(root,glm::radians(90.0F),glm::vec3(1,0,0)); root=glm::rotate(root,glm::radians(180.0F),glm::vec3(0,0,1)); break;
        case 3: root=glm::translate(root,glm::vec3(0,1,-1)); root=glm::rotate(root,glm::radians(90.0F),glm::vec3(1,0,0)); break;
        case 4: root=glm::translate(root,glm::vec3(-1,1,0)); root=glm::rotate(root,glm::radians(90.0F),glm::vec3(1,0,0)); root=glm::rotate(root,glm::radians(-90.0F),glm::vec3(0,0,1)); break;
        case 5: root=glm::translate(root,glm::vec3(1,1,0)); root=glm::rotate(root,glm::radians(90.0F),glm::vec3(1,0,0)); root=glm::rotate(root,glm::radians(90.0F),glm::vec3(0,0,1)); break;
        default: break;
    }

    const float light=lightAt(world,e.position);
    std::vector<V> v; v.reserve(72);
    glm::mat4 base=glm::translate(root,glm::vec3(0.0F,24.0F,0.0F)/16.0F);
    modelBox(v,64,64,0,28,-8,-8,-8,16,8,16,base,light);

    glm::mat4 lidRoot=root;
    lidRoot=glm::translate(lidRoot,glm::vec3(0.0F,-progress*0.5F,0.0F));
    lidRoot=glm::rotate(lidRoot,glm::radians(270.0F*progress),glm::vec3(0,1,0));
    lidRoot=glm::translate(lidRoot,glm::vec3(0.0F,24.0F,0.0F)/16.0F);
    modelBox(v,64,64,0,0,-8,-16,-8,16,12,16,lidRoot,light);

    glBindTexture(GL_TEXTURE_2D,shulkerTextures_[e.color&15U]); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(V),v.data(),GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));
}


void BlockEntityRenderer::renderBanner(const World& world,const RuntimeBlockEntity& e,const glm::mat4&,float partialTick){
    const BlockId id=static_cast<BlockId>(blockId(e.state));
    glm::mat4 root=glm::translate(glm::mat4(1.0F),glm::vec3(e.position)+glm::vec3(0.5F,0.0F,0.5F));
    if(id==BlockId::StandingBanner){
        root=glm::rotate(root,glm::radians(-static_cast<float>(blockMetadata(e.state)&15U)*22.5F),glm::vec3(0,1,0));
    } else {
        const std::uint8_t meta=blockMetadata(e.state)&7U;
        float yaw=meta==2?180.0F:meta==4?90.0F:meta==5?-90.0F:0.0F;
        root=glm::rotate(root,glm::radians(-yaw),glm::vec3(0,1,0));
        root=glm::translate(root,glm::vec3(0.0F,0.0F,-0.4375F));
    }
    const float wave=std::sin((static_cast<float>(e.position.x*7+e.position.y*9+e.position.z*13)+partialTick)*0.05F)*0.03F;
    const float light=lightAt(world,e.position);
    std::vector<V> v; v.reserve(108);
    // ModelBanner dimensions from 1.12.2: slate 20x40x1, top bar 20x2x2,
    // and a 2x42x2 pole for standing banners.
    glm::mat4 cloth=glm::translate(root,glm::vec3(0.0F,1.25F,0.0F));
    cloth=glm::rotate(cloth,wave,glm::vec3(1,0,0));
    modelBox(v,64,64,0,0,-10,-20,-1,20,40,1,cloth,light);
    modelBox(v,64,64,0,42,-10,-22,-1,20,2,2,root,light);
    if(id==BlockId::StandingBanner) modelBox(v,64,64,44,0,-1,-30,-1,2,42,2,glm::translate(root,glm::vec3(0,2.0F,0)),light);
    glBindTexture(GL_TEXTURE_2D,bannerTexture_); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(V),v.data(),GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));
}

void BlockEntityRenderer::renderEnchantingBook(const World& world,const RuntimeBlockEntity& e,const glm::mat4&,float partialTick){
    const float light=lightAt(world,e.position);
    const float bob=0.1F+std::sin((static_cast<float>(e.position.x+e.position.z)+partialTick)*0.1F)*0.03F;
    glm::mat4 root=glm::translate(glm::mat4(1.0F),glm::vec3(e.position)+glm::vec3(0.5F,1.0F+bob,0.5F));
    root=glm::rotate(root,glm::radians(25.0F),glm::vec3(0,1,0));
    root=glm::rotate(root,glm::radians(-20.0F),glm::vec3(1,0,0));
    std::vector<V> v; v.reserve(72);
    modelBox(v,64,32,0,0,-8,-1,-5,8,1,10,root,light);
    modelBox(v,64,32,0,0,0,-1,-5,8,1,10,root,light);
    modelBox(v,64,32,24,10,-7,0,-4,7,1,8,root,light);
    modelBox(v,64,32,24,10,0,0,-4,7,1,8,root,light);
    glBindTexture(GL_TEXTURE_2D,enchantingBookTexture_); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(V),v.data(),GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));
}

void BlockEntityRenderer::renderBeaconBeam(const World& world,const RuntimeBlockEntity& e,const glm::mat4&,float partialTick){
    if(entities_.beaconLevels(e.position)<=0) return;
    // 1.12.2 renders the beam independently of the normal block mesh.  The
    // texture scrolls vertically and remains translucent while extending to
    // the build limit.
    const float y0=static_cast<float>(e.position.y+1), y1=static_cast<float>(chunkHeight);
    const float x0=static_cast<float>(e.position.x)+0.3F, x1=static_cast<float>(e.position.x)+0.7F;
    const float z0=static_cast<float>(e.position.z)+0.3F, z1=static_cast<float>(e.position.z)+0.7F;
    const float scroll=std::fmod(partialTick*0.05F,1.0F);
    const float light=std::max(0.75F,lightAt(world,e.position));
    std::vector<V> v;
    pushRectQuad(v,{glm::vec3(x0,y0,z0),glm::vec3(x0,y1,z0),glm::vec3(x1,y1,z0),glm::vec3(x1,y0,z0)},0,scroll*16,16,16+scroll*16,16,16,light);
    pushRectQuad(v,{glm::vec3(x1,y0,z1),glm::vec3(x1,y1,z1),glm::vec3(x0,y1,z1),glm::vec3(x0,y0,z1)},0,scroll*16,16,16+scroll*16,16,16,light);
    pushRectQuad(v,{glm::vec3(x0,y0,z1),glm::vec3(x0,y1,z1),glm::vec3(x0,y1,z0),glm::vec3(x0,y0,z0)},0,scroll*16,16,16+scroll*16,16,16,light);
    pushRectQuad(v,{glm::vec3(x1,y0,z0),glm::vec3(x1,y1,z0),glm::vec3(x1,y1,z1),glm::vec3(x1,y0,z1)},0,scroll*16,16,16+scroll*16,16,16,light);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    glBindTexture(GL_TEXTURE_2D,beaconBeamTexture_); glBindBuffer(GL_ARRAY_BUFFER,vbo_);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(V),v.data(),GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(v.size()));
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}
