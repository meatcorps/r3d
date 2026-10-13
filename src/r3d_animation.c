/* r3d_animation.c -- R3D Animation Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_animation.h>
#include <r3d_config.h>
#include <raymath.h>
#include <string.h>
#include <math.h>
#include <glad.h>

#include "./common/r3d_helper.h"

#ifdef R3D_SUPPORT_ASSIMP
#   include "./importer/r3d_importer_internal.h"
#endif

// ========================================
// PUBLIC API
// ========================================

R3D_AnimationLib R3D_LoadAnimationLib(const char* filePath)
{
    R3D_AnimationLib animLib = {0};

#ifdef R3D_SUPPORT_ASSIMP
    R3D_Importer* importer = R3D_LoadImporter(filePath, 0);
    if (importer == NULL) return (R3D_AnimationLib) {0};

    animLib = R3D_LoadAnimationLibFromImporter(importer);
    R3D_UnloadImporter(importer);

#else
    R3D_TRACELOG(LOG_WARNING, "Cannot load '%s': built without Assimp support", filePath);

#endif // R3D_SUPPORT_ASSIMP

    return animLib;
}

R3D_AnimationLib R3D_LoadAnimationLibFromMemory(const void* data, unsigned int size, const char* hint)
{
    R3D_AnimationLib animLib = {0};

#ifdef R3D_SUPPORT_ASSIMP
    R3D_Importer* importer = R3D_LoadImporterFromMemory(data, size, hint, 0);
    if (importer == NULL) return (R3D_AnimationLib) {0};

    animLib = R3D_LoadAnimationLibFromImporter(importer);
    R3D_UnloadImporter(importer);

#else
    if (hint && hint[0] != '\0') {
        R3D_TRACELOG(LOG_WARNING, "Cannot load '%s' from memory: built without Assimp support", hint);
    }
    else {
        R3D_TRACELOG(LOG_WARNING, "Cannot load asset from memory: built without Assimp support");
    }

#endif // R3D_SUPPORT_ASSIMP

    return animLib;
}

R3D_AnimationLib R3D_LoadAnimationLibFromImporter(const R3D_Importer* importer)
{
    R3D_AnimationLib animLib = {0};

#ifdef R3D_SUPPORT_ASSIMP
    if (!importer)
    {
        R3D_TRACELOG(LOG_WARNING, "Cannot load animation library from importer: NULL importer");
        return animLib;
    }

    if (r3d_importer_load_animations(importer, &animLib))
    {
        R3D_TRACELOG(LOG_INFO, "Animation library loaded successfully (%d animations): '%s'", animLib.count, importer->name);
    }
    else
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to load animation library: '%s'", importer->name);
    }

#else
    R3D_TRACELOG(LOG_WARNING, "Cannot load animation library from importer: built without Assimp support");

#endif // R3D_SUPPORT_ASSIMP

    return animLib;
}

void R3D_UnloadAnimationLib(R3D_AnimationLib animLib)
{
    if (!animLib.animations) return;

    for (int i = 0; i < animLib.count; ++i)
    {
        R3D_Animation* anim = &animLib.animations[i];

        for (int j = 0; j < anim->channelCount; ++j)
        {
            R3D_AnimationChannel* channel = &anim->channels[j];

            r3d_free((void*)channel->translation.times);
            r3d_free((void*)channel->translation.values);

            r3d_free((void*)channel->rotation.times);
            r3d_free((void*)channel->rotation.values);

            r3d_free((void*)channel->scale.times);
            r3d_free((void*)channel->scale.values);
        }

        r3d_free(anim->channels);

        for (int m = 0; m < anim->morphChannelCount; m++)
        {
            R3D_MorphAnimationChannel* channel =
                &anim->morphChannels[m];

            r3d_free(channel->times);
            r3d_free(channel->weights);
        }

        r3d_free(anim->morphChannels);
    }

    r3d_free(animLib.animations);
}


const R3D_MorphAnimationChannel*
R3D_GetMorphAnimationChannel(
    const R3D_Animation* animation,
    int meshIndex)
{
    if (!animation || meshIndex < 0)
        return NULL;

    for (int i = 0; i < animation->morphChannelCount; i++)
    {
        const R3D_MorphAnimationChannel* channel =
            &animation->morphChannels[i];

        if (channel->meshIndex == meshIndex)
            return channel;
    }

    return NULL;
}

bool R3D_SampleMorphAnimationWeight(
    const R3D_Animation* animation,
    int meshIndex,
    int targetIndex,
    float timeTicks,
    float defaultWeight,
    float* result)
{
    if (!animation || !result ||
        meshIndex < 0 || targetIndex < 0 ||
        !isfinite(timeTicks) ||
        !isfinite(defaultWeight))
    {
        return false;
    }

    const R3D_MorphAnimationChannel* channel =
        R3D_GetMorphAnimationChannel(
            animation, meshIndex
        );

    // This clip does not animate this mesh.
    if (!channel)
    {
        *result = defaultWeight;
        return true;
    }

    if (targetIndex >= channel->targetCount ||
        channel->keyCount <= 0 ||
        !channel->times ||
        !channel->weights)
    {
        return false;
    }

    const int count = channel->keyCount;
    const int stride = channel->targetCount;

    // Clamp before the first keyframe.
    if (count == 1 || timeTicks <= channel->times[0])
    {
        *result = channel->weights[targetIndex];
        return true;
    }

    // Clamp after the last keyframe.
    if (timeTicks >= channel->times[count - 1])
    {
        *result = channel->weights[
            (size_t)(count - 1) * stride + targetIndex
        ];
        return true;
    }

    // Binary search between two keyframes.
    int lo = 0;
    int hi = count - 1;

    while (lo + 1 < hi)
    {
        int mid = lo + (hi - lo) / 2;

        if (channel->times[mid] <= timeTicks)
            lo = mid;
        else
            hi = mid;
    }

    float t0 = channel->times[lo];
    float t1 = channel->times[hi];

    float alpha = t1 > t0
        ? (timeTicks - t0) / (t1 - t0)
        : 0.0f;

    float w0 = channel->weights[
        (size_t)lo * stride + targetIndex
    ];

    float w1 = channel->weights[
        (size_t)hi * stride + targetIndex
    ];

    *result = w0 + (w1 - w0) * alpha;

    return isfinite(*result);
}

bool R3D_EvaluateMorphAnimation(const R3D_Animation* animation, int meshIndex, float timeTicks, R3D_ShapeKeyInstance* instance)
{
    if (!instance ||
        !instance->source ||
        !instance->weights ||
        !isfinite(timeTicks))
    {
        return false;
    }

    const R3D_MorphAnimationChannel* channel =
        R3D_GetMorphAnimationChannel(
            animation, meshIndex
        );

    if (!channel)
        return false;

    const int targetCount = instance->source->keyCount;

    // Reject incompatible model/animation combinations.
    if (channel->targetCount != targetCount)
        return false;

    for (int i = 0; i < targetCount; i++)
    {
        float weight = 0.0f;

        if (!R3D_SampleMorphAnimationWeight(
            animation,
            meshIndex,
            i,
            timeTicks,
            instance->source->keys[i].defaultWeight,
            &weight))
        {
            return false;
        }

        if (!R3D_SetShapeKeyWeight(instance, i, weight))
            return false;
    }

    return true;
}

int R3D_GetAnimationIndex(R3D_AnimationLib animLib, const char* name)
{
    for (int i = 0; i < animLib.count; i++)
    {
        if (strcmp(animLib.animations[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

R3D_Animation* R3D_GetAnimation(R3D_AnimationLib animLib, const char* name)
{
    int index = R3D_GetAnimationIndex(animLib, name);
    if (index < 0) return NULL;

    return &animLib.animations[index];
}
