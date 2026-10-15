
#ifndef R3D_GEOMETRY_DEFORMATION_H
#define R3D_GEOMETRY_DEFORMATION_H

#include "./r3d_render_mode_builder.h"
#include "./r3d_mesh_data.h"
#include "./r3d_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * All geometric parameters use model-local coordinates.
 * Angles are radians.
 * Animated effects use Ray-Shade's built-in TIME.
 */

/* Signed normal displacement.
 * axisInfluence components are clamped to 0..1.
 * planar uses radial XY displacement instead of vertex normals,
 * useful as a simple approximation for flat text shapes.
 */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildInflate(float strength, Vector3 axisInfluence, bool planar, bool continuous);

/* Local normal displacement with radius and falloff exponent. */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildBulge(Vector3 center, float radius, float strength, float falloff, bool continuous);

/* Traveling sine wave. */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildWave(Vector3 axis, Vector3 direction, float amplitude, float frequency, float speed);

/* Progressive rotation between positions start and end along axis. */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildTwist(Vector3 axis, float angle, float start, float end);

/* Curvature in radians per model unit. */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildBend(Vector3 axis, Vector3 direction, float curvature);

/* Radially traveling sine wave. */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildRipple(Vector3 center, float amplitude, float frequency, float speed, float decay, bool continuous);

/* Smooth procedural displacement. */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildNoiseDisplacement(Vector3 scale, float strength, float speed, bool continuous);

/* Discrete randomized vertex motion. */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildVertexJitter(float strength, float rate, float seed, bool continuous);

/* Quantization of selected position axes. */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildVertexSnap(float step, Vector3 axisInfluence);

/* Independent triangle translation.
 * Requires R3D_PrepareMeshDataForExplosion().
 */
R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildExplode(float amount, float seed);

/*
 * Make an indexed or non-indexed triangle mesh non-indexed,
 * duplicating vertices while preserving their complete attributes.
 *
 * Caller owns the returned R3D_MeshData.
 */
R3DAPI R3D_MeshData R3D_PrepareMeshDataForExplosion(R3D_MeshData source);

/*
 * Expand conservative culling bounds.
 * Call on the rendering mesh/model handle, not on shared resources
 * that should retain their original bounds.
 */
R3DAPI void R3D_ExpandMeshBoundsForDeformation(R3D_Mesh* mesh, float margin);

R3DAPI void R3D_ExpandModelBoundsForDeformation(R3D_Model* model, float margin);

#ifdef __cplusplus
}
#endif

#endif
