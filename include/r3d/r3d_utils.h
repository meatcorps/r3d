/* r3d_utils.h -- R3D Utility Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#ifndef R3D_UTILS_H
#define R3D_UTILS_H

#include "./r3d_platform.h"
#include <raylib.h>
#include <stdint.h>

/**
 * @defgroup Utils Utility Functions
 * @brief Defines some utility functions, including access to internal data, useful for adding additional effects.
 * @{
 */

/**
 * Rendering statistics for the current or most recently completed R3D frame.
 *
 * Draw and triangle counts include all R3D rendering passes:
 * shadows, probes, scene rendering and internal fullscreen passes.
 * Instanced geometry contributes its full expanded triangle count.
 *
 * These are CPU-side counters, not GPU timing or hardware pipeline queries.
 */
typedef struct R3D_RenderStats {
 uint64_t submittedCalls;       ///< Logical mesh/decal calls queued by the application
 uint64_t drawCalls;            ///< Actual OpenGL draw calls issued by R3D
 uint64_t meshDrawCalls;        ///< Draw calls for meshes, including repeated rendering passes
 uint64_t decalDrawCalls;       ///< Draw calls for decals
 uint64_t internalDrawCalls;    ///< Internal shapes and fullscreen rendering
 uint64_t instancedDrawCalls;   ///< Draw calls issued using instanced rendering
 uint64_t triangles;            ///< Total triangle work, including instancing and repeated passes
 uint64_t vertexReferences;     ///< Submitted vertex/index references, not unique vertices
 uint64_t meshInstances;        ///< Mesh instances rendered across passes, counting ordinary draws as one
} R3D_RenderStats;

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------
// UTILS: Rendering Statistics
// ----------------------------------------

/**
 * Returns the render statistics.
 *
 * Statistics reset at R3D_Begin() and accumulate through R3D_End().
 * Call after R3D_End() for complete results.
 */
R3DAPI R3D_RenderStats R3D_GetRenderStats(void);

/**
 * Draws a simple statistics overlay using raylib's 2D functions.
 *
 * Call between BeginDrawing()/EndDrawing(), after R3D_End().
 * The overlay itself is not included in R3D's rendering statistics.
 */
R3DAPI void R3D_DrawRenderStats(int x, int y);

// ----------------------------------------
// UTILS: Bounding Box Functions
// ----------------------------------------

/**
 * @brief Checks that an AABB contains finite coordinates
 * and has correctly ordered minimum and maximum values.
 */
R3DAPI bool R3D_IsBoundingBoxValid(BoundingBox bounds);

/**
 * @brief Expands a local-space AABB on both sides of each axis.
 *
 * Padding must be finite and non-negative.
 * The output remains unchanged if validation fails.
 *
 * @return True if the expanded bounds were calculated.
 */
R3DAPI bool R3D_ExpandBoundingBox(BoundingBox bounds, Vector3 padding, BoundingBox* result);

/**
 * @brief Validates a finite, correctly ordered bounding box.
 */
R3DAPI bool R3D_IsBoundingBoxValid(BoundingBox bounds);

/**
 * @brief Expands bounds by non-negative padding on each axis.
 *
 * The output is unchanged on failure.
 */
R3DAPI bool R3D_ExpandBoundingBox(BoundingBox bounds, Vector3 padding, BoundingBox* result);

// ----------------------------------------
// UTILS: Default Texture Retrieval Functions
// ----------------------------------------

/**
 * @brief Retrieves a default white texture.
 *
 * This texture is fully white (1,1,1,1), useful for default material properties.
 *
 * @return A white texture.
 */
R3DAPI Texture2D R3D_GetWhiteTexture(void);

/**
 * @brief Retrieves a default black texture.
 *
 * This texture is fully black (0,0,0,1), useful for masking or default values.
 *
 * @return A black texture.
 */
R3DAPI Texture2D R3D_GetBlackTexture(void);

/**
 * @brief Retrieves a default normal map texture.
 *
 * This texture represents a neutral normal map (0.5, 0.5, 1.0), which applies no normal variation.
 *
 * @return A neutral normal texture.
 */
R3DAPI Texture2D R3D_GetNormalTexture(void);

// ----------------------------------------
// UTILS: Render Texture Retrieval Functions
// ----------------------------------------

/**
 * @brief Retrieves the buffer containing the scene's normal data.
 *
 * This texture stores octahedral-compressed normals using two 16-bit per-channel RG components.
 *
 * @note You can find the decoding functions in 'shaders/include/math.glsl'.
 *
 * @return The normal buffer texture.
 */
R3DAPI Texture2D R3D_GetBufferNormal(void);

/**
 * @brief Retrieves the final depth buffer.
 *
 * This texture is an R16 texture containing a linear depth value
 * normalized between the near and far clipping planes.
 *
 * The texture is intended for post-processing effects outside of R3D
 * that require access to linear depth information.
 *
 * @return The final depth buffer texture (R16, linear depth).
 */
R3DAPI Texture2D R3D_GetBufferDepth(void);

// ----------------------------------------
// UTILS: Camera Matrices Retrieval Functions
// ----------------------------------------

/**
 * @brief Retrieves the view matrix.
 *
 * This matrix represents the camera's transformation from world space to view space.
 * It is updated at the last call to 'R3D_Begin'.
 *
 * @return The current view matrix.
 */
R3DAPI Matrix R3D_GetMatrixView(void);

/**
 * @brief Retrieves the inverse view matrix.
 *
 * This matrix transforms coordinates from view space back to world space.
 * It is updated at the last call to 'R3D_Begin'.
 *
 * @return The current inverse view matrix.
 */
R3DAPI Matrix R3D_GetMatrixInvView(void);

/**
 * @brief Retrieves the projection matrix.
 *
 * This matrix defines the transformation from view space to clip space.
 * It is updated at the last call to 'R3D_Begin'.
 *
 * @return The current projection matrix.
 */
R3DAPI Matrix R3D_GetMatrixProjection(void);

/**
 * @brief Retrieves the inverse projection matrix.
 *
 * This matrix transforms coordinates from clip space back to view space.
 * It is updated at the last call to 'R3D_Begin'.
 *
 * @return The current inverse projection matrix.
 */
R3DAPI Matrix R3D_GetMatrixInvProjection(void);

/**
 * @brief Retrieves the view-projection matrix.
 *
 * This matrix represents the transformation from world space to clip space.
 * It is updated at the last call to 'R3D_Begin'.
 *
 * @return The current view-projection matrix.
 */
R3DAPI Matrix R3D_GetMatrixViewProjection(void);

/**
 * @brief Compares two matrices for equality within a small tolerance.
 *
 * This function checks if the two matrices are equal within a tolerance of 0.00001f.
 *
 * @param a The first matrix to compare.
 * @param b The second matrix to compare.
 * @return True if the matrices are equal within the tolerance, false otherwise.
 */
R3DAPI bool R3D_MatrixEquals(Matrix a, Matrix b);

#ifdef __cplusplus
} // extern "C"
#endif

/** @} */ // end of Utils

#endif // R3D_UTILS_H
