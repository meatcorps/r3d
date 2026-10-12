/* r3d_animation_ik.h -- R3D Animation IK Module.
*
 * Copyright (c) 2025-2026 Dennis Steffen
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#ifndef R3D_ANIMATION_IK_H
#define R3D_ANIMATION_IK_H

#include "./r3d_animation_player.h"

#ifdef __cplusplus
extern "C" {

#endif

/**
* @brief Optional two-bone IK bend-angle constraint.
*
* Angles describe the angle between the upper and lower segments:
* 0 radians means fully straight, PI means fully folded.
*
* When disabled, the solver behaves as before.
*/
typedef struct R3D_TwoBoneIKBendLimit {
    bool enabled;
    float minAngleRadians;
    float maxAngleRadians;
} R3D_TwoBoneIKBendLimit;

/**
 * @brief Configuration for an analytic two-bone IK chain.
 */
typedef struct R3D_TwoBoneIKParams {
    Vector3 targetModel;
    Vector3 poleModel;
    float weight;
    R3D_TwoBoneIKBendLimit bend;
} R3D_TwoBoneIKParams;

/**
 * @brief Applies analytic two-bone inverse kinematics.
 *
 * The hierarchy must be rootBone -> middleBone -> endBone, with direct
 * parent relationships. All three model poses must be current.
 *
 * The solver uses the current segment lengths, clamps unreachable
 * targets to the reachable distance, and uses the pole to select
 * the elbow/knee bend direction.
 *
 * Changes the local matrices of rootBone and middleBone only.
 * The end bone's local matrix is preserved.
 *
 * The weight blends the two joint rotation corrections with the
 * existing animated pose. Intermediate weights need not place
 * the end effector exactly at the target.
 *
 * Does not recompute modelPose, advance animation, or upload to GPU.
 * Recompute from rootBone after a successful call.
 *
 * @return True on success, false for invalid or degenerate input.
 */
R3DAPI bool R3D_ApplyAnimationTwoBoneIK(R3D_AnimationPlayer *player, int rootBone, int middleBone, int endBone, R3D_TwoBoneIKParams params);

#ifdef __cplusplus
}
#endif

#endif // R3D_ANIMATION_IK_H
