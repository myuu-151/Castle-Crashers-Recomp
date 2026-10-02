#include "render/gl.h"

#include <SDL3/SDL_video.h>

namespace gl {

#define CC_GL_DEFINE(type, name) type name = nullptr;
CC_GL_FUNCTIONS(CC_GL_DEFINE)
#undef CC_GL_DEFINE

bool load() {
    bool ok = true;
#define CC_GL_LOAD(type, name)                                         \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));      \
    ok = ok && name != nullptr;
    CC_GL_FUNCTIONS(CC_GL_LOAD)
#undef CC_GL_LOAD
    return ok;
}

}  // namespace gl
