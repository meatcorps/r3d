/* r3d_animation_spring.h -- R3D Animation Spring Bone Module.
 *
 * Copyright (c) 2025-2026 Dennis Steffen
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#ifndef R3D_ANIMATION_SPRING_H
#define R3D_ANIMATION_SPRING_H

#include "./r3d_animation_player.h"

#ifdef __cplusplus
extern "C" {


#endif

/**
 * @brief Persistent simulation state for one spring bone.
 *
 * Give each independently simulated bone its own state.
 * Keep this state across animation frames.
 */
typedef struct R3D_SpringBoneState {
    Vector3 tipModel;
    Vector3 velocityModel;
    bool initialized;
} R3D_SpringBoneState;


/**
 * @brief Optional two-axis angular limits.
 *
 * Angles are relative to the current authored spring direction.
 *
 * localReferenceAxis defines the positive horizontal direction.
 * The vertical direction is derived using:
 *
 *     cross(horizontal, forward)
 *
 * Example for a spring pointing along local +Y:
 *
 * localAxis          = {0, 1, 0}
 * localReferenceAxis = {1, 0, 0}
 *
 * Horizontal bending is toward +/-X.
 * Vertical bending is toward +/-Z.
 *
 * All angles are in radians.
 */
typedef struct R3D_SpringBoneAngleLimits {
    bool enabled;

    Vector3 localReferenceAxis;

    float minHorizontalRadians;
    float maxHorizontalRadians;

    float minVerticalRadians;
    float maxVerticalRadians;
} R3D_SpringBoneAngleLimits;

typedef struct R3D_SpringBoneParams {
    Vector3 localAxis;
    float length;
    float frequencyHz;
    float dampingRatio;
    float weight;

    R3D_SpringBoneAngleLimits limits;
} R3D_SpringBoneParams;

/**
 * @brief Clears spring velocity and simulation history.
 *
 * Use after teleports, scene resets, animation discontinuities,
 * or whenever the previous spring state should be discarded.
 */
R3DAPI void R3D_ResetAnimationSpringBone(R3D_SpringBoneState *state);

/**
 * @brief Simulates one spring bone and applies its rotation to localPose.
 *
 * The current modelPose must represent the newly evaluated authored
 * animation, including any earlier procedural stages.
 *
 * The first update initializes the virtual tip to the authored pose.
 * Subsequent calls maintain simulation state between frames.
 *
 * The solver changes only the selected bone's local matrix.
 * It preserves the bone origin and rotates its existing basis,
 * retaining scale and shear.
 *
 * No hierarchy recomputation or GPU upload occurs here.
 *
 * After a successful call, recompute the affected subtree.
 * For chains, evaluate from parent to child, recomputing after
 * each spring bone.
 *
 * At weight zero, simulation continues but the pose is unchanged.
 * Delta time must be finite and within [0, 1] seconds.
 *
 * The spring operates in character/model space. Scene-level world
 * translation and rotation are not included in its simulation.
 *
 * @return True on success; false on invalid input without mutation.
 */
R3DAPI bool R3D_ApplyAnimationSpringBone(
    R3D_AnimationPlayer *player,
    int boneIndex,
    R3D_SpringBoneState *state,
    R3D_SpringBoneParams params,
    float deltaTime
);

#ifdef __cplusplus
}
#endif

#endif // R3D_ANIMATION_SPRING_H
