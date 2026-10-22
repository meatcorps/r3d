/* r3d_texture.h -- R3D Texture Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_texture.h>

#include "./modules/r3d_texture.h"
#include "./common/r3d_image.h"
#include "./r3d_core_state.h"
#include "modules/r3d_target.h"

// ========================================
// PRIVATE API
// ========================================

static r3d_target_t resolve_shader_buffer(R3D_ShaderBuffer buffer)
{
    switch (buffer)
    {
        #define MAP_BUFFER(name) \
            case R3D_SHADER_BUFFER_##name: \
                return R3D_TARGET_##name;

        MAP_BUFFER(SCENE_0)
        MAP_BUFFER(SCENE_1)

        MAP_BUFFER(ALBEDO)
        MAP_BUFFER(NORMAL)
        MAP_BUFFER(GEOM_NORMAL)
        MAP_BUFFER(ORM)
        MAP_BUFFER(OUTLINE_MASK)
        MAP_BUFFER(OUTLINE_HORIZONTAL)
        MAP_BUFFER(DEPTH)

        MAP_BUFFER(SELECTOR)
        MAP_BUFFER(RADIANCE)
        MAP_BUFFER(SPECULAR)
        MAP_BUFFER(VFOG_RAD)

        MAP_BUFFER(SSAO_0)
        MAP_BUFFER(SSAO_1)
        MAP_BUFFER(SSIL_0)
        MAP_BUFFER(SSIL_1)
        MAP_BUFFER(SSGI_0)
        MAP_BUFFER(SSGI_1)
        MAP_BUFFER(SSR)

        MAP_BUFFER(DOF_COC)
        MAP_BUFFER(DOF_0)
        MAP_BUFFER(DOF_1)
        MAP_BUFFER(BLOOM)
        MAP_BUFFER(LUMINANCE)

        MAP_BUFFER(EXPOSURE_0)
        MAP_BUFFER(EXPOSURE_1)

        MAP_BUFFER(SMAA_EDGES)
        MAP_BUFFER(SMAA_BLEND)

        #undef MAP_BUFFER

        default:
            return R3D_TARGET_INVALID;
    }
}

static bool bind_shader_buffer(Shader shader, int uniformLocation, R3D_ShaderBuffer buffer, int mipLevel)
{
    if (!R3D.initialized ||
        shader.id == 0 ||
        uniformLocation < 0)
    {
        return false;
    }

    r3d_target_t target = resolve_shader_buffer(buffer);

    if (!r3d_target_exists(target))
        return false;

    int minLevel = r3d_target_get_min_level(target);
    int maxLevel = r3d_target_get_max_level(target);

    // -1 means the target's native base resolution.
    if (mipLevel == -1)
        mipLevel = minLevel;

    if (mipLevel < minLevel || mipLevel > maxLevel)
        return false;

    Texture2D texture = {0};

    texture.id = r3d_target_get_level(
        target,
        mipLevel
    );

    // Registers the existing GPU resource with Raylib.
    SetShaderValueTexture(
        shader,
        uniformLocation,
        texture
    );

    return true;
}

// ========================================
// PUBLIC API
// ========================================

bool R3D_SetRaylibShaderBuffer(Shader shader, int uniformLocation, R3D_ShaderBuffer buffer)
{
    return bind_shader_buffer(
        shader,
        uniformLocation,
        buffer,
        -1
    );
}

bool R3D_SetRaylibShaderBufferLevel(Shader shader, int uniformLocation, R3D_ShaderBuffer buffer, int mipLevel)
{
    return bind_shader_buffer(
        shader,
        uniformLocation,
        buffer,
        mipLevel
    );
}

Texture2D R3D_LoadTexture(const char* fileName, bool isColor)
{
    return R3D_LoadTextureEx(fileName, R3D.textureWrap, R3D.textureFilter, isColor);
}

Texture2D R3D_LoadTextureEx(const char* fileName, TextureWrap wrap, TextureFilter filter, bool isColor)
{
    Texture2D texture = {0};

    Image image = LoadImage(fileName);
    if (!IsImageValid(image))
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to load texture (path: '%s')", fileName);
        return texture;
    }

    texture = r3d_image_upload(&image, wrap, filter, isColor);

    if (texture.id != 0)
    {
        R3D_TRACELOG(LOG_INFO, "Texture loaded successfully (path: '%s')", fileName);
    }
    else
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to load texture (path: '%s')", fileName);
    }

    UnloadImage(image);

    return texture;
}

Texture2D R3D_LoadTextureFromImage(Image image, bool isColor)
{
    return R3D_LoadTextureFromImageEx(image, R3D.textureWrap, R3D.textureFilter, isColor);
}

Texture2D R3D_LoadTextureFromImageEx(Image image, TextureWrap wrap, TextureFilter filter, bool isColor)
{
    Texture2D texture = r3d_image_upload(&image, wrap, filter, isColor);

    if (texture.id != 0)
    {
        R3D_TRACELOG(LOG_INFO, "Texture loaded successfully from image");
    }
    else
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to load texture from image");
    }

    return texture;
}

Texture2D R3D_LoadTextureFromMemory(const char* fileType, const void* fileData, int dataSize, bool isColor)
{
    return R3D_LoadTextureFromMemoryEx(fileType, fileData, dataSize, R3D.textureWrap, R3D.textureFilter, isColor);
}

Texture2D R3D_LoadTextureFromMemoryEx(const char* fileType, const void* fileData, int dataSize, TextureWrap wrap, TextureFilter filter, bool isColor)
{
    Texture2D texture = {0};

    Image image = LoadImageFromMemory(fileType, fileData, dataSize);
    if (!IsImageValid(image))
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to load texture from memory (type: '%s')", fileType);
        return texture;
    }

    texture = r3d_image_upload(&image, wrap, filter, isColor);

    if (texture.id != 0)
    {
        R3D_TRACELOG(LOG_INFO, "Texture loaded successfully from memory (type: '%s')", fileType);
    }
    else
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to load texture from memory (type: '%s')", fileType);
    }

    UnloadImage(image);

    return texture;
}

void R3D_UnloadTexture(Texture2D texture)
{
    if (texture.id == 0 || r3d_texture_is_default(texture.id))
    {
        return;
    }

    UnloadTexture(texture);
}
