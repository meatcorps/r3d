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

    // Previous character model-to-world transform.
    Matrix previousWorld;
    bool worldInitialized;

    // Previous authored bone position/direction in world space.
    // Tracks both world-transform and skeletal discontinuities.
    Vector3 previousOriginWorld;
    Vector3 previousDirectionWorld;
    bool trackingInitialized;
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

    // Optional braking region inside each angular limit.
    // Zero preserves the existing hard-limit behavior.
    // The effective region is capped to half the angle range.
    float softZoneRadians;
} R3D_SpringBoneAngleLimits;

/**
 * @brief Optional secondary motion forces.
 *
 * All fields default to zero when omitted.
 */
typedef struct R3D_SpringBoneForces {
    Vector3 gravityWorld; ///< World-space acceleration, units/s^2.
    float drag; ///< Additional viscous damping, 1/s.
    float worldInertia; ///< 0 = model-relative, 1 = full world inertia.
} R3D_SpringBoneForces;

/**
 * @brief Optional spring stability safeguards.
 *
 * All values default to zero (disabled).
 *
 * teleportDistance:
 *   Maximum allowed change in authored bone-origin world position
 *   between updates. Measured in world units.
 *
 * teleportAngleRadians:
 *   Maximum angular change of the authored bone direction,
 *   measured in world space. Range: [0, PI].
 *
 * maxTipOffset:
 *   Maximum model-space deviation from the authored virtual tip
 *   before/after spring integration. Zero disables clamping.
 *
 * maxTipSpeed:
 *   Maximum model-space spring-tip velocity in units/second.
 *   Zero disables clamping.
 */
typedef struct R3D_SpringBoneStability {
    float teleportDistance;
    float teleportAngleRadians;
    float maxTipOffset;
    float maxTipSpeed;
} R3D_SpringBoneStability;

typedef struct R3D_SpringBoneParams {
    Vector3 localAxis;
    float length;
    float frequencyHz;
    float dampingRatio;
    float weight;

    R3D_SpringBoneAngleLimits limits;
    R3D_SpringBoneForces forces;
    R3D_SpringBoneStability stability;
} R3D_SpringBoneParams;

/**
 * @brief Clears spring velocity and simulation history.
 *
 * Use after teleports, scene resets, animation discontinuities,
 * or whenever the previous spring state should be discarded.
 */
R3DAPI void R3D_ResetAnimationSpringBone(R3D_SpringBoneState *state);

R3DAPI bool R3D_ApplyAnimationSpringBone(
    R3D_AnimationPlayer *player,
    int boneIndex,
    R3D_SpringBoneState *state,
    R3D_SpringBoneParams params,
    float deltaTime
);

/**
 * @brief Applies a spring using the character's world transform.
 *
 * modelToWorld is the transform used to place the complete
 * animated character in the scene, not an individual bone matrix.
 *
 * Uses previous-frame world-transform history for inertia.
 * Gravity is supplied in world units per second squared.
 *
 * Use R3D_ResetAnimationSpringBone() after teleports.
 *
 * For physically meaningful force magnitudes, prefer rigid
 * transforms or uniform scale. Nonuniform scaling changes
 * the effective model-space acceleration.
 *
 * Does not recompute modelPose or upload the skinning pose.
 */
R3DAPI bool R3D_ApplyAnimationSpringBoneWorld(
    R3D_AnimationPlayer *player,
    int boneIndex,
    R3D_SpringBoneState *state,
    R3D_SpringBoneParams params,
    Matrix modelToWorld,
    float deltaTime
);

#ifdef __cplusplus
}
#endif

#endif // R3D_ANIMATION_SPRING_H
