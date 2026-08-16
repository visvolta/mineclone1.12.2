#pragma once

#include <glad/gl.h>

class TextureAtlas {
public:
    TextureAtlas();
    ~TextureAtlas();
    TextureAtlas(const TextureAtlas&) = delete;
    TextureAtlas& operator=(const TextureAtlas&) = delete;
    void bind(GLuint unit = 0) const;

private:
    GLuint id_ = 0;
};
