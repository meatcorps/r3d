/* r3d_shape_key.c -- CPU Shape Key Evaluation.
 *
 * Copyright (c) 2025-2026 Dennis Steffen
 */

#include <r3d/r3d_shape_key.h>
#include <r3d/r3d_utils.h>
#include <r3d/r3d_vertex.h>
#include <raymath.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool finite_vec3(Vector3 v)
{
    return isfinite(v.x) &&
           isfinite(v.y) &&
           isfinite(v.z);
}

static Vector3 normalize_or(Vector3 v, Vector3 fallback)
{
    const float lengthSq = Vector3LengthSqr(v);

    if (!isfinite(lengthSq) || lengthSq <= 1e-12f)
    {
        return fallback;
    }

    return Vector3Scale(v, 1.0f / sqrtf(lengthSq));
}

bool R3D_CalculateConservativeShapeKeyBounds(BoundingBox baseBounds, const R3D_ShapeKeyGpu* gpu, const R3D_ShapeKeyInstance* instance, BoundingBox* result)
{
    if (!result || !gpu || !instance || !instance->source || !instance->weights) return false;
    if (!R3D_IsBoundingBoxValid(baseBounds)) return false;
    if (gpu->source != instance->source || gpu->keyCount <= 0 || gpu->keyCount != instance->source->keyCount) return false;
    if (!gpu->minDeltas || !gpu->maxDeltas) return false;

    BoundingBox bounds = baseBounds;

    for (int k = 0; k < gpu->keyCount; k++)
    {
        const float weight = instance->weights[k];
        const Vector3 minDelta = gpu->minDeltas[k];
        const Vector3 maxDelta = gpu->maxDeltas[k];

        if (!isfinite(weight) || !finite_vec3(minDelta) || !finite_vec3(maxDelta)) return false;
        if (minDelta.x > maxDelta.x || minDelta.y > maxDelta.y || minDelta.z > maxDelta.z) return false;
        if (weight == 0.0f) continue;

        const Vector3 lower = weight >= 0.0f ? minDelta : maxDelta;
        const Vector3 upper = weight >= 0.0f ? maxDelta : minDelta;

        bounds.min.x += lower.x * weight;
        bounds.min.y += lower.y * weight;
        bounds.min.z += lower.z * weight;

        bounds.max.x += upper.x * weight;
        bounds.max.y += upper.y * weight;
        bounds.max.z += upper.z * weight;
    }

    if (!R3D_IsBoundingBoxValid(bounds)) return false;

    *result = bounds;
    return true;
}

void R3D_UnloadShapeKeySet(R3D_ShapeKeySet set)
{
    for (int i = 0; i < set.keyCount; i++)
    {
        free(set.keys[i].positionDeltas);
        free(set.keys[i].normalDeltas);
        free(set.keys[i].tangentDeltas);
    }

    free(set.keys);
    R3D_UnloadMeshData(set.baseMesh);
}

int R3D_GetShapeKeyCount(const R3D_ShapeKeySet* set)
{
    return set ? set->keyCount : 0;
}

const char* R3D_GetShapeKeyName(
    const R3D_ShapeKeySet* set,
    int index)
{
    if (!set || !set->keys ||
        index < 0 || index >= set->keyCount)
    {
        return NULL;
    }

    return set->keys[index].name;
}

int R3D_FindShapeKeyIndex(
    const R3D_ShapeKeySet* set,
    const char* name)
{
    if (!set || !set->keys || !name) return -1;

    for (int i = 0; i < set->keyCount; i++)
    {
        if (strcmp(set->keys[i].name, name) == 0)
        {
            return i;
        }
    }

    return -1;
}

R3D_ShapeKeyInstance R3D_LoadShapeKeyInstance(
    const R3D_ShapeKeySet* set)
{
    R3D_ShapeKeyInstance result = {0};

    if (!set || !set->keys ||
        set->keyCount <= 0 ||
        !R3D_IsMeshDataValid(set->baseMesh))
    {
        return result;
    }

    result.meshData = R3D_CopyMeshData(set->baseMesh);

    if (!R3D_IsMeshDataValid(result.meshData))
    {
        return result;
    }

    result.weights = calloc(
        (size_t)set->keyCount,
        sizeof(*result.weights)
    );

    if (!result.weights)
    {
        R3D_UnloadMeshData(result.meshData);
        return (R3D_ShapeKeyInstance){0};
    }

    for (int i = 0; i < set->keyCount; i++)
    {
        result.weights[i] = set->keys[i].defaultWeight;
    }

    result.source = set;
    result.aabb = R3D_CalculateMeshDataBoundingBox(
        result.meshData
    );

    return result;
}

void R3D_UnloadShapeKeyInstance(
    R3D_ShapeKeyInstance instance)
{
    R3D_UnloadMeshData(instance.meshData);
    free(instance.weights);
}

float R3D_GetShapeKeyWeight(
    const R3D_ShapeKeyInstance* instance,
    int index)
{
    if (!instance || !instance->source ||
        !instance->weights ||
        index < 0 ||
        index >= instance->source->keyCount)
    {
        return 0.0f;
    }

    return instance->weights[index];
}

bool R3D_SetShapeKeyWeight(
    R3D_ShapeKeyInstance* instance,
    int index,
    float weight)
{
    if (!instance || !instance->source ||
        !instance->weights ||
        index < 0 ||
        index >= instance->source->keyCount ||
        !isfinite(weight))
    {
        return false;
    }

    // We don't artificially limit morph weights to [0, 1].
    // Negative and greater-than-one values are useful for
    // some procedural effects.
    instance->weights[index] = weight;

    return true;
}

bool R3D_ApplyShapeKeys(R3D_ShapeKeyInstance* instance)
{
    if (!instance || !instance->source ||
        !instance->weights)
    {
        return false;
    }

    const R3D_ShapeKeySet* source = instance->source;

    if (!source->keys ||
        !source->baseMesh.vertices ||
        !instance->meshData.vertices ||
        source->baseMesh.vertexCount <= 0 ||
        instance->meshData.vertexCount !=
            source->baseMesh.vertexCount)
    {
        return false;
    }

    const int vertexCount = source->baseMesh.vertexCount;

    // Validate all weights before touching the working mesh.
    for (int k = 0; k < source->keyCount; k++)
    {
        if (!isfinite(instance->weights[k]))
        {
            return false;
        }
    }

    // ========================================
    // ALWAYS START FROM IMMUTABLE BASE
    // ========================================

    memcpy(
        instance->meshData.vertices,
        source->baseMesh.vertices,
        (size_t)vertexCount * sizeof(R3D_Vertex)
    );

    bool hasActiveWeights = false;

    for (int k = 0; k < source->keyCount; k++)
    {
        if (instance->weights[k] != 0.0f)
        {
            hasActiveWeights = true;
            break;
        }
    }

    if (!hasActiveWeights)
    {
        instance->aabb = R3D_CalculateMeshDataBoundingBox(
            instance->meshData
        );

        return true;
    }

    // ========================================
    // EVALUATE EACH VERTEX
    // ========================================

    for (int v = 0; v < vertexCount; v++)
    {
        const R3D_Vertex* base =
            &source->baseMesh.vertices[v];

        R3D_Vertex* output =
            &instance->meshData.vertices[v];

        Vector3 position = base->position;

        Vector3 baseNormal = R3D_UnpackNormal(
            base->normal
        );

        Vector4 baseTangent = R3D_UnpackTangent(
            base->tangent
        );

        Vector3 normal = baseNormal;

        Vector3 tangent = {
            baseTangent.x,
            baseTangent.y,
            baseTangent.z
        };

        for (int k = 0; k < source->keyCount; k++)
        {
            const float weight = instance->weights[k];

            if (weight == 0.0f) continue;

            const R3D_ShapeKey* key = &source->keys[k];

            if (key->positionDeltas)
            {
                position = Vector3Add(
                    position,
                    Vector3Scale(
                        key->positionDeltas[v],
                        weight
                    )
                );
            }

            if (key->normalDeltas)
            {
                normal = Vector3Add(
                    normal,
                    Vector3Scale(
                        key->normalDeltas[v],
                        weight
                    )
                );
            }

            if (key->tangentDeltas)
            {
                tangent = Vector3Add(
                    tangent,
                    Vector3Scale(
                        key->tangentDeltas[v],
                        weight
                    )
                );
            }
        }

        if (!finite_vec3(position) ||
            !finite_vec3(normal) ||
            !finite_vec3(tangent))
        {
            return false;
        }

        // Normalize the final blended normal.
        normal = normalize_or(
            normal,
            normalize_or(
                baseNormal,
                (Vector3){0, 0, 1}
            )
        );

        // Gram-Schmidt: keep the tangent perpendicular
        // to the final normal.
        tangent = Vector3Subtract(
            tangent,
            Vector3Scale(
                normal,
                Vector3DotProduct(tangent, normal)
            )
        );

        Vector3 fallback = fabsf(normal.x) < 0.9f
            ? (Vector3){1, 0, 0}
            : (Vector3){0, 1, 0};

        fallback = normalize_or(
            Vector3Subtract(
                fallback,
                Vector3Scale(
                    normal,
                    Vector3DotProduct(fallback, normal)
                )
            ),
            (Vector3){0, 0, 1}
        );

        tangent = normalize_or(tangent, fallback);

        output->position = position;

        R3D_PackNormal(
            output->normal,
            normal
        );

        R3D_PackTangent(
            output->tangent,
            (Vector4){
                tangent.x,
                tangent.y,
                tangent.z,
                baseTangent.w
            }
        );
    }

    instance->aabb = R3D_CalculateMeshDataBoundingBox(
        instance->meshData
    );

    return true;
}