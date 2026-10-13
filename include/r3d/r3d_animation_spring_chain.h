/* r3d_animation_spring_chain.h -- R3D Spring Chain Module.
 *
 * Copyright (c) 2025-2026 Dennis Steffen
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#ifndef R3D_ANIMATION_SPRING_CHAIN_H
#define R3D_ANIMATION_SPRING_CHAIN_H

#include "./r3d_animation_spring.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Input description for one bone in a spring chain.
 *
 * Descriptors may be supplied in any order.
 */
typedef struct R3D_SpringChainBoneDesc {
    int boneIndex;
    R3D_SpringBoneParams params;
} R3D_SpringChainBoneDesc;

/**
 * @brief Runtime data for one spring chain bone.
 *
 * Entries are stored in parent-to-child order.
 */
typedef struct R3D_SpringChainBone {
    int boneIndex;
    R3D_SpringBoneParams params;
    R3D_SpringBoneState state;
} R3D_SpringChainBone;

/**
 * @brief Persistent spring chain controller.
 *
 * Owns the bone array and simulation states.
 * References the skeleton hierarchy without owning it.
 *
 * A chain represents one linear parent-to-child path.
 * Branches are not supported in a single chain.
 */
typedef struct R3D_SpringChain {
    R3D_SpringChainBone* bones;
    int boneCount;

    const R3D_BoneInfo* skeletonBones;
    int skeletonBoneCount;
} R3D_SpringChain;

/**
 * @brief Creates a spring chain from unordered descriptors.
 *
 * Automatically sorts bones from parent to child.
 * Every bone except the first must be a direct child
 * of the previous bone.
 *
 * Returns a zero-initialized chain if validation or
 * allocation fails.
 *
 * The skeleton must outlive the chain.
 */
R3DAPI R3D_SpringChain R3D_LoadSpringChain(
    const R3D_AnimationPlayer* player,
    const R3D_SpringChainBoneDesc* descriptors,
    int count
);

/**
 * @brief Releases chain-owned memory.
 *
 * Does not unload the animation player or skeleton.
 */
R3DAPI void R3D_UnloadSpringChain(R3D_SpringChain chain);

/**
 * @brief Returns whether the chain contains valid storage.
 */
R3DAPI bool R3D_IsSpringChainValid(
    const R3D_SpringChain* chain
);

/**
 * @brief Clears simulation history for every spring bone.
 *
 * Does not modify the animation player's pose.
 */
R3DAPI void R3D_ResetSpringChain(R3D_SpringChain* chain);

/**
 * @brief Evaluates every spring bone in parent-to-child order.
 *
 * The animation player's modelPose must already be current.
 *
 * Each bone:
 *   1. Applies its spring.
 *   2. Recomputes its affected hierarchy subtree.
 *
 * The chain does not sample animations, advance animation
 * time, or upload the final pose to the GPU.
 *
 * Parameters may be edited through chain->bones[i].params.
 * Invalid parameters are checked before evaluation begins.
 *
 * The player must use the skeleton associated with the
 * chain at creation time.
 *
 * On failure during evaluation, earlier bones may already
 * have changed. Resample the authored pose before retrying.
 *
 * @return True if every spring and hierarchy update succeeds.
 */
R3DAPI bool R3D_UpdateSpringChain(
    R3D_SpringChain* chain,
    R3D_AnimationPlayer* player,
    float deltaTime
);

/**
 * @brief Evaluates a spring chain with gravity, drag,
 * and optional world-space inertia.
 *
 * All bones share the character's model-to-world transform,
 * but retain independent spring force settings.
 *
 * The hierarchy is recomputed after each spring bone.
 * Animation sampling and GPU upload remain external.
 */
R3DAPI bool R3D_UpdateSpringChainWorld(
    R3D_SpringChain* chain,
    R3D_AnimationPlayer* player,
    Matrix modelToWorld,
    float deltaTime
);

#ifdef __cplusplus
}
#endif

#endif // R3D_ANIMATION_SPRING_CHAIN_H