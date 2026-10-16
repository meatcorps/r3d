#ifndef R3D_MANIFOLD_H
#define R3D_MANIFOLD_H

#include "./r3d_mesh_data.h"
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

#ifdef __cplusplus
}
#endif

#endif // R3D_MANIFOLD_H