/* r3d_animation_spring_chain.c -- R3D Spring Chain Module.
 *
 * Copyright (c) 2025-2026 Dennis Steffen
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_animation_spring_chain.h>
#include <raymath.h>
#include <math.h>
#include <stdlib.h>

static bool finite_vec3(Vector3 value)
{
    return isfinite(value.x) &&
           isfinite(value.y) &&
           isfinite(value.z);
}

static bool valid_params(R3D_SpringBoneParams params)
{
    if (!finite_vec3(params.localAxis) ||
        !isfinite(params.length) ||
        !isfinite(params.frequencyHz) ||
        !isfinite(params.dampingRatio) ||
        !isfinite(params.weight))
    {
        return false;
    }

    if (Vector3LengthSqr(params.localAxis) <= 1e-12f ||
        params.length <= 0.0f ||
        params.frequencyHz <= 0.0f ||
        params.frequencyHz > 1000.0f ||
        params.dampingRatio < 0.0f ||
        params.dampingRatio > 1.0f ||
        params.weight < 0.0f ||
        params.weight > 1.0f)
    {
        return false;
    }

    if (!params.limits.enabled) return true;

    const R3D_SpringBoneAngleLimits* limits = &params.limits;

    if (!finite_vec3(limits->localReferenceAxis) ||
        !isfinite(limits->minHorizontalRadians) ||
        !isfinite(limits->maxHorizontalRadians) ||
        !isfinite(limits->minVerticalRadians) ||
        !isfinite(limits->maxVerticalRadians))
    {
        return false;
    }

    if (limits->minHorizontalRadians < -PI ||
        limits->maxHorizontalRadians > PI ||
        limits->minHorizontalRadians >
            limits->maxHorizontalRadians ||
        limits->minVerticalRadians < -PI * 0.5f ||
        limits->maxVerticalRadians > PI * 0.5f ||
        limits->minVerticalRadians >
            limits->maxVerticalRadians)
    {
        return false;
    }

    Vector3 forward = Vector3Normalize(params.localAxis);

    Vector3 reference = Vector3Subtract(
        limits->localReferenceAxis,
        Vector3Scale(
            forward,
            Vector3DotProduct(
                limits->localReferenceAxis,
                forward
            )
        )
    );

    float referenceLengthSq = Vector3LengthSqr(reference);

    return isfinite(referenceLengthSq) &&
           referenceLengthSq > 1e-12f;
}

static bool valid_state(const R3D_SpringBoneState* state)
{
    if (!state->initialized) return true;

    return finite_vec3(state->tipModel) &&
           finite_vec3(state->velocityModel);
}

static int find_descriptor(
    const R3D_SpringChainBoneDesc* descriptors,
    int count,
    int boneIndex)
{
    for (int i = 0; i < count; i++)
    {
        if (descriptors[i].boneIndex == boneIndex)
        {
            return i;
        }
    }

    return -1;
}

R3D_SpringChain R3D_LoadSpringChain(
    const R3D_AnimationPlayer* player,
    const R3D_SpringChainBoneDesc* descriptors,
    int count)
{
    R3D_SpringChain result = {0};

    if (!player || !player->skeleton.bones ||
        !descriptors || count <= 0 ||
        count > player->skeleton.boneCount)
    {
        return result;
    }

    const R3D_Skeleton* skeleton = &player->skeleton;
    const int totalBones = skeleton->boneCount;

    int headDescriptor = -1;
    int headCount = 0;

    // ========================================
    // VALIDATE DESCRIPTORS
    // ========================================

    for (int i = 0; i < count; i++)
    {
        const int index = descriptors[i].boneIndex;

        if (index < 0 || index >= totalBones ||
            !valid_params(descriptors[i].params))
        {
            return result;
        }

        const int parent = skeleton->bones[index].parent;

        if (parent < -1 || parent >= totalBones ||
            parent == index)
        {
            return result;
        }

        // Reject duplicate bone indices.
        for (int j = 0; j < i; j++)
        {
            if (descriptors[j].boneIndex == index)
            {
                return result;
            }
        }

        // The head is the only selected bone whose parent
        // is not another selected bone.
        if (find_descriptor(descriptors, count, parent) < 0)
        {
            headDescriptor = i;
            headCount++;
        }
    }

    // Multiple heads indicate disconnected chains.
    // Zero heads indicates a cycle.
    if (headCount != 1) return result;

    R3D_SpringChainBone* bones = calloc(
        (size_t)count,
        sizeof(*bones)
    );

    if (!bones) return result;

    // ========================================
    // SORT PARENT TO CHILD
    // ========================================

    int descriptorIndex = headDescriptor;

    for (int i = 0; i < count; i++)
    {
        if (descriptorIndex < 0)
        {
            free(bones);
            return result;
        }

        const R3D_SpringChainBoneDesc* descriptor =
            &descriptors[descriptorIndex];

        bones[i].boneIndex = descriptor->boneIndex;
        bones[i].params = descriptor->params;

        // Find the direct child of this bone.
        // A single chain may have only one selected child.
        int nextDescriptor = -1;

        for (int j = 0; j < count; j++)
        {
            const int candidate = descriptors[j].boneIndex;

            if (skeleton->bones[candidate].parent ==
                descriptor->boneIndex)
            {
                if (nextDescriptor >= 0)
                {
                    // Branching is unsupported.
                    free(bones);
                    return result;
                }

                nextDescriptor = j;
            }
        }

        if (i == count - 1)
        {
            // No unprocessed bones may remain.
            if (nextDescriptor >= 0)
            {
                free(bones);
                return result;
            }
        }
        else if (nextDescriptor < 0)
        {
            // A missing successor means there are
            // disconnected descriptors.
            free(bones);
            return result;
        }

        descriptorIndex = nextDescriptor;
    }

    result.bones = bones;
    result.boneCount = count;
    result.skeletonBones = skeleton->bones;
    result.skeletonBoneCount = totalBones;

    return result;
}

void R3D_UnloadSpringChain(R3D_SpringChain chain)
{
    free(chain.bones);
}

bool R3D_IsSpringChainValid(const R3D_SpringChain* chain)
{
    return chain &&
           chain->bones &&
           chain->boneCount > 0 &&
           chain->skeletonBones &&
           chain->skeletonBoneCount >= chain->boneCount;
}

void R3D_ResetSpringChain(R3D_SpringChain* chain)
{
    if (!R3D_IsSpringChainValid(chain)) return;

    for (int i = 0; i < chain->boneCount; i++)
    {
        R3D_ResetAnimationSpringBone(
            &chain->bones[i].state
        );
    }
}

bool R3D_UpdateSpringChain(
    R3D_SpringChain* chain,
    R3D_AnimationPlayer* player,
    float deltaTime)
{
    if (!R3D_IsSpringChainValid(chain) ||
        !player ||
        !player->localPose ||
        !player->modelPose ||
        !isfinite(deltaTime) ||
        deltaTime < 0.0f ||
        deltaTime > 1.0f)
    {
        return false;
    }

    // The chain must be evaluated against the same
    // skeleton hierarchy it was created for.
    if (player->skeleton.bones != chain->skeletonBones ||
        player->skeleton.boneCount != chain->skeletonBoneCount)
    {
        return false;
    }

    // ========================================
    // PREFLIGHT VALIDATION
    // ========================================
    //
    // Validate all entries before changing any poses.
    // This catches invalid runtime parameter edits.

    for (int i = 0; i < chain->boneCount; i++)
    {
        const R3D_SpringChainBone* bone = &chain->bones[i];

        if (bone->boneIndex < 0 ||
            bone->boneIndex >= chain->skeletonBoneCount ||
            !valid_params(bone->params) ||
            !valid_state(&bone->state))
        {
            return false;
        }

        if (i > 0)
        {
            const int expectedParent =
                chain->bones[i - 1].boneIndex;

            if (player->skeleton.bones[bone->boneIndex].parent !=
                expectedParent)
            {
                return false;
            }
        }
    }

    // ========================================
    // EVALUATE CHAIN
    // ========================================

    for (int i = 0; i < chain->boneCount; i++)
    {
        R3D_SpringChainBone* bone = &chain->bones[i];

        if (!R3D_ApplyAnimationSpringBone(
            player,
            bone->boneIndex,
            &bone->state,
            bone->params,
            deltaTime))
        {
            return false;
        }

        // Descendants need the updated parent model pose
        // before their own spring simulation.
        if (!R3D_RecomputeAnimationModelPoseFromBone(
            player,
            bone->boneIndex))
        {
            return false;
        }
    }

    return true;
}