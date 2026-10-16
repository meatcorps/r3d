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
     * Generates an R3D mesh from a colored line path.
     *
     * The returned mesh uses LINE_STRIP or LINE_LOOP primitives.
     * The caller must release it using R3D_UnloadMesh().
     */
    R3DAPI R3D_Mesh R3D_GenMeshLinePath(const R3D_LinePath* path, R3D_LineStyle style);

#ifdef __cplusplus
}
#endif

#endif // R3D_LINE_H
