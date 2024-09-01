#pragma warning(push)
#pragma warning(disable : 4244) // conversion warning in stb lib
    #define STB_IMAGE_IMPLEMENTATION
    #include "extern/stb/stb_image.h"
#pragma warning(pop)

#define DEBUG_DRAW_IMPLEMENTATION
#include "extern/debug_draw/debug_draw.hpp"

// NOTE: must include this before tiny_gltf, due to 'TINYGLTF_IMPLEMENTATION'
#include "extern/json/json.hpp"

#pragma warning(push)
#pragma warning(disable : 4018) // '>=': signed/unsigned mismatch
#pragma warning(disable : 4267) // 'argument': conversion from 'size_t' to 'int'
    #define TINYGLTF_IMPLEMENTATION
    #include "extern/json/tiny_gltf.h"
#pragma warning(pop)
