/* r3d_line.h -- R3D Line Module.
*
 * Copyright (c) 2025-2026 Dennis Steffen
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#ifndef R3D_LINE_H
#define R3D_LINE_H

#include "./r3d_mesh.h"
#include "./r3d_draw.h"
#include "./r3d_surface_effect.h"

/**
 * Defines how line width is interpreted.
 *
 * Width modes become functional in later phases.
 */
typedef enum R3D_LineWidthMode {
    R3D_LINE_WIDTH_SCREEN,
    R3D_LINE_WIDTH_WORLD
} R3D_LineWidthMode;

/**
 * Rounded endpoint configuration.
 *
 * Only affects open paths. Closed paths have no endpoints.
 */
typedef enum R3D_LineCapFlags {
    R3D_LINE_CAP_NONE = 0,
    R3D_LINE_CAP_START = 1 << 0,
    R3D_LINE_CAP_END = 1 << 1,
    R3D_LINE_CAP_BOTH = R3D_LINE_CAP_START | R3D_LINE_CAP_END
} R3D_LineCapFlags;

/**
 * Curvature of the segment leading to the next point.
 *
 * sweepAngle is expressed in radians.
 * Zero produces a straight segment.
 * subdivisions <= 0 uses automatic subdivision.
 */
typedef struct R3D_LineArc {
    float sweepAngle;
    Vector3 normal;
    int subdivisions;
} R3D_LineArc;

/**
 * Defines the appearance of a complete line path.
 *
 * Width is the default thickness for connections whose
 * widthToNext is zero.
 *
 * WORLD widths are measured in world units.
 * SCREEN widths are measured in pixels.
 */
typedef struct R3D_LineStyle {
    float width;
    R3D_LineWidthMode widthMode;
    Vector3 normal;
    float miterLimit;
    R3D_LineCapFlags caps;
} R3D_LineStyle;

/**
 * One colored point in a line path.
 *
 * widthToNext controls the connection from this point to
 * the next point. Zero inherits LinePath.style.width.
 *
 * The final point's widthToNext is ignored for open paths.
 */
typedef struct R3D_LinePoint {
    Vector3 position;
    Color color;
    R3D_LineArc arcToNext;
    float width;
} R3D_LinePoint;

/**
 * Complete line path configuration.
 *
 * The caller owns the points array.
 * Changes to the path require an explicit mesh update.
 */
typedef struct R3D_LinePath {
    const R3D_LinePoint *points;
    int count;
    bool closed;
    R3D_LineStyle style;
} R3D_LinePath;

/**
 * One projected point, including its original path metadata.
 */
typedef struct R3D_ProjectedLinePoint {
    Vector2 position; ///< Target pixel coordinates, top-left origin.
    Color color; ///< Interpolated RGBA color.
    float depth; ///< Projected depth in the [0, 1] range.
    float segmentT; ///< Position within the original segment [0, 1].
    float width; ///< Interpolated width in target pixels.
} R3D_ProjectedLinePoint;

/**
 * A projected and near/far-clipped line segment.
 *
 * Each segment is independent, so gaps caused by clipping
 * are represented correctly.
 */
typedef struct R3D_ProjectedLineSegment {
    R3D_ProjectedLinePoint start;
    R3D_ProjectedLinePoint end;
    R3D_LineArc sourceArc;
    int sourceSegment;
} R3D_ProjectedLineSegment;

/**
 * CPU-side projected line path.
 *
 * Memory must be released using R3D_UnloadProjectedLinePath().
 */
typedef struct R3D_ProjectedLinePath {
    R3D_ProjectedLineSegment *segments;
    int count;
    bool closed;
    R3D_LineCapFlags caps;
    int sourceSegmentCount;
} R3D_ProjectedLinePath;

#ifdef __cplusplus
extern "C" {


#endif

/**
 * Generates CPU-side line geometry using path.style.
 *
 * WORLD mode produces a triangle ribbon.
 * SCREEN mode produces a native thin line strip/loop.
 *
 * The caller owns the returned mesh data.
 */
R3DAPI R3D_MeshData R3D_GenMeshDataLinePath(const R3D_LinePath *path);

/**
 * Generates a mesh using path.style.
 *
 * WORLD mode bakes connection widths into the geometry.
 * It does not support draw-time width scaling.
 */
R3DAPI R3D_Mesh R3D_GenMeshLinePath(const R3D_LinePath *path);

/**
 * Regenerates geometry and uploads it into an existing mesh.
 *
 * Supports changed points, colors, widths, arcs and point counts.
 * Uses R3D_UpdateMesh() to reuse existing GPU storage where possible.
 *
 * The mesh must already have been created.
 */
R3DAPI bool R3D_UpdateLineMesh(R3D_Mesh *mesh, const R3D_LinePath *path);

/**
 * Populates four points for an XY-plane rectangle.
 * The path should be closed.
 * Existing point colors and widths are preserved.
 */
R3DAPI void R3D_SetLineRectangle(R3D_LinePoint points[4], Vector3 center, Vector2 size);

/**
 * Populates four quarter-circle arcs in the XY plane.
 * The path should be closed and have four points.
 * Existing point colors and widths are preserved.
 */
R3DAPI void R3D_SetLineCircle(R3D_LinePoint points[4], Vector3 center, float radius);

/**
 * Populates a sampled XY-plane ellipse.
 * The path should be closed and count must be at least three.
 * Existing point colors and widths are preserved.
 */
R3DAPI void R3D_SetLineEllipse(R3D_LinePoint *points, int count, Vector3 center, Vector2 radii);

/**
 * Populates two endpoints and the arc connecting them.
 * The path should be open.
 * sweep is expressed in radians.
 * Existing point colors and widths are preserved.
 */
R3DAPI void R3D_SetLineArc(R3D_LinePoint points[2], Vector3 start, Vector3 end, Vector3 normal, float sweep, int subdivisions);

/**
 * Projects a 3D line path into 2D pixel coordinates.
 *
 * Arcs are tessellated before projection.
 * Segments crossing the near/far planes are clipped.
 * The result preserves source segment and arc metadata.
 *
 * Does not require an active R3D drawing session.
 */
R3DAPI R3D_ProjectedLinePath R3D_ProjectLinePath2D(const R3D_LinePath *path, R3D_View view);

/**
 * Releases the projected CPU-side segments.
 */
R3DAPI void R3D_UnloadProjectedLinePath(R3D_ProjectedLinePath path);

/**
 * Draws a projected path using ordinary Raylib 2D drawing functions.
 *
 * Uses the interpolated point widths and cap configuration stored
 * during R3D_ProjectLinePath2D().
 *
 * widthScale is a multiplier: 1.0f uses the configured pixel widths.
 *
 * Draw after R3D_End(). This is an overlay without 3D depth testing.
 */
R3DAPI void R3D_Draw2DProjectedLinePath(const R3D_ProjectedLinePath *path, float widthScale);

/**
 * Generates CPU-side triangles for GPU-expanded screen-space lines.
 * The caller must release the result using R3D_UnloadMeshData().
 */
R3DAPI R3D_MeshData R3D_GenMeshDataScreenLinePath(const R3D_LinePath* path);

/**
 * Generates a persistent GPU-expandable screen-space line mesh.
 *
 * The resulting triangles require R3D_DrawScreenLineMesh().
 */
R3DAPI R3D_Mesh R3D_GenMeshScreenLinePath(const R3D_LinePath *path);

/**
 * Rebuilds an existing screen-space line mesh from the path.
 * Reuses GPU storage through R3D_UpdateMesh() where possible.
 * The new caps, point widths, colors and geometry are baked in.
 */
R3DAPI bool R3D_UpdateScreenLineMesh(R3D_Mesh *mesh, const R3D_LinePath *path);

/**
 * Loads the reusable screen-space line vertex effect.
 * Create once and share across screen-line meshes.
 */
R3DAPI R3D_SurfaceEffect *R3D_LoadScreenLineEffect(void);

/**
 * Returns the recommended default material for GPU screen-space lines.
 *
 * Uses unlit, double-sided, opaque rendering with depth testing.
 * Callers can modify transparency, blending, emission and other
 * properties without the line renderer overriding them.
 */
R3DAPI R3D_Material R3D_GetDefaultScreenLineMaterial(void);

/**
* Draws a GPU-expanded screen-space line inside an R3D session.
*
* Point widths are stored in the mesh in internal render-target pixels.
* widthScale multiplies those widths without rebuilding the mesh.
* Use 1.0f for the configured widths.
*
* Unlike WORLD ribbons, SCREEN lines support draw-time width scaling.
* The transform changes the path's world-space placement.
*
* The material is rendered unlit and participates in depth testing.
*/
R3DAPI void R3D_DrawScreenLineMesh(R3D_Mesh mesh, R3D_Material material, R3D_SurfaceEffect *effect, float widthScale,
                                   Matrix transform);

#ifdef __cplusplus
}
#endif

#endif // R3D_LINE_H
