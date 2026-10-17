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
#include "./modules/r3d_render.h"
#include "./modules/r3d_target.h"
#include "./r3d_core_state.h"
#include "./common/r3d_math.h"

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

R3D_RenderStats R3D_GetRenderStats(void)
{
    return R3D_MOD_RENDER.stats;
}

void R3D_DrawRenderStats(int x, int y)
{
    R3D_RenderStats stats = R3D_GetRenderStats();

    const int width = 370;
    const int height = 177;
    const int left = x + 10;

    DrawRectangle(x, y, width, height, Fade(BLACK, 0.82f));

    DrawText("R3D RENDER STATS", left, y + 8, 19, YELLOW);

    DrawText(
        TextFormat("FPS: %d    Queued: %llu",
            GetFPS(),
            (unsigned long long)stats.submittedCalls),
        left, y + 35, 16, RAYWHITE
    );

    DrawText(
        TextFormat("GL draws: %llu    Instanced: %llu",
            (unsigned long long)stats.drawCalls,
            (unsigned long long)stats.instancedDrawCalls),
        left, y + 58, 16, RAYWHITE
    );

    DrawText(
        TextFormat("Mesh: %llu  Decal: %llu  Internal: %llu",
            (unsigned long long)stats.meshDrawCalls,
            (unsigned long long)stats.decalDrawCalls,
            (unsigned long long)stats.internalDrawCalls),
        left, y + 81, 16, LIGHTGRAY
    );

    DrawText(
        TextFormat("Triangles: %llu",
            (unsigned long long)stats.triangles),
        left, y + 104, 16, SKYBLUE
    );

    DrawText(
        TextFormat("Vertex references: %llu",
            (unsigned long long)stats.vertexReferences),
        left, y + 127, 16, LIGHTGRAY
    );

    DrawText(
        TextFormat("Mesh instances: %llu",
            (unsigned long long)stats.meshInstances),
        left, y + 150, 16, LIGHTGRAY
    );
}

// ========================================
// MATRIX COMPOSITION
// ========================================

Matrix R3D_MatrixFromTRS(Vector3 position, Quaternion rotation, Vector3 scale)
{
    return r3d_matrix_srt_quat(scale, rotation, position);
}

Matrix R3D_MatrixFromTR(Matrix original, Vector3 position, Quaternion rotation)
{
    Vector3 oldPosition;
    Vector3 scale;
    Quaternion oldRotation;

    r3d_matrix_decompose(
        original, &oldPosition, &oldRotation, &scale
    );

    return r3d_matrix_srt_quat(scale, rotation, position);
}

Matrix R3D_MatrixFromTS(Matrix original, Vector3 position, Vector3 scale)
{
    Vector3 oldPosition;
    Vector3 oldScale;
    Quaternion rotation;

    r3d_matrix_decompose(
        original, &oldPosition, &rotation, &oldScale
    );

    return r3d_matrix_srt_quat(scale, rotation, position);
}

Matrix R3D_MatrixFromRS(Matrix original, Quaternion rotation, Vector3 scale)
{
    Vector3 position = {
        original.m12, original.m13, original.m14
    };

    return r3d_matrix_srt_quat(scale, rotation, position);
}

Matrix R3D_MatrixFromT(Matrix original, Vector3 position)
{
    // Translation-only replacement preserves shear and
    // all other matrix elements exactly.
    original.m12 = position.x;
    original.m13 = position.y;
    original.m14 = position.z;

    return original;
}

Matrix R3D_MatrixFromR(Matrix original, Quaternion rotation)
{
    Vector3 position;
    Vector3 scale;
    Quaternion oldRotation;

    r3d_matrix_decompose(
        original, &position, &oldRotation, &scale
    );

    return r3d_matrix_srt_quat(scale, rotation, position);
}

Matrix R3D_MatrixFromS(Matrix original, Vector3 scale)
{
    Vector3 position;
    Vector3 oldScale;
    Quaternion rotation;

    r3d_matrix_decompose(
        original, &position, &rotation, &oldScale
    );

    return r3d_matrix_srt_quat(scale, rotation, position);
}