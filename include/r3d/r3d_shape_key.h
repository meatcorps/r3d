/* r3d_shape_key.h -- R3D Shape Keys / Morph Targets.
 *
 * Copyright (c) 2025-2026 Dennis Steffen
 */

#ifndef R3D_SHAPE_KEY_H
#define R3D_SHAPE_KEY_H

#include "./r3d_mesh_data.h"
#include "./r3d_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define R3D_SHAPE_KEY_NAME_LENGTH 64

/**
 * One immutable morph target.
 *
 * Deltas have one entry per base-mesh vertex.
 * NULL normal/tangent arrays mean that attribute is unchanged.
 */
typedef struct R3D_ShapeKey {
    char name[R3D_SHAPE_KEY_NAME_LENGTH];

    Vector3* positionDeltas;
    Vector3* normalDeltas;
    Vector3* tangentDeltas;

    float defaultWeight;
} R3D_ShapeKey;

/**
 * Shared, immutable imported data for one mesh.
 *
 * Owned by R3D_Model when imported from a model.
 */
typedef struct R3D_ShapeKeySet {
    R3D_MeshData baseMesh;

    R3D_ShapeKey* keys;
    int keyCount;
} R3D_ShapeKeySet;

/**
 * Mutable CPU state for one character/mesh instance.
 *
 * The source ShapeKeySet must outlive this instance.
 * This structure does not own GPU resources.
 */
typedef struct R3D_ShapeKeyInstance {
    const R3D_ShapeKeySet* source;

    R3D_MeshData meshData;
    float* weights;

    BoundingBox aabb;
} R3D_ShapeKeyInstance;

/**
 * Releases owned imported shape-key data.
 * Normally called automatically by R3D_UnloadModel.
 */
R3DAPI void R3D_UnloadShapeKeySet(R3D_ShapeKeySet set);

/**
 * Query immutable shape-key definitions.
 */
R3DAPI int R3D_GetShapeKeyCount(
    const R3D_ShapeKeySet* set
);

R3DAPI const char* R3D_GetShapeKeyName(
    const R3D_ShapeKeySet* set,
    int index
);

R3DAPI int R3D_FindShapeKeyIndex(
    const R3D_ShapeKeySet* set,
    const char* name
);

/**
 * Creates independent weights and a mutable CPU mesh.
 * Initializes weights from imported default values.
 */
R3DAPI R3D_ShapeKeyInstance R3D_LoadShapeKeyInstance(
    const R3D_ShapeKeySet* set
);

R3DAPI void R3D_UnloadShapeKeyInstance(
    R3D_ShapeKeyInstance instance
);

R3DAPI float R3D_GetShapeKeyWeight(
    const R3D_ShapeKeyInstance* instance,
    int index
);

R3DAPI bool R3D_SetShapeKeyWeight(
    R3D_ShapeKeyInstance* instance,
    int index,
    float weight
);

/**
 * Applies all weights simultaneously to the CPU mesh.
 *
 * Every call starts from immutable base vertices.
 * No GPU upload occurs.
 *
 * Recomputes instance->aabb.
 */
R3DAPI bool R3D_ApplyShapeKeys(
    R3D_ShapeKeyInstance* instance
);

#ifdef __cplusplus
}
#endif

#endif // R3D_SHAPE_KEY_H