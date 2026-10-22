/* r3d_texture.h -- R3D Texture Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#ifndef R3D_TEXTURE_H
#define R3D_TEXTURE_H

#include "./r3d_platform.h"
#include <raylib.h>

/**
 * @defgroup Texture
 * @{
 */

// ========================================
// PUBLIC API
// ========================================

#ifdef __cplusplus
extern "C" {
#endif

 typedef enum R3D_ShaderBuffer {
  R3D_SHADER_BUFFER_SCENE_0,
  R3D_SHADER_BUFFER_SCENE_1,

  R3D_SHADER_BUFFER_ALBEDO,
  R3D_SHADER_BUFFER_NORMAL,
  R3D_SHADER_BUFFER_GEOM_NORMAL,
  R3D_SHADER_BUFFER_ORM,
  R3D_SHADER_BUFFER_OUTLINE_MASK,
  R3D_SHADER_BUFFER_OUTLINE_HORIZONTAL,
  R3D_SHADER_BUFFER_DEPTH,

  R3D_SHADER_BUFFER_SELECTOR,
  R3D_SHADER_BUFFER_RADIANCE,
  R3D_SHADER_BUFFER_SPECULAR,
  R3D_SHADER_BUFFER_VFOG_RAD,

  R3D_SHADER_BUFFER_SSAO_0,
  R3D_SHADER_BUFFER_SSAO_1,
  R3D_SHADER_BUFFER_SSIL_0,
  R3D_SHADER_BUFFER_SSIL_1,
  R3D_SHADER_BUFFER_SSGI_0,
  R3D_SHADER_BUFFER_SSGI_1,
  R3D_SHADER_BUFFER_SSR,

  R3D_SHADER_BUFFER_DOF_COC,
  R3D_SHADER_BUFFER_DOF_0,
  R3D_SHADER_BUFFER_DOF_1,
  R3D_SHADER_BUFFER_BLOOM,
  R3D_SHADER_BUFFER_LUMINANCE,

  R3D_SHADER_BUFFER_EXPOSURE_0,
  R3D_SHADER_BUFFER_EXPOSURE_1,

  R3D_SHADER_BUFFER_SMAA_EDGES,
  R3D_SHADER_BUFFER_SMAA_BLEND
} R3D_ShaderBuffer;

 /**
  * Bind an existing internal R3D GPU buffer
  * to a Raylib shader sampler.
  *
  * Does not copy or transfer the texture.
  * R3D retains ownership.
  *
  * Returns false if the target is unavailable.
  */
 R3DAPI bool R3D_SetRaylibShaderBuffer(Shader shader, int uniformLocation, R3D_ShaderBuffer buffer);

 /**
  * Same operation, with an explicit R3D virtual mip level.
  */
 R3DAPI bool R3D_SetRaylibShaderBufferLevel(Shader shader, int uniformLocation, R3D_ShaderBuffer buffer, int mipLevel);

/**
 * @brief Loads a texture from the specified file path.
 *
 * If 'isColor' is true, the texture is loaded using the currently defined color space (sRGB by default).
 * The wrap and filter modes are taken from the current global default state.
 *
 * @param fileName The path to the texture file.
 * @param isColor Whether the texture should be treated as color data.
 * @return The loaded texture.
 */
R3DAPI Texture2D R3D_LoadTexture(const char* fileName, bool isColor);

/**
 * @brief Loads a texture from the specified file path.
 *
 * If 'isColor' is true, the texture is loaded using the currently defined color space (sRGB by default).
 *
 * @param fileName The path to the texture file.
 * @param wrap The texture wrap mode.
 * @param filter The texture filter mode.
 * @param isColor Whether the texture should be treated as color data.
 * @return The loaded texture.
 */
R3DAPI Texture2D R3D_LoadTextureEx(const char* fileName, TextureWrap wrap, TextureFilter filter, bool isColor);

/**
 * @brief Loads a texture from the specified image.
 *
 * If 'isColor' is true, the texture is loaded using the currently defined color space (sRGB by default).
 * The wrap and filter modes are taken from the current global default state.
 *
 * @param image The source image.
 * @param isColor Whether the texture should be treated as color data.
 * @return The loaded texture.
 */
R3DAPI Texture2D R3D_LoadTextureFromImage(Image image, bool isColor);

/**
 * @brief Loads a texture from the specified image.
 *
 * If 'isColor' is true, the texture is loaded using the currently defined color space (sRGB by default).
 *
 * @param image The source image.
 * @param wrap The texture wrap mode.
 * @param filter The texture filter mode.
 * @param isColor Whether the texture should be treated as color data.
 * @return The loaded texture.
 */
R3DAPI Texture2D R3D_LoadTextureFromImageEx(Image image, TextureWrap wrap, TextureFilter filter, bool isColor);

/**
 * @brief Loads a texture directly from memory.
 *
 * If 'isColor' is true, the texture is loaded using the currently defined color space (sRGB by default).
 * The wrap and filter modes are taken from the current global default state.
 *
 * @param fileType The file type/extension used to interpret the data.
 * @param fileData A pointer to the file data in memory.
 * @param dataSize The size of the file data in bytes.
 * @param isColor Whether the texture should be treated as color data.
 * @return The loaded texture.
 */
R3DAPI Texture2D R3D_LoadTextureFromMemory(const char* fileType, const void* fileData, int dataSize, bool isColor);

/**
 * @brief Loads a texture directly from memory.
 *
 * If 'isColor' is true, the texture is loaded using the currently defined color space (sRGB by default).
 *
 * @param fileType The file type/extension used to interpret the data.
 * @param fileData A pointer to the file data in memory.
 * @param dataSize The size of the file data in bytes.
 * @param wrap The texture wrap mode.
 * @param filter The texture filter mode.
 * @param isColor Whether the texture should be treated as color data.
 * @return The loaded texture.
 */
R3DAPI Texture2D R3D_LoadTextureFromMemoryEx(const char* fileType, const void* fileData, int dataSize, TextureWrap wrap, TextureFilter filter, bool isColor);

/**
 * @brief Unloads a texture.
 *
 * This function calls raylib's `UnloadTexture` internally, while ensuring that
 * the provided texture is not an internal r3d texture.
 *
 * @param texture The texture to unload.
 */
R3DAPI void R3D_UnloadTexture(Texture2D texture);

#ifdef __cplusplus
} // extern "C"
#endif

/** @} */ // end of Texture

#endif // R3D_TEXTURE_H
