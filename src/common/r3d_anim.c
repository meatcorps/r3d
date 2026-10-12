/* r3d_anim.c -- Common R3D Animation Helper Functions
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include "./r3d_anim.h"

// ========================================
// INTERNAL FUNCTION
// ========================================

static void find_key_frames(const float* times, uint32_t count, float time,
                            uint32_t* outIdx0, uint32_t* outIdx1,
                            float* outT)
{
    // No keys
    if (count == 0)
    {
        *outIdx0 = *outIdx1 = 0;
        *outT = 0.0f;
        return;
    }

    // Single key or before first
    if (count == 1 || time <= times[0])
    {
        *outIdx0 = *outIdx1 = 0;
        *outT = 0.0f;
        return;
    }

    // After last
    if (time >= times[count - 1])
    {
        *outIdx0 = *outIdx1 = count - 1;
        *outT = 0.0f;
        return;
    }

    // Binary search
    uint32_t left = 0;
    uint32_t right = count - 1;
    while(right - left > 1)
    {
        uint32_t mid = (left + right) >> 1;
        if (times[mid] <= time) left  = mid;
        else right = mid;
    }

    *outIdx0 = left;
    *outIdx1 = right;

    float t0 = times[left];
    float t1 = times[right];
    float dt = t1 - t0;

    *outT = (dt > 0.0f) ? (time - t0) / dt : 0.0f;
}

// ========================================
// TRANSFORM/MATRIX FUNCTIONS
// ========================================

Transform r3d_anim_transform_lerp(Transform a, Transform b, float value)
{
    Transform result;
    result.translation = Vector3Lerp(a.translation, b.translation, value);
    result.rotation    = QuaternionSlerp(a.rotation, b.rotation, value);
    result.scale       = Vector3Lerp(a.scale, b.scale, value);
    return result;
}

Transform r3d_anim_transform_add(Transform a, Transform b)
{
    Transform result;
    result.translation = Vector3Add(a.translation, b.translation);
    result.rotation    = QuaternionAdd(a.rotation, b.rotation);
    result.scale       = Vector3Add(a.scale, b.scale);
    return result;
}

Transform r3d_anim_transform_add_v(Transform a, Transform b, float value)
{
    Transform result;
    result.translation = Vector3Add(a.translation, Vector3Scale(b.translation, value));
    result.rotation    = QuaternionAdd(a.rotation, QuaternionScale(b.rotation, value));
    result.scale       = Vector3Add(a.scale, Vector3Scale(b.scale, value));
    return result;
}

Transform r3d_anim_transform_addx_v(Transform a, Transform b, float value)
{
    Transform result;
    result.translation = Vector3Add(a.translation, Vector3Scale(b.translation, value));
    result.rotation    = QuaternionSlerp(a.rotation, b.rotation, value);
    result.scale       = Vector3Add(a.scale, Vector3Scale(b.scale, value));
    return result;
}

Transform r3d_anim_transform_subtr(Transform a, Transform b)
{
    Transform result;
    result.translation = Vector3Subtract(a.translation, b.translation);
    result.rotation    = QuaternionSubtract(a.rotation, b.rotation);
    result.scale       = Vector3Subtract(a.scale, b.scale);
    return result;
}

Transform r3d_anim_transform_scale(Transform tf, float val)
{
    Transform result;
    result.translation = Vector3Scale(tf.translation, val);
    result.rotation    = QuaternionScale(tf.rotation, val);
    result.scale       = Vector3Scale(tf.scale, val);
    return result;
}

static int bone_in_subtree(const R3D_BoneInfo* bones, int boneCount, int boneIndex, int rootBone)
{
    int current = boneIndex;

    for (int depth = 0; depth <= boneCount; depth++)
    {
        if (current == -1) return 0;
        if (current < 0 || current >= boneCount) return -1;
        if (current == rootBone) return 1;

        current = bones[current].parent;
    }

    // Invalid hierarchy: a cycle was encountered.
    return -1;
}

static bool recompute_model_bone(R3D_AnimationPlayer* player, int boneIndex, uint8_t* states)
{
    if (states[boneIndex] == 2) return true;
    if (states[boneIndex] == 1) return false;

    const R3D_Skeleton* skeleton = &player->skeleton;
    const int parentIndex = skeleton->bones[boneIndex].parent;

    if (parentIndex < -1 || parentIndex >= skeleton->boneCount) return false;

    states[boneIndex] = 1;

    if (parentIndex >= 0)
    {
        if (!recompute_model_bone(player, parentIndex, states)) return false;
    }

    Matrix parentPose = parentIndex >= 0 ? player->modelPose[parentIndex] : skeleton->rootBind;

    player->modelPose[boneIndex] = MatrixMultiply(player->localPose[boneIndex], parentPose);
    states[boneIndex] = 2;

    return true;
}

bool r3d_anim_matrices_compute(R3D_AnimationPlayer* player, int rootBone)
{
    if (!player || !player->localPose || !player->modelPose || !player->skeleton.bones) return false;

    const int boneCount = player->skeleton.boneCount;

    if (boneCount <= 0) return false;
    if (rootBone < -1 || rootBone >= boneCount) return false;

    // Most skeletons fit within the 256-bone range used by R3D bone masks.
    // Larger skeletons remain supported, using temporary heap storage.
    uint8_t stackStates[256];
    uint8_t* states = stackStates;

    if (boneCount > (int)sizeof(stackStates))
    {
        states = MemAlloc((unsigned int)boneCount);
        if (!states) return false;
    }

    bool success = true;

    for (int i = 0; i < boneCount; i++)
    {
        if (rootBone == -1)
        {
            states[i] = 0;
        }
        else
        {
            int membership = bone_in_subtree(player->skeleton.bones, boneCount, i, rootBone);

            if (membership < 0)
            {
                success = false;
                break;
            }

            // Bones outside the subtree are already computed and preserved.
            states[i] = membership ? 0 : 2;
        }
    }

    if (success)
    {
        for (int i = 0; i < boneCount; i++)
        {
            if (states[i] == 0 && !recompute_model_bone(player, i, states))
            {
                success = false;
                break;
            }
        }
    }

    if (states != stackStates) MemFree(states);

    return success;
}

// ========================================
// ANIMATION CHANNEL FUNCTIONS
// ========================================

const R3D_AnimationChannel* r3d_anim_channel_find(const R3D_Animation* anim, int boneIdx)
{
    for (int i = 0; i < anim->channelCount; i++)
    {
        if (anim->channels[i].boneIndex == boneIdx)
        {
            return &anim->channels[i];
        }
    }
    return NULL;
}

Transform r3d_anim_channel_lerp(const R3D_AnimationChannel* channel, float time, const Matrix* localBind, Transform* rest0, Transform* restN)
{
    Transform result = {
        .translation = {0.0f, 0.0f, 0.0f},
        .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
        .scale = {1.0f, 1.0f, 1.0f}
    };

    // Only decompose the bind pose when a track is missing.
    if (channel->translation.count == 0 || channel->rotation.count == 0 || channel->scale.count == 0)
    {
        r3d_matrix_decompose(*localBind, &result.translation, &result.rotation, &result.scale);
    }

    // Initialize root-motion endpoints with the complete fallback pose.
    if (rest0) *rest0 = result;
    if (restN) *restN = result;

    if (channel->translation.count > 0)
    {
        const Vector3* values = (const Vector3*)channel->translation.values;
        uint32_t i0, i1;
        float t;

        find_key_frames(channel->translation.times, channel->translation.count, time, &i0, &i1, &t);

        result.translation = Vector3Lerp(values[i0], values[i1], t);

        if (rest0) rest0->translation = values[0];
        if (restN) restN->translation = values[channel->translation.count - 1];
    }

    if (channel->rotation.count > 0)
    {
        const Quaternion* values = (const Quaternion*)channel->rotation.values;
        uint32_t i0, i1;
        float t;

        find_key_frames(channel->rotation.times, channel->rotation.count, time, &i0, &i1, &t);

        result.rotation = QuaternionSlerp(values[i0], values[i1], t);

        if (rest0) rest0->rotation = values[0];
        if (restN) restN->rotation = values[channel->rotation.count - 1];
    }

    if (channel->scale.count > 0)
    {
        const Vector3* values = (const Vector3*)channel->scale.values;
        uint32_t i0, i1;
        float t;

        find_key_frames(channel->scale.times, channel->scale.count, time, &i0, &i1, &t);

        result.scale = Vector3Lerp(values[i0], values[i1], t);

        if (rest0) rest0->scale = values[0];
        if (restN) restN->scale = values[channel->scale.count - 1];
    }

    return result;
}

static float additive_scale_component(float base, float layer, float reference, float weight)
{
    // A zero reference scale has no multiplicative inverse.
    // Use an additive difference as a fallback.
    if (fabsf(reference) <= 1e-6f)
    {
        return base + (layer - reference) * weight;
    }

    const float ratio = layer / reference;
    return base * (1.0f + (ratio - 1.0f) * weight);
}

Transform r3d_anim_transform_additive(Transform base, Transform layer, Transform reference, float weight)
{
    if (weight <= 0.0f) return base;

    Transform result;

    // Translation: apply the offset relative to the reference pose.
    Vector3 translationDelta = Vector3Subtract(layer.translation, reference.translation);
    result.translation = Vector3Add(base.translation, Vector3Scale(translationDelta, weight));

    // Rotation: apply the layer's local rotation relative to the reference pose.
    Quaternion baseRotation = r3d_quaternion_normalize_or_id(base.rotation);
    Quaternion layerRotation = r3d_quaternion_normalize_or_id(layer.rotation);
    Quaternion referenceRotation = r3d_quaternion_normalize_or_id(reference.rotation);

    Quaternion rotationDelta = QuaternionMultiply(QuaternionInvert(referenceRotation), layerRotation);
    rotationDelta = r3d_quaternion_normalize_or_id(rotationDelta);

    Quaternion weightedRotation = QuaternionSlerp(QuaternionIdentity(), rotationDelta, weight);
    result.rotation = r3d_quaternion_normalize_or_id(QuaternionMultiply(baseRotation, weightedRotation));

    // Scale: multiply by the weighted ratio relative to the reference pose.
    result.scale.x = additive_scale_component(base.scale.x, layer.scale.x, reference.scale.x, weight);
    result.scale.y = additive_scale_component(base.scale.y, layer.scale.y, reference.scale.y, weight);
    result.scale.z = additive_scale_component(base.scale.z, layer.scale.z, reference.scale.z, weight);

    return result;
}
