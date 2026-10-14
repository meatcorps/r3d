/* r3d_draw.h -- R3D Draw Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#ifndef R3D_DRAW_H
#define R3D_DRAW_H

#include "./r3d_animation_player.h"
#include "./r3d_instance.h"
#include "./r3d_platform.h"
#include "./r3d_lighting.h"
#include "./r3d_camera.h"
#include "./r3d_model.h"
#include "./r3d_decal.h"
#include "./r3d_probe.h"
#include <raylib.h>

/**
 * @defgroup Draw
 * @{
 */

// ========================================
// STRUCT TYPES
// ========================================

/**
 * @brief Describes an R3D rendering view.
 *
 * A view defines the camera and output area used for a single rendering
 * session. It combines an R3D camera, an optional render target and an optional
 * viewport inside that target.
 *
 * If `target` is zero-initialized, rendering is directed to the default
 * framebuffer. If `viewport.width` or `viewport.height` is less than or equal
 * to zero, the full target size is used.
 */
typedef struct R3D_View {
    R3D_Camera camera;      ///< Camera used for this view.
    RenderTexture target;   ///< Render target. Zero-initialized means screen/backbuffer.
    Rectangle viewport;     ///< Viewport inside the target. If width or height <= 0, the full target is used.
} R3D_View;

/**
 * @brief A world-space point projected into output-target coordinates.
 *
 * Position is in target pixels with a top-left origin.
 * Depth is normalized to [0, 1] for points between near and far planes.
 * Values outside that range are retained for clipping decisions.
 *
 * insideViewport checks the 2D presentation rectangle only.
 * It does not imply that the point is inside the camera's depth range.
 */
typedef struct R3D_ScreenProjection {
    Vector2 position;
    float depth;
    bool inFront;
    bool insideViewport;
} R3D_ScreenProjection;

// ========================================
// PUBLIC API
// ========================================

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Projects a world position into the view's output target.
 *
 * Uses the same camera and resolved presentation rectangle as R3D rendering.
 * The returned position includes custom viewport offsets.
 *
 * Does not require an active rendering session.
 * Returns false for invalid input or an undefined projection.
 */
R3DAPI bool R3D_WorldToScreen(Vector3 worldPosition, R3D_View view, R3D_ScreenProjection* result);

/**
 * @brief Creates a world-space ray from an output-target pixel position.
 *
 * Returns false if the position is outside the resolved presentation
 * rectangle, including letterbox bars.
 *
 * Perspective rays start at the camera position.
 * Orthographic rays start on the camera's near plane.
 *
 * Does not require an active rendering session.
 */
R3DAPI bool R3D_ScreenToWorldRay(Vector2 screenPosition, R3D_View view, Ray* result);

/**
 * @brief Convenience function for picking a world-space AABB.
 *
 * Uses R3D_ScreenToWorldRay() followed by R3D_RaycastBoundingBox().
 * The bounding box must already be expressed in world coordinates.
 *
 * Returns true on a collision, false otherwise.
 */
R3DAPI bool R3D_PickBoundingBox(Vector2 screenPosition, R3D_View view, BoundingBox bounds, RayCollision* result);

/**
 * @brief Projects a local-space bounding box to an output-target rectangle.
 *
 * Transforms the eight corners and clips box edges against the near plane.
 * Coordinates are target pixels with a top-left origin.
 *
 * Does not clamp the rectangle to the viewport.
 * Returns false when no projectable portion remains.
 */
R3DAPI bool R3D_BoundingBoxToScreenRect(BoundingBox bounds, Matrix transform, R3D_View view, Rectangle* result);

/**
 * @brief Projects a mesh's local bounds into a target-space rectangle.
 */
R3DAPI bool R3D_MeshToScreenRect(R3D_Mesh mesh, Matrix transform, R3D_View view, Rectangle* result);

/**
 * @brief Projects a model's bounds into a target-space rectangle.
 *
 * Uses the model's animated bounds override when one is configured.
 */
R3DAPI bool R3D_ModelToScreenRect(R3D_Model model, Matrix transform, R3D_View view, Rectangle* result);

/**
 * @brief Begins a rendering session using the given raylib camera.
 *
 * Rendering output is directed to the default framebuffer.
 *
 * The given Camera3D is converted internally to an R3D_Camera. Since raylib
 * cameras do not store near/far clipping planes, the converted camera uses the
 * current rlgl culling distances for those values.
 *
 * @param camera Camera used to render the scene.
 */
R3DAPI void R3D_Begin(Camera3D camera);

/**
 * @brief Begins a rendering session using an R3D camera.
 *
 * Rendering output is directed to the default framebuffer.
 *
 * This entry point provides access to R3D-specific camera features such as
 * layer masks, custom near/far clipping planes and quaternion-based orientation.
 *
 * @param camera Camera used to render the scene.
 */
R3DAPI void R3D_BeginEx(R3D_Camera camera);

/**
 * @brief Begins a rendering session using a complete R3D view descriptor.
 *
 * This is the advanced entry point. It allows the caller to specify the camera,
 * render target and viewport used for the rendering session.
 *
 * The view camera is used as-is, including its near/far clipping planes and
 * layer mask. If the camera was created from a raylib Camera3D using
 * `R3D_CameraFromRL()`, its near/far planes come from the current rlgl culling
 * distances because raylib cameras do not store those values directly.
 *
 * Use this function for render-to-texture workflows, custom viewports,
 * multipass rendering, editor views, minimaps, probes or any case where the
 * default framebuffer is not enough.
 *
 * @param view View descriptor used to render the scene.
 */
R3DAPI void R3D_BeginPro(R3D_View view);

/**
 * @brief Ends the current rendering session.
 *
 * This function is the one that actually performs the full
 * rendering of the described scene. It carries out culling,
 * sorting, shadow rendering, scene rendering, and screen /
 * post-processing effects.
 */
R3DAPI void R3D_End(void);

/**
 * @brief Begins a clustered draw pass.
 *
 * All draw calls submitted in this pass are first tested against the
 * cluster AABB. If the cluster fails the scene/shadow frustum test,
 * none of the contained objects are tested or drawn.
 *
 * @param aabb Bounding box used as the cluster-level frustum test.
 */
R3DAPI void R3D_BeginCluster(BoundingBox aabb);

/**
 * @brief Ends the current clustered draw pass.
 *
 * Stops submitting draw calls to the active cluster.
 */
R3DAPI void R3D_EndCluster(void);

/**
 * @brief Queues a light to be rendered for the current frame, without shadows.
 *
 * The light is submitted for rendering during R3D_End(), and must be pushed
 * again on every frame between R3D_Begin() and R3D_End() to remain visible.
 *
 * @param light The light data to submit (see R3D_Light).
 */
R3DAPI void R3D_PushLight(R3D_Light light);

/**
 * @brief Queues a light to be rendered for the current frame, with an associated shadow map.
 *
 * The light is submitted for rendering during R3D_End(), and must be pushed
 * again on every frame between R3D_Begin() and R3D_End() to remain visible.
 *
 * The shadow map is re-rendered whenever updateShadow is true, or when it has never
 * been rendered before (so static lights can safely pass false from the start and
 * still get valid shadows). This happens even if the light is currently off-screen,
 * so you can safely ignore visibility when driving updateShadow, or factor it in
 * yourself if you want to defer updates for off-screen lights.
 *
 * @param light The light data to submit (see R3D_Light).
 * @param map The shadow map to associate with this light for this frame.
 * @param updateShadow Whether the shadow map should be re-rendered this frame.
 *                     If false, the previously rendered content is reused instead.
 */
R3DAPI void R3D_PushLightEx(R3D_Light light, R3D_ShadowMap map, bool updateShadow);

/**
 * @brief Queues a probe to be rendered for the current frame.
 *
 * The probe is submitted for rendering during R3D_End(), and must be pushed
 * again on every frame between R3D_Begin() and R3D_End() to remain active.
 *
 * The probe is re-captured whenever updateProbe is true, or when it has never been
 * captured before (so static probes can safely pass false from the start and still
 * get valid data). This happens even if the probe is currently off-screen, so you
 * can safely ignore visibility when driving updateProbe, or factor it in yourself
 * if you want to defer updates for off-screen probes.
 *
 * @param probe The probe data to submit (see R3D_Probe).
 * @param updateProbe Whether the probe's capture should be re-rendered this frame.
 *                    If false, the previously captured content is reused instead.
 */
R3DAPI void R3D_PushProbe(R3D_Probe probe, bool updateProbe);

/**
 * @brief Queues a mesh draw command with position and uniform scale.
 * 
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawMesh(R3D_Mesh mesh, R3D_Material material, Vector3 position, float scale);

/**
 * @brief Queues a mesh draw command with position, rotation and non-uniform scale.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawMeshEx(R3D_Mesh mesh, R3D_Material material, Vector3 position, Quaternion rotation, Vector3 scale);

/**
 * @brief Queues a mesh draw command using a full transform matrix.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawMeshPro(R3D_Mesh mesh, R3D_Material material, Matrix transform);

/**
 * Queues a mesh draw with GPU shape-key deformation.
 *
 * Shares the original mesh and immutable morph data.
 * Captures the instance's current weights at submission.
 *
 * Does not call CPU ApplyShapeKeys().
 *
 * Returns false if the input is invalid or GPU resources
 * cannot support the requested draw.
 */
R3DAPI bool R3D_DrawShapeKeyMeshPro(R3D_Mesh mesh, R3D_Material material, const R3D_ShapeKeyGpu* gpu, const R3D_ShapeKeyInstance* instance, Matrix transform);

/**
 * @brief Queues an instanced mesh draw command.
 *
 * Draws multiple instances using the provided instance buffer.
 * Does nothing if the number of instances is <= 0.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawMeshInstanced(R3D_Mesh mesh, R3D_Material material, R3D_InstanceBuffer instances, int count);

/**
 * @brief Queues an instanced mesh draw command with an instance range.
 *
 * Draws 'count' instances starting at 'offset' in the instance buffer.
 * Both 'offset' and 'count' are clamped to stay within [0, instances.capacity]:
 *   - offset is clamped to [0, capacity]
 *   - count is clamped to [0, capacity - offset]
 * Does nothing if the resulting count is <= 0.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawMeshInstancedEx(R3D_Mesh mesh, R3D_Material material, R3D_InstanceBuffer instances, int offset, int count);

/**
 * @brief Queues an instanced mesh draw command with an instance range and an additional transform.
 *
 * Draws 'count' instances starting at 'offset' in the instance buffer.
 * Both 'offset' and 'count' are clamped to stay within [0, instances.capacity]:
 *   - offset is clamped to [0, capacity]
 *   - count is clamped to [0, capacity - offset]
 * Does nothing if the resulting count is <= 0.
 * The transform is applied to all instances.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawMeshInstancedPro(R3D_Mesh mesh, R3D_Material material, R3D_InstanceBuffer instances, int offset, int count, Matrix transform);

/**
 * @brief Queues an animated mesh draw command.
 *
 * Uses the provided animation player for GPU skinning.
 */
R3DAPI void R3D_DrawAnimatedMesh(R3D_Mesh mesh, R3D_Material material, R3D_AnimationPlayer player, Vector3 position, float scale);

/**
 * @brief Queues an animated mesh draw command with rotation and non-uniform scale.
 */
R3DAPI void R3D_DrawAnimatedMeshEx(R3D_Mesh mesh, R3D_Material material, R3D_AnimationPlayer player, Vector3 position, Quaternion rotation, Vector3 scale);

/**
 * @brief Queues an animated mesh draw command using a full transform matrix.
 */
R3DAPI void R3D_DrawAnimatedMeshPro(R3D_Mesh mesh, R3D_Material material, R3D_AnimationPlayer player, Matrix transform);

/**
 * @brief Draws an animated mesh with explicit local-space culling bounds.
 *
 * The bounds must contain all positions produced by skeletal animation,
 * IK and other deformations for this draw.
 *
 * Does not modify the original mesh.
 * Invalid bounds cause the draw to be rejected.
 */
R3DAPI void R3D_DrawAnimatedMeshProBounds(R3D_Mesh mesh, R3D_Material material, R3D_AnimationPlayer player, BoundingBox bounds, Matrix transform);

/**
 * @brief GPU morphing followed by skeletal animation.
 */
R3DAPI bool R3D_DrawAnimatedShapeKeyMeshPro(R3D_Mesh mesh, R3D_Material material, R3D_AnimationPlayer player, const R3D_ShapeKeyGpu* gpu, const R3D_ShapeKeyInstance* instance, Matrix transform);

/**
 * @brief Draws a GPU-morphed and skinned mesh with explicit bounds.
 *
 * The bounds must contain the final geometry after BOTH
 * morph deformation and skeletal skinning.
 *
 * The existing GPU morph-weight evaluation is preserved.
 * Invalid bounds cause the draw to be rejected.
 */
R3DAPI bool R3D_DrawAnimatedShapeKeyMeshProBounds(R3D_Mesh mesh, R3D_Material material, R3D_AnimationPlayer player, const R3D_ShapeKeyGpu* gpu, const R3D_ShapeKeyInstance* instance, BoundingBox bounds, Matrix transform);

/**
 * @brief Queues a model draw command with position and uniform scale.
 * 
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawModel(R3D_Model model, Vector3 position, float scale);

/**
 * @brief Queues a model draw command with position, rotation and non-uniform scale.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawModelEx(R3D_Model model, Vector3 position, Quaternion rotation, Vector3 scale);

/**
 * @brief Queues a model draw command using a full transform matrix.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawModelPro(R3D_Model model, Matrix transform);

/**
 * @brief Queues an instanced model draw command.
 *
 * Draws multiple instances using the provided instance buffer.
 * Does nothing if the number of instances is <= 0.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawModelInstanced(R3D_Model model, R3D_InstanceBuffer instances, int count);

/**
 * @brief Queues an instanced model draw command with an instance range.
 *
 * Draws 'count' instances starting at 'offset' in the instance buffer.
 * Both 'offset' and 'count' are clamped to stay within [0, instances.capacity]:
 *   - offset is clamped to [0, capacity]
 *   - count is clamped to [0, capacity - offset]
 * Does nothing if the resulting count is <= 0.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawModelInstancedEx(R3D_Model model, R3D_InstanceBuffer instances, int offset, int count);

/**
 * @brief Queues an instanced model draw command with an instance range and an additional transform.
 *
 * Draws 'count' instances starting at 'offset' in the instance buffer.
 * Both 'offset' and 'count' are clamped to stay within [0, instances.capacity]:
 *   - offset is clamped to [0, capacity]
 *   - count is clamped to [0, capacity - offset]
 * Does nothing if the resulting count is <= 0.
 * The transform is applied to all instances.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawModelInstancedPro(R3D_Model model, R3D_InstanceBuffer instances, int offset, int count, Matrix transform);

/**
 * @brief Queues an animated model draw command.
 *
 * Uses the provided animation player to compute the pose.
 * 
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawAnimatedModel(R3D_Model model, R3D_AnimationPlayer player, Vector3 position, float scale);

/**
 * @brief Queues an animated model draw command with position, rotation and non-uniform scale.
 *
 * Uses the provided animation player to compute the pose.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawAnimatedModelEx(R3D_Model model, R3D_AnimationPlayer player, Vector3 position, Quaternion rotation, Vector3 scale);

/**
 * @brief Queues an animated model draw command using a full transform matrix.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawAnimatedModelPro(R3D_Model model, R3D_AnimationPlayer player, Matrix transform);

/**
 * @brief Draws an animated model with explicit local-space culling bounds.
 *
 * The bounds must contain all animated meshes, including their
 * skeletal, IK, spring and other deformations.
 *
 * Does not modify the original model.
 * Invalid bounds cause the draw to be rejected.
 */
R3DAPI void R3D_DrawAnimatedModelProBounds(R3D_Model model, R3D_AnimationPlayer player, BoundingBox bounds, Matrix transform);

/**
 * @brief Queues an instanced animated model draw command.
 *
 * Draws multiple animated instances using the provided instance buffer.
 * Does nothing if the number of instances is <= 0.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawAnimatedModelInstanced(R3D_Model model, R3D_AnimationPlayer player, R3D_InstanceBuffer instances, int count);

/**
 * @brief Queues an instanced animated model draw command with an instance range.
 *
 * Draws 'count' animated instances starting at 'offset' in the instance buffer.
 * Both 'offset' and 'count' are clamped to stay within [0, instances.capacity]:
 *   - offset is clamped to [0, capacity]
 *   - count is clamped to [0, capacity - offset]
 * Does nothing if the resulting count is <= 0.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawAnimatedModelInstancedEx(R3D_Model model, R3D_AnimationPlayer player, R3D_InstanceBuffer instances, int offset, int count);

/**
 * @brief Queues an instanced animated model draw command with an instance range and an additional transform.
 *
 * Draws 'count' animated instances starting at 'offset' in the instance buffer.
 * Both 'offset' and 'count' are clamped to stay within [0, instances.capacity]:
 *   - offset is clamped to [0, capacity]
 *   - count is clamped to [0, capacity - offset]
 * Does nothing if the resulting count is <= 0.
 * The transform is applied to all instances.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawAnimatedModelInstancedPro(R3D_Model model, R3D_AnimationPlayer player, R3D_InstanceBuffer instances, int offset, int count, Matrix transform);

/**
 * @brief Queues a decal draw command with position and uniform scale.
 * 
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawDecal(R3D_Decal decal, Vector3 position, float scale);

/**
 * @brief Queues a decal draw command with position, rotation and non-uniform scale.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawDecalEx(R3D_Decal decal, Vector3 position, Quaternion rotation, Vector3 scale);

/**
 * @brief Queues a decal draw command using a full transform matrix.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawDecalPro(R3D_Decal decal, Matrix transform);

/**
 * @brief Queues an instanced decal draw command.
 *
 * Draws multiple instances using the provided instance buffer.
 * Does nothing if the number of instances is <= 0.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawDecalInstanced(R3D_Decal decal, R3D_InstanceBuffer instances, int count);

/**
 * @brief Queues an instanced decal draw command with an instance range.
 *
 * Draws 'count' instances starting at 'offset' in the instance buffer.
 * Both 'offset' and 'count' are clamped to stay within [0, instances.capacity]:
 *   - offset is clamped to [0, capacity]
 *   - count is clamped to [0, capacity - offset]
 * Does nothing if the resulting count is <= 0.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawDecalInstancedEx(R3D_Decal decal, R3D_InstanceBuffer instances, int offset, int count);

/**
 * @brief Queues an instanced decal draw command with an instance range and an additional transform.
 *
 * Draws 'count' instances starting at 'offset' in the instance buffer.
 * Both 'offset' and 'count' are clamped to stay within [0, instances.capacity]:
 *   - offset is clamped to [0, capacity]
 *   - count is clamped to [0, capacity - offset]
 * Does nothing if the resulting count is <= 0.
 * The transform is applied to all instances.
 *
 * The command is executed during R3D_End().
 */
R3DAPI void R3D_DrawDecalInstancedPro(R3D_Decal decal, R3D_InstanceBuffer instances, int offset, int count, Matrix transform);

#ifdef __cplusplus
} // extern "C"
#endif

/** @} */ // end of Draw

#endif // R3D_DRAW_H
