/* r3d_shape_key_gpu.c -- GPU Morph Target Storage. */

#include <r3d/r3d_shape_key.h>
#include <glad.h>

#include "./modules/r3d_shader.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static bool finite_vector(Vector3 v)
{
    return isfinite(v.x) &&
           isfinite(v.y) &&
           isfinite(v.z);
}

bool R3D_IsShapeKeyGpuValid(
    const R3D_ShapeKeyGpu* gpu)
{
    return gpu &&
           gpu->buffer != 0 &&
           gpu->texture != 0 &&
           gpu->source != NULL &&
           gpu->vertexCount > 0 &&
           gpu->keyCount > 0;
}

void R3D_UnloadShapeKeyGpu(R3D_ShapeKeyGpu gpu)
{
    if (gpu.texture)
    {
        // Avoid cached references to a deleted texture.
        r3d_shader_invalidate_cache();
        glDeleteTextures(1, &gpu.texture);
    }

    if (gpu.buffer)
    {
        glDeleteBuffers(1, &gpu.buffer);
    }

    free(gpu.minDeltas);
    free(gpu.maxDeltas);
}



R3D_ShapeKeyGpu R3D_LoadShapeKeyGpu(
    const R3D_ShapeKeySet* source)
{
    R3D_ShapeKeyGpu result = {0};

    if (!source || !source->keys ||
        source->keyCount <= 0 ||
        !R3D_IsMeshDataValid(source->baseMesh))
    {
        return result;
    }

    const int vertexCount = source->baseMesh.vertexCount;
    const int keyCount = source->keyCount;

    // Position, normal, tangent: three vec4 texels.
    const uint64_t texelCount =
        (uint64_t)vertexCount * (uint64_t)keyCount * 3u;

    GLint maxTexels = 0;
    GLint textureUnits = 0;

    glGetIntegerv(
        GL_MAX_TEXTURE_BUFFER_SIZE, &maxTexels
    );

    glGetIntegerv(
        GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
        &textureUnits
    );

    // R3D reserves texture units 17 and 18 for morphing.
    if (maxTexels <= 0 ||
        textureUnits <= 18 ||
        texelCount > (uint64_t)maxTexels ||
        texelCount > SIZE_MAX / sizeof(Vector4))
    {
        TraceLog(
            LOG_ERROR,
            "GPU morph storage exceeds supported device limits"
        );

        return result;
    }

    const size_t byteCount =
        (size_t)texelCount * sizeof(Vector4);

    Vector4* texels = calloc(
        (size_t)texelCount, sizeof(Vector4)
    );

    if (!texels) return result;

    result.minDeltas = calloc(
        (size_t)keyCount, sizeof(Vector3)
    );

    result.maxDeltas = calloc(
        (size_t)keyCount, sizeof(Vector3)
    );

    if (!result.minDeltas || !result.maxDeltas)
    {
        free(texels);
        free(result.minDeltas);
        free(result.maxDeltas);
        return (R3D_ShapeKeyGpu){0};
    }

    // ========================================
    // PACK THE IMPORTED DELTAS
    // ========================================

    for (int k = 0; k < keyCount; k++)
    {
        const R3D_ShapeKey* key = &source->keys[k];

        Vector3 minDelta = {
            +FLT_MAX, +FLT_MAX, +FLT_MAX
        };

        Vector3 maxDelta = {
            -FLT_MAX, -FLT_MAX, -FLT_MAX
        };

        for (int v = 0; v < vertexCount; v++)
        {
            Vector3 position = key->positionDeltas
                ? key->positionDeltas[v]
                : (Vector3){0};

            Vector3 normal = key->normalDeltas
                ? key->normalDeltas[v]
                : (Vector3){0};

            Vector3 tangent = key->tangentDeltas
                ? key->tangentDeltas[v]
                : (Vector3){0};

            if (!finite_vector(position) ||
                !finite_vector(normal) ||
                !finite_vector(tangent))
            {
                TraceLog(
                    LOG_ERROR,
                    "Invalid morph data in target %d", k
                );

                free(texels);
                free(result.minDeltas);
                free(result.maxDeltas);
                return (R3D_ShapeKeyGpu){0};
            }

            minDelta.x = fminf(minDelta.x, position.x);
            minDelta.y = fminf(minDelta.y, position.y);
            minDelta.z = fminf(minDelta.z, position.z);

            maxDelta.x = fmaxf(maxDelta.x, position.x);
            maxDelta.y = fmaxf(maxDelta.y, position.y);
            maxDelta.z = fmaxf(maxDelta.z, position.z);

            const size_t offset =
                ((size_t)k * vertexCount + v) * 3;

            texels[offset + 0] = (Vector4){
                position.x, position.y, position.z, 0
            };

            texels[offset + 1] = (Vector4){
                normal.x, normal.y, normal.z, 0
            };

            texels[offset + 2] = (Vector4){
                tangent.x, tangent.y, tangent.z, 0
            };
        }

        result.minDeltas[k] = minDelta;
        result.maxDeltas[k] = maxDelta;
    }

    // ========================================
    // CREATE IMMUTABLE GPU DATA STORAGE
    // ========================================

    GLint oldActiveTexture = 0;
    GLint oldTexture = 0;
    GLint oldBuffer = 0;

    glGetIntegerv(
        GL_ACTIVE_TEXTURE, &oldActiveTexture
    );

    glActiveTexture(GL_TEXTURE0);

    glGetIntegerv(
        GL_TEXTURE_BINDING_BUFFER, &oldTexture
    );

    glGetIntegerv(
        GL_TEXTURE_BUFFER, &oldBuffer
    );

    glGenBuffers(1, &result.buffer);
    glBindBuffer(GL_TEXTURE_BUFFER, result.buffer);

    glBufferData(
        GL_TEXTURE_BUFFER,
        (GLsizeiptr)byteCount,
        texels,
        GL_STATIC_DRAW
    );

    glGenTextures(1, &result.texture);
    glBindTexture(GL_TEXTURE_BUFFER, result.texture);

    glTexBuffer(
        GL_TEXTURE_BUFFER,
        GL_RGBA32F,
        result.buffer
    );

    GLenum error = glGetError();

    glBindTexture(
        GL_TEXTURE_BUFFER, (GLuint)oldTexture
    );

    glBindBuffer(
        GL_TEXTURE_BUFFER, (GLuint)oldBuffer
    );

    glActiveTexture((GLenum)oldActiveTexture);

    free(texels);

    if (error != GL_NO_ERROR ||
        !result.buffer || !result.texture)
    {
        TraceLog(
            LOG_ERROR,
            "GPU morph upload failed (OpenGL error %u)",
            (unsigned)error
        );

        R3D_UnloadShapeKeyGpu(result);
        return (R3D_ShapeKeyGpu){0};
    }

    result.source = source;
    result.vertexCount = vertexCount;
    result.keyCount = keyCount;

    return result;
}