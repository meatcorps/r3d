#include <r3d/r3d.h>
#include <raymath.h>
#include "../src/common/r3d_math.h"
#include <stdio.h>

static Matrix make_expected_pose(Vector3 translation, Quaternion rotation, Vector3 scale)
{
    Matrix result = QuaternionToMatrix(QuaternionNormalize(rotation));

    // Apply scale to the rotation matrix columns.
    result.m0 *= scale.x;
    result.m1 *= scale.x;
    result.m2 *= scale.x;

    result.m4 *= scale.y;
    result.m5 *= scale.y;
    result.m6 *= scale.y;

    result.m8 *= scale.z;
    result.m9 *= scale.z;
    result.m10 *= scale.z;

    result.m12 = translation.x;
    result.m13 = translation.y;
    result.m14 = translation.z;

    return result;
}

static int check_pose(const char* name, Matrix matrix, Vector3 expectedTranslation, Quaternion expectedRotation, Vector3 expectedScale)
{
    Matrix expected = make_expected_pose(expectedTranslation, expectedRotation, expectedScale);
    bool passed = R3D_MatrixEquals(matrix, expected);

    printf("%s: %s\n", name, passed ? "PASS" : "FAIL");

    return passed ? 0 : 1;
}

int main(void)
{
    // Nonidentity bind pose: translated, rotated, and scaled.
    Matrix bindPose = make_expected_pose(
        (Vector3){3.0f, 4.0f, 5.0f},
        QuaternionFromAxisAngle((Vector3){0.0f, 0.0f, 1.0f}, 0.4f),
        (Vector3){2.0f, 3.0f, 4.0f}
    );
    bindPose.m12 = 3.0f;
    bindPose.m13 = 4.0f;
    bindPose.m14 = 5.0f;

    Vector3 bindTranslation;
    Vector3 bindScale;
    Quaternion bindRotation;

    r3d_matrix_decompose(bindPose, &bindTranslation, &bindRotation, &bindScale);

    float keyTime = 0.0f;
    Vector3 animatedTranslation = {8.0f, 9.0f, 10.0f};
    Vector3 animatedScale = {5.0f, 6.0f, 7.0f};
    Quaternion animatedRotation = QuaternionFromAxisAngle((Vector3){1.0f, 0.0f, 0.0f}, 0.75f);

    R3D_AnimationChannel channel = {
        .boneIndex = 0
    };

    R3D_Animation animation = {
        .channels = &channel,
        .channelCount = 1,
        .ticksPerSecond = 1.0f,
        .duration = 1.0f,
        .boneCount = 1
    };

    R3D_AnimationLib library = {
        .animations = &animation,
        .count = 1
    };

    R3D_AnimationState state = {
        .currentTime = 0.0f,
        .speed = 1.0f
    };

    R3D_Skeleton skeleton = {
        .localBind = &bindPose,
        .boneCount = 1
    };

    Matrix sampledPose = MatrixIdentity();

    R3D_AnimationPlayer player = {
        .skeleton = skeleton,
        .animLib = library,
        .states = &state,
        .activeAnimIndex = 0,
        .localPose = &sampledPose
    };

    int failures = 0;

    // Translation-only: rotation and scale must come from the bind pose.
    channel.translation = (R3D_AnimationTrack){.times = &keyTime, .values = &animatedTranslation, .count = 1};
    R3D_ComputeAnimationLocalPose(&player);
    failures += check_pose("Translation-only", sampledPose, animatedTranslation, bindRotation, bindScale);

    // Rotation-only: translation and scale must come from the bind pose.
    channel.translation = (R3D_AnimationTrack){0};
    channel.rotation = (R3D_AnimationTrack){.times = &keyTime, .values = &animatedRotation, .count = 1};
    R3D_ComputeAnimationLocalPose(&player);
    failures += check_pose("Rotation-only", sampledPose, bindTranslation, animatedRotation, bindScale);

    // Scale-only: translation and rotation must come from the bind pose.
    channel.rotation = (R3D_AnimationTrack){0};
    channel.scale = (R3D_AnimationTrack){.times = &keyTime, .values = &animatedScale, .count = 1};
    R3D_ComputeAnimationLocalPose(&player);
    failures += check_pose("Scale-only", sampledPose, bindTranslation, bindRotation, animatedScale);

    // Existing channel with no tracks should resolve to the bind pose.
    channel.scale = (R3D_AnimationTrack){0};
    R3D_ComputeAnimationLocalPose(&player);
    failures += check_pose("Empty channel", sampledPose, bindTranslation, bindRotation, bindScale);

    // Completely missing channel should preserve the original bind matrix.
    animation.channelCount = 0;
    R3D_ComputeAnimationLocalPose(&player);

    bool missingChannelPassed = R3D_MatrixEquals(sampledPose, bindPose);
    printf("Missing channel: %s\n", missingChannelPassed ? "PASS" : "FAIL");
    if (!missingChannelPassed) failures++;

    printf("\nSparse animation tests: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}