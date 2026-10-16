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
 * One colored point in a line path.
 */
typedef struct R3D_LinePoint {
    Vector3 position;
    Color color;
    R3D_LineArc arcToNext;
} R3D_LinePoint;

/**
 * Ordered sequence of line points.
 *
 * The point array remains owned by the caller.
 * For closed paths, the last point connects to the first.
 */
typedef struct R3D_LinePath {
    const R3D_LinePoint* points;
    int count;
    bool closed;
} R3D_LinePath;

/**
 * Geometric properties of a line path.
 *
 * SCREEN mode keeps native thin lines until Phase 3.
 * WORLD mode generates a triangle ribbon.
 */
typedef struct R3D_LineStyle {
    float width;                    ///< Width in world units for WORLD mode.
    R3D_LineWidthMode widthMode;    ///< WORLD or SCREEN.
    Vector3 normal;                 ///< Reference normal for WORLD ribbons. Zero selects automatic orientation.
    float miterLimit;               ///< Maximum miter length relative to half-width. Zero defaults to 4.
} R3D_LineStyle;


/**
 * One projected point, including its original path metadata.
 */
typedef struct R3D_ProjectedLinePoint {
    Vector2 position;       ///< Target pixel coordinates, top-left origin.
    Color color;            ///< Interpolated RGBA color.
    float depth;            ///< Projected depth in the [0, 1] range.
    float segmentT;         ///< Position within the original segment [0, 1].
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
    R3D_ProjectedLineSegment* segments;
    int count;
    bool closed;
} R3D_ProjectedLinePath;

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Generates CPU-side line geometry.
     *
     * The caller owns the returned mesh data and must release it
     * using R3D_UnloadMeshData().
     */
    R3DAPI R3D_MeshData R3D_GenMeshDataLinePath(const R3D_LinePath* path, R3D_LineStyle style);

    /**
     * Generates CPU-side line geometry with configurable end caps.
     *
     * Rounded caps are supported for WORLD mode.
     * SCREEN thin-line mode ignores the cap flags.
     * Closed paths have no endpoint caps.
     */
    R3DAPI R3D_MeshData R3D_GenMeshDataLinePathEx(
        const R3D_LinePath* path,
        R3D_LineStyle style,
        R3D_LineCapFlags caps
    );

    /**
     * Generates an R3D mesh from a colored line path.
     *
     * The returned mesh uses LINE_STRIP or LINE_LOOP primitives.
     * The caller must release it using R3D_UnloadMesh().
     */
    R3DAPI R3D_Mesh R3D_GenMeshLinePath(const R3D_LinePath* path, R3D_LineStyle style);

    /**
     * Generates a line mesh with configurable endpoint caps.
     *
     * In WORLD mode, rounded caps are real triangle geometry.
     * Existing R3D_GenMeshLinePath() keeps flat endpoints.
     */
    R3DAPI R3D_Mesh R3D_GenMeshLinePathEx(
        const R3D_LinePath* path,
        R3D_LineStyle style,
        R3D_LineCapFlags caps
    );

    /**
     * Projects a 3D line path into 2D pixel coordinates.
     *
     * Arcs are tessellated before projection.
     * Segments crossing the near/far planes are clipped.
     * The result preserves source segment and arc metadata.
     *
     * Does not require an active R3D drawing session.
     */
    R3DAPI R3D_ProjectedLinePath R3D_ProjectLinePath2D(const R3D_LinePath* path, R3D_View view);

    /**
     * Releases the projected CPU-side segments.
     */
    R3DAPI void R3D_UnloadProjectedLinePath(R3D_ProjectedLinePath path);

    /**
     * Draws an already projected 2D line path using Raylib drawing functions.
     *
     * Uses DrawLineEx() for thick segments and DrawCircleV()
     * for rounded joins and endpoints.
     *
     * Vertex colors are interpolated along each segment.
     * Width is specified in target pixels.
     *
     * Call during Raylib 2D drawing, normally after R3D_End().
     *
     * This is an overlay renderer and does not use the 3D depth buffer.
     * The input path remains owned by the caller.
     */
    R3DAPI void R3D_Draw2DProjectedLinePath(const R3D_ProjectedLinePath* path, float width);

    /**
     * Draws projected 2D lines with configurable rounded endpoints.
     *
     * Internal joins remain rounded.
     * Cap flags are ignored for closed paths.
     */
    R3DAPI void R3D_Draw2DProjectedLinePathEx(const R3D_ProjectedLinePath* path, float width, R3D_LineCapFlags caps);

    /**
     * Generates a persistent GPU-expandable screen-space line mesh.
     *
     * The resulting triangles require R3D_DrawScreenLineMesh().
     */
    R3DAPI R3D_Mesh R3D_GenMeshScreenLinePath(const R3D_LinePath* path);

    /**
     * Generates a screen-space line mesh with configurable rounded endpoints.
     *
     * Endpoint caps are baked into the generated mesh.
     * Internal joins remain rounded regardless of the cap flags.
     * Cap flags are ignored for closed paths.
     */
    R3DAPI R3D_Mesh R3D_GenMeshScreenLinePathEx(const R3D_LinePath* path, R3D_LineCapFlags caps);

    /**
     * Loads the reusable screen-space line vertex effect.
     * Create once and share across screen-line meshes.
     */
    R3DAPI R3D_SurfaceEffect* R3D_LoadScreenLineEffect(void);

    /**
     * Draws a screen-space line inside an R3D session.
     *
     * Width is measured in internal render-target pixels.
     * The transform is applied to the original 3D path.
     * Material is rendered unlit, alpha-blended and double-sided.
     */
    R3DAPI void R3D_DrawScreenLineMesh(R3D_Mesh mesh, R3D_Material material, R3D_SurfaceEffect* effect, R3D_LineStyle style, Matrix transform);

#ifdef __cplusplus
}
#endif

#endif // R3D_LINE_H
