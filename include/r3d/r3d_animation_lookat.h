/* r3d_animation_lookat.h -- R3D Animation Look at Module.
*
* Copyright (c) 2025-2026 Dennis Steffen
*
* This software is provided 'as-is', without any express or implied warranty.
* For conditions of distribution and use, see accompanying LICENSE file.
*/

#ifndef R3D_ANIMATION_LOOKAT_H
#define R3D_ANIMATION_LOOKAT_H

#include "./r3d_animation_player.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuration for procedural bone LookAt.
 */
typedef struct R3D_BoneLookAtParams {
    Vector3 localForward;   ///< Bone-local direction that should face the target.
    Vector3 localUp;        ///< Bone-local up direction, used to control roll.
    Vector3 modelUp;        ///< Reference up direction in character/model space.
    float weight;           ///< Blend weight, from 0 to 1.
    float maxAngleRadians;  ///< Maximum total rotation correction. Zero disables the limit.
} R3D_BoneLookAtParams;

/**
 * @brief Rotates a bone toward a target in character/model space.
 *
 * Applies a weighted rotational correction to the current local pose.
 * Preserves the bone's translation, scale and shear.
 *
 * The parent model pose must already be current and invertible.
 * The resulting change affects localPose only; call a hierarchy
 * recomputation helper afterward.
 *
 * Does not sample animations, advance time, or upload to the GPU.
 *
 * Returns false for invalid input, degenerate axes, or a target
 * coincident with the bone origin.
 */
R3DAPI bool R3D_ApplyAnimationBoneLookAt(R3D_AnimationPlayer* player, int boneIndex, Vector3 targetModel, R3D_BoneLookAtParams params);

#ifdef __cplusplus
}
#endif

#endif // R3D_ANIMATION_LOOKAT_H