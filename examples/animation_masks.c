#include <r3d/r3d.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool mask_contains(const R3D_BoneMask* mask, int boneIdx)
{
    if (boneIdx < 0 || boneIdx >= mask->boneCount || boneIdx >= 256) return false;

    return (mask->mask[boneIdx / 32] & (UINT32_C(1) << (boneIdx % 32))) != 0;
}

static int check_mask(const char* name, bool condition)
{
    printf("%s: %s\n", name, condition ? "PASS" : "FAIL");
    return condition ? 0 : 1;
}

int main(void)
{
    R3D_AnimationChannel channels[] = {
        {.boneIndex = 0,   .translation = {.count = 2}},
        {.boneIndex = 31,  .rotation = {.count = 1}},
        {.boneIndex = 32,  .scale = {.count = 2}},
        {.boneIndex = 63},
        {.boneIndex = 255, .translation = {.count = 1}},
        {.boneIndex = 32,  .rotation = {.count = 1}}
    };

    R3D_Animation animation = {
        .channels = channels,
        .channelCount = 6,
        .boneCount = 256
    };

    R3D_BoneMask mask = R3D_ComputeAnimationBoneMask(&animation);

    int failures = 0;

    failures += check_mask("Bone count", mask.boneCount == 256);
    failures += check_mask("First bone", mask_contains(&mask, 0));
    failures += check_mask("Bit 31", mask_contains(&mask, 31));
    failures += check_mask("Bit 32", mask_contains(&mask, 32));
    failures += check_mask("Last bone", mask_contains(&mask, 255));

    failures += check_mask("Unanimated bone excluded", !mask_contains(&mask, 1));
    failures += check_mask("Empty channel excluded", !mask_contains(&mask, 63));

    // Duplicate channel entries must not cause problems.
    failures += check_mask("Duplicate bone channel", mask_contains(&mask, 32));

    // Verify existing name-based masks still handle high bits correctly.
    R3D_BoneInfo bones[256] = {0};

    strcpy(bones[31].name, "bone31");
    strcpy(bones[32].name, "bone32");
    strcpy(bones[255].name, "bone255");

    R3D_Skeleton skeleton = {
        .bones = bones,
        .boneCount = 256
    };

    const char* names[] = {"bone31", "bone32", "bone255"};
    R3D_BoneMask namedMask = R3D_ComputeBoneMask(&skeleton, names, 3);

    failures += check_mask("Manual mask high bits",
        mask_contains(&namedMask, 31) &&
        mask_contains(&namedMask, 32) &&
        mask_contains(&namedMask, 255));

    // Invalid bone index should produce an invalid mask.
    R3D_AnimationChannel invalidChannel = {
        .boneIndex = 256,
        .translation = {.count = 1}
    };

    animation.channels = &invalidChannel;
    animation.channelCount = 1;

    R3D_BoneMask invalidMask = R3D_ComputeAnimationBoneMask(&animation);
    failures += check_mask("Invalid bone index rejected", invalidMask.boneCount == 0);

    // More than 256 bones is unsupported by the current mask format.
    animation.boneCount = 257;

    invalidMask = R3D_ComputeAnimationBoneMask(&animation);
    failures += check_mask("Oversized skeleton rejected", invalidMask.boneCount == 0);

    // An animation with no channels should produce a valid empty mask.
    animation.boneCount = 256;
    animation.channelCount = 0;
    animation.channels = NULL;

    R3D_BoneMask emptyMask = R3D_ComputeAnimationBoneMask(&animation);

    failures += check_mask("Empty animation", emptyMask.boneCount == 256 &&
        !mask_contains(&emptyMask, 0) &&
        !mask_contains(&emptyMask, 255));

    printf("\nAnimation mask tests: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}