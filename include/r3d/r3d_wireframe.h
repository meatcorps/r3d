
/* r3d_wireframe.h -- Presentation edges extracted from triangle topology. */
#ifndef R3D_WIREFRAME_H
#define R3D_WIREFRAME_H

#include "r3d_line.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Builds topology-aware wireframe triangles entirely on the CPU.
 *
 * No GPU upload or OpenGL context is required.
 * Can be scheduled on a worker thread with independent,
 * immutable source data.
 *
 * The caller owns the resulting mesh data and must release it
 * using R3D_UnloadMeshData().
 *
 * Supports both WORLD and SCREEN line-width modes.
 */
R3DAPI R3D_MeshData R3D_GenMeshDataTopologyWireframe(const R3D_MeshData* source, R3D_LineStyle style, float creaseAngleDegrees, float weldEpsilon, int* outEdgeCount);

/**
 * Builds one mesh containing triangle boundary edges and sharp creases.
 *
 * Duplicate positions are welded using weldEpsilon in model units.
 * Coplanar triangle diagonals are discarded.
 *
 * creaseAngleDegrees selects which connections are retained:
 * 0 degrees includes every unique triangle edge.
 * Greater than 0 keeps boundaries and sufficiently sharp creases.
 *
 * Source vertex colors become the endpoint colors.
 *
 * WORLD output: draw with R3D_DrawMesh / R3D_DrawMeshPro.
 * SCREEN output: draw with R3D_DrawScreenLineMesh.
 *
 * Requires CPU-side triangle mesh data. The topology of an
 * uploaded R3D_Mesh cannot be recovered through this API.
 *
 * outEdgeCount may be NULL.
 */
R3DAPI R3D_Mesh R3D_GenMeshTopologyWireframe(const R3D_MeshData* source, R3D_LineStyle style, float creaseAngleDegrees, float weldEpsilon, int* outEdgeCount);

/**
 * Regenerates the wireframe from current CPU triangle-mesh data and
 * updates an existing GPU mesh through R3D_UpdateMesh().
 *
 * Supports changed positions, vertex colors and triangle topology.
 * Existing GPU allocation is reused when capacity permits.
 *
 * The mesh must already exist and use R3D_PRIMITIVE_TRIANGLES.
 */
R3DAPI bool R3D_UpdateTopologyWireframe(R3D_Mesh* mesh, const R3D_MeshData* source, R3D_LineStyle style, float creaseAngleDegrees, float weldEpsilon, int* outEdgeCount);

#ifdef __cplusplus
}
#endif

#endif // R3D_WIREFRAME_H
