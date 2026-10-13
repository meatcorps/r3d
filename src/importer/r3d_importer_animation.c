/* r3d_importer_animation.c -- Module to import animations from assimp scene.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include "./r3d_importer_internal.h"

#include <assimp/anim.h>
#include <r3d_config.h>
#include <raylib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <limits.h>

#include "../common/r3d_helper.h"

// ========================================
// CHANNEL LOADING (INTERNAL)
// ========================================

static void load_vec3_track(R3D_AnimationTrack* track, unsigned int count, const struct aiVectorKey* keys)
{
    track->count  = (int)count;
    track->times  = NULL;
    track->values = NULL;

    if (track->count == 0) return;

    float* times    = r3d_zalloc(sizeof(float) * track->count);
    Vector3* values = r3d_zalloc(sizeof(Vector3) * track->count);

    for (int i = 0; i < track->count; ++i)
    {
        times[i]  = (float)keys[i].mTime;
        values[i] = r3d_importer_cast(keys[i].mValue);
    }

    track->times  = times;
    track->values = values;
}

static void load_quat_track(R3D_AnimationTrack* track, unsigned int count, const struct aiQuatKey* keys)
{
    track->count  = (int)count;
    track->times  = NULL;
    track->values = NULL;

    if (track->count == 0) return;

    float* times       = r3d_zalloc(sizeof(float) * track->count);
    Quaternion* values = r3d_zalloc(sizeof(Quaternion) * track->count);

    for (int i = 0; i < track->count; ++i)
    {
        times[i]  = (float)keys[i].mTime;
        values[i] = r3d_importer_cast(keys[i].mValue);
    }

    track->times  = times;
    track->values = values;
}

static bool load_channel(R3D_AnimationChannel* channel, const R3D_Importer* importer, const struct aiNodeAnim* aiChannel)
{
    if (!aiChannel)
    {
        R3D_TRACELOG(LOG_ERROR, "Invalid animation channel");
        return false;
    }

    const char* boneName = aiChannel->mNodeName.data;
    channel->boneIndex = r3d_importer_get_bone_index(importer, boneName);

    if (channel->boneIndex < 0)
    {
        R3D_TRACELOG(LOG_WARNING, "Bone '%s' from animation not found in skeleton", boneName);
        return false;
    }

    load_vec3_track(&channel->translation, aiChannel->mNumPositionKeys, aiChannel->mPositionKeys);
    load_quat_track(&channel->rotation, aiChannel->mNumRotationKeys, aiChannel->mRotationKeys);
    load_vec3_track(&channel->scale, aiChannel->mNumScalingKeys, aiChannel->mScalingKeys);

    return true;
}

// ========================================
// ANIMATION LOADING (INTERNAL)
// ========================================


/**
 * Find the scene node named by an Assimp morph channel.
 *
 * glTF weight animations target nodes, which may
 * reference meshes with completely different names.
 */
static const struct aiNode* find_morph_node(
    const struct aiNode* node,
    const char* name)
{
    if (!node || !name)
        return NULL;

    if (strcmp(node->mName.data, name) == 0)
        return node;

    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        const struct aiNode* found = find_morph_node(
            node->mChildren[i],
            name
        );

        if (found)
            return found;
    }

    return NULL;
}

/**
 * Cleanup for a partially loaded animation.
 */
static void free_imported_animation(
    R3D_Animation* animation)
{
    for (int i = 0; i < animation->channelCount; i++)
    {
        R3D_AnimationChannel* channel =
            &animation->channels[i];

        r3d_free((void*)channel->translation.times);
        r3d_free((void*)channel->translation.values);

        r3d_free((void*)channel->rotation.times);
        r3d_free((void*)channel->rotation.values);

        r3d_free((void*)channel->scale.times);
        r3d_free((void*)channel->scale.values);
    }

    r3d_free(animation->channels);

    for (int i = 0; i < animation->morphChannelCount; i++)
    {
        R3D_MorphAnimationChannel* channel =
            &animation->morphChannels[i];

        r3d_free(channel->times);
        r3d_free(channel->weights);
    }

    r3d_free(animation->morphChannels);
    memset(animation, 0, sizeof(*animation));
}

/**
 * Import morph-weight keyframes for one mesh.
 *
 * Assimp mValues contains target indices.
 * mWeights contains their corresponding weights.
 *
 * Missing target entries are treated as zero.
 */
static bool load_morph_channel(R3D_MorphAnimationChannel* output, const struct aiMeshMorphAnim* source, int meshIndex, int targetCount)
{
    if (!output || !source ||
        !source->mKeys ||
        source->mNumKeys == 0 ||
        source->mNumKeys > INT_MAX ||
        targetCount <= 0)
    {
        return false;
    }

    const size_t keyCount = source->mNumKeys;
    const size_t targets = (size_t)targetCount;

    // MemAlloc uses an unsigned int allocation size.
    if (keyCount > UINT_MAX / sizeof(float) ||
        targets > (UINT_MAX / sizeof(float)) / keyCount)
    {
        return false;
    }

    output->meshIndex = meshIndex;
    output->targetCount = targetCount;
    output->keyCount = (int)keyCount;

    output->times = r3d_zalloc(
        keyCount * sizeof(float)
    );

    output->weights = r3d_zalloc(
        keyCount * targets * sizeof(float)
    );

    memset(
        output->weights,
        0,
        keyCount * targets * sizeof(float)
    );

    for (size_t keyIndex = 0; keyIndex < keyCount; keyIndex++)
    {
        const struct aiMeshMorphKey* key =
            &source->mKeys[keyIndex];

        if (!isfinite(key->mTime) ||
            fabs(key->mTime) > FLT_MAX)
        {
            return false;
        }

        if (keyIndex > 0 &&
            key->mTime < source->mKeys[keyIndex - 1].mTime)
        {
            return false;
        }

        if (key->mNumValuesAndWeights > 0 &&
            (!key->mValues || !key->mWeights))
        {
            return false;
        }

        output->times[keyIndex] = (float)key->mTime;

        for (unsigned int i = 0;
             i < key->mNumValuesAndWeights;
             i++)
        {
            unsigned int targetIndex = key->mValues[i];
            double weight = key->mWeights[i];

            if (targetIndex >= (unsigned int)targetCount ||
                !isfinite(weight) ||
                fabs(weight) > FLT_MAX)
            {
                return false;
            }

            output->weights[
                keyIndex * targets + targetIndex
            ] = (float)weight;
        }
    }

    return true;
}

/**
 * Import skeletal and morph channels into one clip.
 */
static bool load_animation(R3D_Animation* animation, const R3D_Importer* importer, const struct aiAnimation* aiAnim)
{
    if (!animation || !importer || !aiAnim)
        return false;

    if (aiAnim->mNumChannels == 0 &&
        aiAnim->mNumMorphMeshChannels == 0)
    {
        return false;
    }

    memset(animation, 0, sizeof(*animation));

    const struct aiScene* scene =
        r3d_importer_get_scene(importer);

    if (!scene)
        return false;

    const int boneCount =
        r3d_importer_get_bone_count(importer);

    animation->boneCount = boneCount;

    animation->duration =
        isfinite(aiAnim->mDuration) &&
        aiAnim->mDuration >= 0.0 &&
        aiAnim->mDuration <= FLT_MAX
            ? (float)aiAnim->mDuration
            : 0.0f;

    animation->ticksPerSecond =
        isfinite(aiAnim->mTicksPerSecond) &&
        aiAnim->mTicksPerSecond > 0.0 &&
        aiAnim->mTicksPerSecond <= FLT_MAX
            ? (float)aiAnim->mTicksPerSecond
            : 24.0f;

    // Preserve the original animation name.
    r3d_string_copy(
        animation->name,
        sizeof(animation->name),
        aiAnim->mName.data,
        aiAnim->mName.length
    );

    // ========================================
    // SKELETAL CHANNELS
    // ========================================

    if (boneCount > 0 && aiAnim->mNumChannels > 0)
    {
        if (!aiAnim->mChannels ||
            aiAnim->mNumChannels > INT_MAX ||
            aiAnim->mNumChannels >
                UINT_MAX / sizeof(R3D_AnimationChannel))
        {
            goto fail;
        }

        size_t allocationSize =
            (size_t)aiAnim->mNumChannels *
            sizeof(R3D_AnimationChannel);

        animation->channels =
            r3d_zalloc(allocationSize);

        memset(
            animation->channels,
            0,
            allocationSize
        );

        for (unsigned int i = 0;
             i < aiAnim->mNumChannels;
             i++)
        {
            R3D_AnimationChannel* channel =
                &animation->channels[
                    animation->channelCount
                ];

            if (load_channel(
                channel,
                importer,
                aiAnim->mChannels[i]))
            {
                animation->channelCount++;
            }
        }
    }

    // ========================================
    // MORPH-WEIGHT CHANNELS
    // ========================================

    if (aiAnim->mNumMorphMeshChannels > 0 &&
        scene->mNumMeshes > 0)
    {
        if (!aiAnim->mMorphMeshChannels)
            goto fail;

        // One node may reference multiple meshes.
        size_t capacity =
            (size_t)aiAnim->mNumMorphMeshChannels *
            scene->mNumMeshes;

        if (capacity > INT_MAX ||
            capacity >
                UINT_MAX / sizeof(R3D_MorphAnimationChannel))
        {
            goto fail;
        }

        size_t allocationSize =
            capacity * sizeof(R3D_MorphAnimationChannel);

        animation->morphChannels =
            r3d_zalloc(allocationSize);

        memset(
            animation->morphChannels,
            0,
            allocationSize
        );

        for (unsigned int c = 0;
             c < aiAnim->mNumMorphMeshChannels;
             c++)
        {
            const struct aiMeshMorphAnim* source =
                aiAnim->mMorphMeshChannels[c];

            if (!source)
                continue;

            // Resolve the animated node first.
            const struct aiNode* node = find_morph_node(
                scene->mRootNode,
                source->mName.data
            );

            // Fallback for formats/importers that name
            // the mesh directly instead of its node.
            const unsigned int candidateCount =
                node ? node->mNumMeshes : scene->mNumMeshes;

            int matched = 0;

            for (unsigned int n = 0;
                 n < candidateCount;
                 n++)
            {
                const unsigned int meshIndex =
                    node ? node->mMeshes[n] : n;

                if (meshIndex >= scene->mNumMeshes)
                    continue;

                const struct aiMesh* mesh =
                    scene->mMeshes[meshIndex];

                if (!mesh || mesh->mNumAnimMeshes == 0)
                    continue;

                if (mesh->mNumAnimMeshes > INT_MAX)
                    goto fail;

                // Fallback matches the mesh name exactly.
                if (!node &&
                    strcmp(
                        mesh->mName.data,
                        source->mName.data) != 0)
                {
                    continue;
                }

                // A single R3D mesh index can only have
                // one morph channel in one animation clip.
                bool duplicate = false;

                for (int existing = 0;
                     existing < animation->morphChannelCount;
                     existing++)
                {
                    if (animation->morphChannels[existing]
                            .meshIndex == (int)meshIndex)
                    {
                        duplicate = true;
                        break;
                    }
                }

                if (duplicate)
                {
                    R3D_TRACELOG(
                        LOG_WARNING,
                        "Duplicate morph channel for mesh %u in animation '%s'",
                        meshIndex,
                        animation->name
                    );
                    continue;
                }

                R3D_MorphAnimationChannel* channel =
                    &animation->morphChannels[
                        animation->morphChannelCount
                    ];

                // Count the channel before loading so
                // partial allocations are cleaned on error.
                animation->morphChannelCount++;

                if (!load_morph_channel(
                    channel,
                    source,
                    (int)meshIndex,
                    (int)mesh->mNumAnimMeshes))
                {
                    goto fail;
                }

                matched++;
            }

            if (matched == 0)
            {
                R3D_TRACELOG(
                    LOG_WARNING,
                    "Morph channel '%s' could not be mapped to a unique imported mesh",
                    source->mName.data
                );
            }
        }
    }

    // A morph-only clip is perfectly valid.
    if (animation->channelCount == 0 &&
        animation->morphChannelCount == 0)
    {
        goto fail;
    }

    R3D_TRACELOG(
        LOG_INFO,
        "Animation '%s' imported: %d skeletal channels, %d morph channels",
        animation->name,
        animation->channelCount,
        animation->morphChannelCount
    );

    return true;

fail:
    free_imported_animation(animation);
    return false;
}

// ========================================
// PUBLIC FUNCTIONS
// ========================================

bool r3d_importer_load_animations(const R3D_Importer* importer, R3D_AnimationLib* animLib)
{
    if (!importer || !r3d_importer_is_valid(importer))
    {
        R3D_TRACELOG(LOG_ERROR, "Invalid importer for animation loading");
        return false;
    }

    int animCount = r3d_importer_get_animation_count(importer);
    if (animCount == 0)
    {
        R3D_TRACELOG(LOG_WARNING, "No animations found in the imported scene");
        return false;
    }

    // Allocate temporary animations array
    R3D_Animation* animations = r3d_zalloc(animCount * sizeof(R3D_Animation));

    // Load each animation
    int successCount = 0;
    for (int i = 0; i < animCount; i++)
    {
        const struct aiAnimation* aiAnim = r3d_importer_get_animation(importer, i);
        if (load_animation(&animations[successCount], importer, aiAnim))
        {
            successCount++;
        }
        else
        {
            R3D_TRACELOG(LOG_ERROR, "Failed to process animation %d", i);
        }
    }

    if (successCount == 0)
    {
        R3D_TRACELOG(LOG_ERROR, "No animations were successfully loaded");
        r3d_free(animations);
        return false;
    }

    // Resize if some animations failed
    if (successCount < animCount)
    {
        R3D_TRACELOG(LOG_WARNING, "Only %d out of %d animations were successfully loaded", successCount, animCount);
        animations = r3d_realloc(animations, successCount * sizeof(R3D_Animation));
    }

    animLib->animations = animations;
    animLib->count = successCount;

    return true;
}
