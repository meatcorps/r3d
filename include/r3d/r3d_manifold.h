#ifndef R3D_MANIFOLD_H
#define R3D_MANIFOLD_H

#include "./r3d_mesh.h"
#include <manifold/manifoldc.h>

#ifdef __cplusplus
extern "C" {
#endif

    // Returns a caller-owned Manifold MeshGL.
    // Use manifold_delete_meshgl() when finished.
    // Accepts indexed or sequential triangle meshes.
    R3DAPI ManifoldMeshGL* R3D_MeshDataToManifoldMeshGL(const R3D_MeshData* data);

    // Returns caller-owned CPU mesh data.
    // Use R3D_UnloadMeshData() when finished.
    // An invalid result contains zero-initialized fields.
    R3DAPI R3D_MeshData R3D_ManifoldMeshGLToMeshData(ManifoldMeshGL* mesh);

    typedef enum R3D_ManifoldStatus {
        R3D_MANIFOLD_OK,
        R3D_MANIFOLD_INVALID_INPUT,
        R3D_MANIFOLD_NATIVE_ERROR,
        R3D_MANIFOLD_EMPTY_RESULT,
        R3D_MANIFOLD_CONVERSION_FAILED,
        R3D_MANIFOLD_UPLOAD_FAILED,
        R3D_MANIFOLD_OUT_OF_MEMORY
    } R3D_ManifoldStatus;

    // The target R3D_Mesh owns the primary piece.
    // This result owns ONLY the additional meshes.
    typedef struct R3D_ManifoldResult {
        R3D_ManifoldStatus status;
        ManifoldError manifoldError;
        R3D_Mesh* extraMeshes;
        R3D_MeshData* extraData;
        int extraCount;
    } R3D_ManifoldResult;

    // Convert a native Manifold result and upload it into an existing R3D_Mesh.
    // A zero-initialized target is also supported.
    // The native Manifold remains owned by the caller.
    R3DAPI bool R3D_UpdateMeshFromManifold(R3D_Mesh* mesh, ManifoldManifold* solid);

    // One-shot CPU mesh Boolean operations.
    // The destination is updated; both CPU inputs remain unchanged.
    R3DAPI R3D_ManifoldResult R3D_ManifoldUnion(R3D_Mesh* mesh, const R3D_Mesh* other);
    R3DAPI R3D_ManifoldResult R3D_ManifoldSubtract(R3D_Mesh* mesh, const R3D_Mesh* other);
    R3DAPI R3D_ManifoldResult R3D_ManifoldIntersect(R3D_Mesh* mesh, const R3D_Mesh* other);

    // Extrude a simple XY outline in +Z. No holes or twist in this shortcut.
    // More advanced extrusions remain available through Manifold.
    R3DAPI R3D_ManifoldResult R3D_ManifoldExtrude(R3D_Mesh* mesh, const Vector2* outline, int pointCount, float height);

    // Split by a plane. The primary mesh contains the normal-facing side.
    // Other components, including the opposite side, are returned as extraMeshes.
    R3DAPI R3D_ManifoldResult R3D_ManifoldSlicePlane(R3D_Mesh* mesh, Vector3 normal, float offset);

    // Unloads additional GPU meshes and their array, but NOT the primary mesh.
    R3DAPI void R3D_UnloadManifoldResult(R3D_ManifoldResult* result);

#ifdef __cplusplus
}
#endif

#endif // R3D_MANIFOLD_H