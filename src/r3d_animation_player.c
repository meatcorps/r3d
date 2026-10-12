/* r3d_animation_player.c -- R3D Animation Player Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_animation_player.h>
#include <r3d_config.h>
#include <raymath.h>
#include <glad.h>
#include <math.h>

#include "./common/r3d_helper.h"
#include "./common/r3d_anim.h"

// ========================================
// INTERNAL FUNCTIONS DECLARATIONS
// ========================================

static void emit_event(R3D_AnimationPlayer* player, R3D_AnimationEvent event, int animIndex);
static bool is_anim_index_valid(R3D_AnimationPlayer* player, int animIndex);
static void reset_anim_time(R3D_AnimationPlayer* player, int animIndex);
static void compute_local_matrices(R3D_AnimationPlayer* player);

// ========================================
// PUBLIC API
// ========================================

R3D_AnimationPlayer R3D_LoadAnimationPlayer(R3D_Skeleton skeleton, R3D_AnimationLib animLib)
{
    R3D_AnimationPlayer player = {0};

    player.skeleton = skeleton;
    player.animLib  = animLib;

    // Allocate the required memory
    player.states     = r3d_zalloc(animLib.count * sizeof(*player.states));
    player.localPose  = r3d_zalloc(skeleton.boneCount * sizeof(*player.localPose));
    player.modelPose  = r3d_zalloc(skeleton.boneCount * sizeof(*player.modelPose));
    player.skinBuffer = r3d_zalloc(skeleton.boneCount * sizeof(*player.skinBuffer));

    // Initialize animation states
    for (int i = 0; i < animLib.count; i++)
    {
        player.states[i] = (R3D_AnimationState) {
            .currentTime = 0.0f,
            .speed       = 1.0f,
            .play        = false,
            .loop        = false
        };
    }
    player.activeAnimIndex = -1;

    // Load default poses for each space
    memcpy(player.localPose, player.skeleton.localBind, player.skeleton.boneCount * sizeof(Matrix));
    memcpy(player.modelPose, player.skeleton.modelBind, player.skeleton.boneCount * sizeof(Matrix));
    for (int i = 0; i < player.skeleton.boneCount; i++)
    {
        player.skinBuffer[i] = MatrixMultiply(player.skeleton.invBind[i], player.modelPose[i]);
    }

    // Generate the skinning texture then uploading the computed matrices
    glGenTextures(1, &player.skinTexture);
    glBindTexture(GL_TEXTURE_1D, player.skinTexture);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA16F, 4 * skeleton.boneCount, 0, GL_RGBA, GL_FLOAT, player.skinBuffer);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_1D, 0);

    return player;
}

void R3D_UnloadAnimationPlayer(R3D_AnimationPlayer player)
{
    if (player.skinTexture > 0)
    {
        glDeleteTextures(1, &player.skinTexture);
    }

    r3d_free(player.skinBuffer);
    r3d_free(player.modelPose);
    r3d_free(player.localPose);
    r3d_free(player.states);
}

bool R3D_IsAnimationPlayerValid(R3D_AnimationPlayer player)
{
    return (player.skinTexture > 0);
}

bool R3D_IsAnimationPlaying(R3D_AnimationPlayer player)
{
    if (is_anim_index_valid(&player, player.activeAnimIndex))
    {
        return player.states[player.activeAnimIndex].play;
    }
    return false;
}

void R3D_PlayAnimation(R3D_AnimationPlayer* player, int animIndex)
{
    if (!is_anim_index_valid(player, animIndex))
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to play animation %i; Animation index invalid", animIndex);
        return;
    }

    if (is_anim_index_valid(player, player->activeAnimIndex))
    {
        player->states[player->activeAnimIndex].play = false;
    }

    player->states[animIndex].play = true;
    player->activeAnimIndex = animIndex;
}

void R3D_PauseAnimation(R3D_AnimationPlayer* player)
{
    if (is_anim_index_valid(player, player->activeAnimIndex))
    {
        player->states[player->activeAnimIndex].play = false;
    }
}

void R3D_StopAnimation(R3D_AnimationPlayer* player)
{
    if (is_anim_index_valid(player, player->activeAnimIndex))
    {
        player->states[player->activeAnimIndex].play = false;
        reset_anim_time(player, player->activeAnimIndex);
    }
}

void R3D_RewindAnimation(R3D_AnimationPlayer* player)
{
    if (is_anim_index_valid(player, player->activeAnimIndex))
    {
        reset_anim_time(player, player->activeAnimIndex);
    }
}

float R3D_GetAnimationTime(R3D_AnimationPlayer player, int animIndex)
{
    if (is_anim_index_valid(&player, animIndex))
    {
        return player.states[animIndex].currentTime;
    }
    return 0.0f;
}

void R3D_SetAnimationTime(R3D_AnimationPlayer* player, int animIndex, float time)
{
    if (!is_anim_index_valid(player, animIndex) || !isfinite(time)) return;

    float duration = R3D_GetAnimationDurationSeconds(*player, animIndex);
    if (duration <= 0.0f)
    {
        player->states[animIndex].currentTime = 0.0f;
        return;
    }

    player->states[animIndex].currentTime = Wrap(time, 0.0f, duration);
}

float R3D_GetAnimationTimeSeconds(R3D_AnimationPlayer player, int animIndex)
{
    return R3D_GetAnimationTime(player, animIndex);
}

float R3D_GetAnimationDurationSeconds(R3D_AnimationPlayer player, int animIndex)
{
    if (!is_anim_index_valid(&player, animIndex)) return 0.0f;

    const R3D_Animation* anim = &player.animLib.animations[animIndex];

    if (!isfinite(anim->duration) || !isfinite(anim->ticksPerSecond)) return 0.0f;
    if (anim->duration <= 0.0f || anim->ticksPerSecond <= 0.0f) return 0.0f;

    return anim->duration / anim->ticksPerSecond;
}

float R3D_GetAnimationNormalizedTime(R3D_AnimationPlayer player, int animIndex)
{
    float duration = R3D_GetAnimationDurationSeconds(player, animIndex);
    if (duration <= 0.0f) return 0.0f;

    float time = R3D_GetAnimationTimeSeconds(player, animIndex);
    return Clamp(time / duration, 0.0f, 1.0f);
}

void R3D_SetAnimationTimeSeconds(R3D_AnimationPlayer* player, int animIndex, float timeSeconds)
{
    if (!is_anim_index_valid(player, animIndex) || !isfinite(timeSeconds)) return;

    float duration = R3D_GetAnimationDurationSeconds(*player, animIndex);
    player->states[animIndex].currentTime = Clamp(timeSeconds, 0.0f, duration);
}

float R3D_GetAnimationSpeed(R3D_AnimationPlayer player, int animIndex)
{
    if (is_anim_index_valid(&player, animIndex))
    {
        return player.states[animIndex].speed;
    }
    return 0.0f;
}

void R3D_SetAnimationSpeed(R3D_AnimationPlayer* player, int animIndex, float speed)
{
    if (is_anim_index_valid(player, animIndex))
    {
        player->states[animIndex].speed = speed;
    }
}

bool R3D_GetAnimationLoop(R3D_AnimationPlayer player, int animIndex)
{
    if (is_anim_index_valid(&player, animIndex))
    {
        return player.states[animIndex].loop;
    }
    return false;
}

void R3D_SetAnimationLoop(R3D_AnimationPlayer* player, int animIndex, bool loop)
{
    if (is_anim_index_valid(player, animIndex))
    {
        player->states[animIndex].loop = loop;
    }
}

void R3D_AdvanceAnimationTime(R3D_AnimationPlayer* player, float dt)
{
    if (!player || !isfinite(dt) || dt <= 0.0f) return;

    int animIndex = player->activeAnimIndex;
    if (!is_anim_index_valid(player, animIndex)) return;

    R3D_AnimationState* state = &player->states[animIndex];
    if (!state->play || !isfinite(state->speed) || state->speed == 0.0f) return;

    float duration = R3D_GetAnimationDurationSeconds(*player, animIndex);
    if (duration <= 0.0f)
    {
        state->currentTime = 0.0f;
        state->play = false;
        emit_event(player, R3D_ANIM_EVENT_FINISHED, animIndex);
        return;
    }

    double nextTime = (double)state->currentTime + (double)state->speed * dt;

    bool crossed = (state->speed > 0.0f && nextTime >= duration) ||
                   (state->speed < 0.0f && nextTime <= 0.0);

    if (!crossed)
    {
        state->currentTime = (float)nextTime;
        return;
    }

    if (!state->loop)
    {
        state->currentTime = Clamp((float)nextTime, 0.0f, duration);
        state->play = false;
        emit_event(player, R3D_ANIM_EVENT_FINISHED, animIndex);
        return;
    }

    double wrappedTime = fmod(nextTime, (double)duration);
    if (wrappedTime < 0.0) wrappedTime += duration;

    // Reverse playback wraps to the end at an exact boundary.
    if (state->speed < 0.0f && wrappedTime == 0.0) wrappedTime = duration;

    state->currentTime = (float)wrappedTime;
    emit_event(player, R3D_ANIM_EVENT_LOOPED, animIndex);
}

static bool is_finite_affine_matrix(Matrix matrix)
{
    float16 values = MatrixToFloatV(matrix);

    for (int i = 0; i < 16; i++)
    {
        if (!isfinite(values.v[i])) return false;
    }

    const float epsilon = 1e-5f;

    return fabsf(matrix.m3) <= epsilon &&
           fabsf(matrix.m7) <= epsilon &&
           fabsf(matrix.m11) <= epsilon &&
           fabsf(matrix.m15 - 1.0f) <= epsilon;
}

bool R3D_GetAnimationBoneLocalMatrix(const R3D_AnimationPlayer* player, int boneIndex, Matrix* outMatrix)
{
    if (!player || !outMatrix || !player->localPose) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;

    *outMatrix = player->localPose[boneIndex];
    return true;
}

bool R3D_SetAnimationBoneLocalMatrix(R3D_AnimationPlayer* player, int boneIndex, Matrix matrix)
{
    if (!player || !player->localPose) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;
    if (!is_finite_affine_matrix(matrix)) return false;

    player->localPose[boneIndex] = matrix;
    return true;
}

bool R3D_SetAnimationBoneModelMatrix(R3D_AnimationPlayer* player, int boneIndex, Matrix matrix)
{
    if (!player || !player->localPose || !player->modelPose || !player->skeleton.bones) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;
    if (!is_finite_affine_matrix(matrix)) return false;

    const int parentIndex = player->skeleton.bones[boneIndex].parent;

    if (parentIndex < -1 || parentIndex >= player->skeleton.boneCount) return false;
    if (parentIndex == boneIndex) return false;

    Matrix parentPose = parentIndex >= 0 ? player->modelPose[parentIndex] : player->skeleton.rootBind;

    if (!is_finite_affine_matrix(parentPose)) return false;

    const float determinant = MatrixDeterminant(parentPose);
    if (!isfinite(determinant) || fabsf(determinant) <= 1e-8f) return false;

    Matrix local = MatrixMultiply(matrix, MatrixInvert(parentPose));

    if (!is_finite_affine_matrix(local)) return false;

    player->localPose[boneIndex] = local;
    return true;
}

void R3D_ComputeAnimationLocalPose(R3D_AnimationPlayer* player)
{
    if (is_anim_index_valid(player, player->activeAnimIndex)) compute_local_matrices(player);
    else memcpy(player->localPose, player->skeleton.localBind, player->skeleton.boneCount * sizeof(Matrix));
}

void R3D_ComputeAnimationModelPose(R3D_AnimationPlayer* player)
{
    if (is_anim_index_valid(player, player->activeAnimIndex)) R3D_RecomputeAnimationModelPose(player);
    else memcpy(player->modelPose, player->skeleton.modelBind, player->skeleton.boneCount * sizeof(Matrix));
}

bool R3D_RecomputeAnimationModelPose(R3D_AnimationPlayer* player)
{
    return r3d_anim_matrices_compute(player, -1);
}

bool R3D_RecomputeAnimationModelPoseFromBone(R3D_AnimationPlayer* player, int boneIndex)
{
    if (!player) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;

    return r3d_anim_matrices_compute(player, boneIndex);
}

void R3D_ComputeAnimationPose(R3D_AnimationPlayer* player)
{
    if (is_anim_index_valid(player, player->activeAnimIndex))
    {
        compute_local_matrices(player);
        R3D_RecomputeAnimationModelPose(player);
    }
    else
    {
        memcpy(player->localPose, player->skeleton.localBind, player->skeleton.boneCount * sizeof(Matrix));
        memcpy(player->modelPose, player->skeleton.modelBind, player->skeleton.boneCount * sizeof(Matrix));
    }
}

void R3D_UploadAnimationPose(R3D_AnimationPlayer* player)
{
    if (!player || player->skeleton.boneCount <= 0 ||
        !player->modelPose || !player->skinBuffer ||
        !player->skeleton.invBind || player->skinTexture == 0)
    {
        return;
    }

    const int boneCount = player->skeleton.boneCount;

    for (int i = 0; i < boneCount; i++)
    {
        player->skinBuffer[i] = MatrixMultiply(player->skeleton.invBind[i], player->modelPose[i]);
    }

    // Preserve the 1D texture binding on the currently active texture unit.
    GLint previousTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_1D, &previousTexture);

    glBindTexture(GL_TEXTURE_1D, player->skinTexture);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 4 * boneCount, GL_RGBA, GL_FLOAT, player->skinBuffer);
    glBindTexture(GL_TEXTURE_1D, (GLuint)previousTexture);
}

bool R3D_GetAnimationBoneLocalTransform(const R3D_AnimationPlayer* player, int boneIndex, Transform* outTransform)
{
    if (!player || !outTransform || !player->localPose) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;

    r3d_matrix_decompose(player->localPose[boneIndex], &outTransform->translation, &outTransform->rotation, &outTransform->scale);
    return true;
}

bool R3D_SetAnimationBoneLocalTransform(R3D_AnimationPlayer* player, int boneIndex, Transform transform)
{
    if (!player || !player->localPose) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;

    if (!isfinite(transform.translation.x) || !isfinite(transform.translation.y) || !isfinite(transform.translation.z) ||
        !isfinite(transform.rotation.x) || !isfinite(transform.rotation.y) || !isfinite(transform.rotation.z) || !isfinite(transform.rotation.w) ||
        !isfinite(transform.scale.x) || !isfinite(transform.scale.y) || !isfinite(transform.scale.z))
    {
        return false;
    }

    player->localPose[boneIndex] = r3d_matrix_srt_quat(transform.scale, transform.rotation, transform.translation);
    return true;
}

bool R3D_GetAnimationBoneModelTransform(const R3D_AnimationPlayer* player, int boneIndex, Transform* outTransform)
{
    if (!player || !outTransform || !player->modelPose) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;

    r3d_matrix_decompose(player->modelPose[boneIndex], &outTransform->translation, &outTransform->rotation, &outTransform->scale);
    return true;
}

bool R3D_GetAnimationBoneModelMatrix(const R3D_AnimationPlayer* player, int boneIndex, Matrix* outMatrix)
{
    if (!player || !outMatrix || !player->modelPose) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;

    *outMatrix = player->modelPose[boneIndex];
    return true;
}

bool R3D_SetAnimationBoneModelTransform(R3D_AnimationPlayer* player, int boneIndex, Transform transform)
{
    if (!isfinite(transform.translation.x) || !isfinite(transform.translation.y) || !isfinite(transform.translation.z) ||
        !isfinite(transform.rotation.x) || !isfinite(transform.rotation.y) || !isfinite(transform.rotation.z) || !isfinite(transform.rotation.w) ||
        !isfinite(transform.scale.x) || !isfinite(transform.scale.y) || !isfinite(transform.scale.z))
    {
        return false;
    }

    Matrix matrix = r3d_matrix_srt_quat(transform.scale, transform.rotation, transform.translation);

    return R3D_SetAnimationBoneModelMatrix(player, boneIndex, matrix);
}

void R3D_UpdateAnimationPlayer(R3D_AnimationPlayer* player, float dt)
{
    R3D_AdvanceAnimationTime(player, dt);
    R3D_ComputeAnimationPose(player);
    R3D_UploadAnimationPose(player);
}

// ========================================
// INTERNAL FUNCTIONS DEFINITIONS
// ========================================

void emit_event(R3D_AnimationPlayer* player, R3D_AnimationEvent event, int animIndex)
{
    if (player->eventCallback != NULL)
    {
        player->eventCallback(player, event, animIndex, player->eventUserData);
    }
}

bool is_anim_index_valid(R3D_AnimationPlayer* player, int animIndex)
{
    return animIndex >= 0 && animIndex < player->animLib.count;
}

void reset_anim_time(R3D_AnimationPlayer* player, int animIndex)
{
    R3D_ASSERT(is_anim_index_valid(player, animIndex));

    R3D_AnimationState* state = &player->states[animIndex];
    float duration = R3D_GetAnimationDurationSeconds(*player, animIndex);

    state->currentTime = (state->speed >= 0.0f) ? 0.0f : duration;
}

void compute_local_matrices(R3D_AnimationPlayer* player)
{
    R3D_ASSERT(is_anim_index_valid(player, player->activeAnimIndex));

    const R3D_AnimationState* state = &player->states[player->activeAnimIndex];
    const R3D_Animation* anim = &player->animLib.animations[player->activeAnimIndex];

    float tick = state->currentTime * anim->ticksPerSecond;
    int boneCount = player->skeleton.boneCount;
    Matrix* localPose = player->localPose;

    for (int iBone = 0; iBone < boneCount; iBone++)
    {
        const R3D_AnimationChannel* channel = r3d_anim_channel_find(anim, iBone);
        if (channel == NULL)
        {
            localPose[iBone] = player->skeleton.localBind[iBone];
            continue;
        }
        Transform local = r3d_anim_channel_lerp(channel, tick, &player->skeleton.localBind[iBone], NULL, NULL);
        localPose[iBone] = r3d_matrix_srt_quat(local.scale, QuaternionNormalize(local.rotation), local.translation);
    }
}
