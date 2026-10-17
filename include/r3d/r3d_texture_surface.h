#ifndef R3D_TEXTURE_SURFACE_H
#define R3D_TEXTURE_SURFACE_H

#include "./r3d_platform.h"
#include "./r3d_material.h"
#include "./r3d_surface_shader.h"
#include <raylib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum R3D_TextureAspectMode {
    R3D_TEXTURE_ASPECT_STRETCH,
    R3D_TEXTURE_ASPECT_CONTAIN,
    R3D_TEXTURE_ASPECT_COVER,
    R3D_TEXTURE_ASPECT_NATIVE
} R3D_TextureAspectMode;

typedef enum R3D_TextureSurfaceChannel {
    R3D_TEXTURE_SURFACE_ALBEDO,
    R3D_TEXTURE_SURFACE_EMISSION,
    R3D_TEXTURE_SURFACE_BOTH
} R3D_TextureSurfaceChannel;

typedef struct R3D_TextureDrawOptions {
    R3D_TextureAspectMode aspectMode;
    R3D_TextureSurfaceChannel channel;
    R3D_BillboardMode billboardMode;
    R3D_TransparencyMode transparencyMode;
    R3D_CullMode cullMode;
    R3D_DepthState depth;
    Color tint;
    float emissionIntensity;
    float pixelsPerUnit;
} R3D_TextureDrawOptions;

// Shared resources. Optional explicit warm-up; drawing also initializes lazily.
R3DAPI bool R3D_InitTexture3DResources(void);
R3DAPI void R3D_UnloadTexture3DResources(void);

// Raylib-style drawing.
R3DAPI void R3D_DrawTexture3D(Texture2D texture, Vector3 position, float height);
R3DAPI void R3D_DrawTexture3DEx(Texture2D texture, Vector3 position, Quaternion rotation, Vector2 size, Color tint);
R3DAPI void R3D_DrawTexture3DPro(Texture2D texture, Rectangle source, Vector3 position, Quaternion rotation, Vector2 size, Vector2 origin, R3D_TextureDrawOptions options);
R3DAPI void R3D_DrawTexture3DNative(Texture2D texture, Vector3 position, float pixelsPerUnit);

// RenderTexture variants. Source rectangles use Raylib's top-left pixel convention.
R3DAPI void R3D_DrawRenderTexture3D(RenderTexture2D target, Vector3 position, float height);
R3DAPI void R3D_DrawRenderTexture3DEx(RenderTexture2D target, Vector3 position, Quaternion rotation, Vector2 size, Color tint);
R3DAPI void R3D_DrawRenderTexture3DPro(RenderTexture2D target, Rectangle source, Vector3 position, Quaternion rotation, Vector2 size, Vector2 origin, R3D_TextureDrawOptions options);

R3DAPI R3D_TextureDrawOptions R3D_DefaultTextureDrawOptions(void);

// Public shader and material creation.
R3DAPI R3D_SurfaceShader* R3D_LoadTextureSurfaceShader(void);
R3DAPI R3D_Material R3D_CreateTextureSurfaceMaterial(R3D_SurfaceShader* shader);

// Material helpers. Textures are borrowed, not owned.
R3DAPI void R3D_SetTextureSurfaceTexture(R3D_Material* material, Texture2D texture, R3D_TextureSurfaceChannel channel);
R3DAPI void R3D_SetTextureSurfaceRenderTarget(R3D_Material* material, RenderTexture2D target, R3D_TextureSurfaceChannel channel);
R3DAPI void R3D_SetTextureSurfaceRegion(R3D_Material* material, Rectangle source, int textureWidth, int textureHeight, bool flipY);
R3DAPI void R3D_SetTextureSurfaceHDR(R3D_Material* material, float intensity);
R3DAPI void R3D_SetTextureSurfaceNormal(R3D_Material* material, Texture2D normal, float strength);
R3DAPI void R3D_SetTextureSurfaceORM(R3D_Material* material, Texture2D orm);

#ifdef __cplusplus
}
#endif

#endif // R3D_TEXTURE_SURFACE_H