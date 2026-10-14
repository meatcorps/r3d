/* r3d_utils.h -- R3D Utility Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_utils.h>
#include <math.h>
#include "./modules/r3d_texture.h"
#include "./modules/r3d_target.h"
#include "./r3d_core_state.h"

// ========================================
// PUBLIC API
// ========================================

bool R3D_IsBoundingBoxValid(BoundingBox bounds)
{
    return isfinite(bounds.min.x) &&
           isfinite(bounds.min.y) &&
           isfinite(bounds.min.z) &&
           isfinite(bounds.max.x) &&
           isfinite(bounds.max.y) &&
           isfinite(bounds.max.z) &&
           bounds.min.x <= bounds.max.x &&
           bounds.min.y <= bounds.max.y &&
           bounds.min.z <= bounds.max.z;
}

bool R3D_ExpandBoundingBox(BoundingBox bounds, Vector3 padding, BoundingBox* result)
{
    if (!result || !R3D_IsBoundingBoxValid(bounds)) return false;

    if (!isfinite(padding.x) || !isfinite(padding.y) || !isfinite(padding.z)) return false;
    if (padding.x < 0.0f || padding.y < 0.0f || padding.z < 0.0f) return false;

    BoundingBox expanded = {
        .min = {
            bounds.min.x - padding.x,
            bounds.min.y - padding.y,
            bounds.min.z - padding.z
        },
        .max = {
            bounds.max.x + padding.x,
            bounds.max.y + padding.y,
            bounds.max.z + padding.z
        }
    };

    if (!R3D_IsBoundingBoxValid(expanded)) return false;

    *result = expanded;
    return true;
}

Texture2D R3D_GetWhiteTexture(void)
{
    Texture2D texture = {0};
    texture.id = r3d_texture_get(R3D_TEXTURE_WHITE);
    texture.width = 1;
    texture.height = 1;
    texture.mipmaps = 1;
    texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return texture;
}

Texture2D R3D_GetBlackTexture(void)
{
    Texture2D texture = {0};
    texture.id = r3d_texture_get(R3D_TEXTURE_BLACK);
    texture.width = 1;
    texture.height = 1;
    texture.mipmaps = 1;
    texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return texture;
}

Texture2D R3D_GetNormalTexture(void)
{
    Texture2D texture = {0};
    texture.id = r3d_texture_get(R3D_TEXTURE_NORMAL);
    texture.width = 1;
    texture.height = 1;
    texture.mipmaps = 1;
    texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
    return texture;
}

Texture2D R3D_GetBufferNormal(void)
{
    Texture2D texture = {0};
    texture.id = r3d_target_get(R3D_TARGET_NORMAL);
    texture.width = R3D_TARGET_SIZE_W;
    texture.height = R3D_TARGET_SIZE_H;
    texture.mipmaps = 1;
    texture.format = PIXELFORMAT_UNCOMPRESSED_R32;
    return texture;
}

Texture2D R3D_GetBufferDepth(void)
{
    Texture2D texture = {0};
    texture.id = r3d_target_get(R3D_TARGET_DEPTH);
    texture.width = R3D_TARGET_SIZE_W;
    texture.height = R3D_TARGET_SIZE_H;
    texture.mipmaps = 1;
    texture.format = PIXELFORMAT_UNCOMPRESSED_R16;
    return texture;
}

Matrix R3D_GetMatrixView(void)
{
    return R3D.viewState.view;
}

Matrix R3D_GetMatrixInvView(void)
{
    return R3D.viewState.invView;
}

Matrix R3D_GetMatrixProjection(void)
{
    return R3D.viewState.proj;
}

Matrix R3D_GetMatrixInvProjection(void)
{
    return R3D.viewState.invProj;
}

Matrix R3D_GetMatrixViewProjection(void)
{
    return R3D.viewState.viewProj;
}

bool R3D_MatrixEquals(Matrix a, Matrix b)
{
    float16 valuesA = MatrixToFloatV(a);
    float16 valuesB = MatrixToFloatV(b);

    for (int i = 0; i < 16; i++)
    {
        if (!(fabsf(valuesA.v[i] - valuesB.v[i]) <= 0.00001f)) {
            return false;
        }
    }

    return true;
}