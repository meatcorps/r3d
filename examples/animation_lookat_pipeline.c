#include <r3d/r3d_animation_tree.h>
#include <r3d/r3d_animation_lookat.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

static int check(const char* name, bool condition)
{
    printf("%s: %s\n", name, condition ? "PASS" : "FAIL");
    return condition ? 0 : 1;
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

static bool near_position(Matrix matrix, float x, float y, float z)
{
    return near(matrix.m12, x) &&
           near(matrix.m13, y) &&
           near(matrix.m14, z);
}

static Vector3 bone_direction(Matrix matrix, Vector3 axis)
{
    Vector3 origin = Vector3Transform(Vector3Zero(), matrix);
    Vector3 end = Vector3Transform(axis, matrix);

    return Vector3Normalize(Vector3Subtract(end, origin));
}

static bool aimed_at(Matrix matrix, Vector3 axis, Vector3 target)
{
    Vector3 origin = {matrix.m12, matrix.m13, matrix.m14};
    Vector3 expected = Vector3Normalize(Vector3Subtract(target, origin));

    return Vector3DotProduct(bone_direction(matrix, axis), expected) > 0.999f;
}

int main(void)
{
    // Deliberately unordered skeleton:
    //
    // Root (3)
    //   Head (0)
    //   Weapon (1)
    //     Muzzle (2)
    //
    // Root is animated.
    // Head receives LookAt.
    // Weapon receives Aim.
    // Muzzle represents a future post-aim procedural/IK stage.

    R3D_BoneInfo bones[4] = {
        {.name = "Head", .parent = 3},
        {.name = "Weapon", .parent = 3},
        {.name = "Muzzle", .parent = 1},
        {.name = "Root", .parent = -1}
    };

    Matrix localBind[4] = {
        MatrixTranslate(0.0f, 1.5f, 0.0f),
        MatrixTranslate(0.5f, 1.0f, 0.0f),
        MatrixTranslate(0.0f, 0.0f, 1.0f),
        MatrixIdentity()
    };

    Matrix modelBind[4] = {
        MatrixTranslate(0.0f, 1.5f, 0.0f),
        MatrixTranslate(0.5f, 1.0f, 0.0f),
        MatrixTranslate(0.5f, 1.0f, 1.0f),
        MatrixIdentity()
    };

    Matrix localPose[4] = {
        MatrixIdentity(), MatrixIdentity(),
        MatrixIdentity(), MatrixIdentity()
    };

    Matrix modelPose[4] = {
        MatrixIdentity(), MatrixIdentity(),
        MatrixIdentity(), MatrixIdentity()
    };

    const float times[] = {0.0f, 1.0f};
    const Vector3 positions[] = {
        {0.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f}
    };

    R3D_AnimationChannel channel = {
        .boneIndex = 3,
        .translation = {
            .times = times,
            .values = positions,
            .count = 2
        }
    };

    R3D_Animation animation = {
        .name = "walk",
        .channels = &channel,
        .channelCount = 1,
        .boneCount = 4,
        .duration = 1.0f,
        .ticksPerSecond = 1.0f
    };

    R3D_AnimationLib library = {
        .animations = &animation,
        .count = 1
    };

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = 4,
            .localBind = localBind,
            .modelBind = modelBind,
            .rootBind = MatrixIdentity()
        },
        .animLib = library,
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    R3D_AnimationNodeParams nodeParams = {
        .name = "walk",
        .state = {
            .play = true,
            .loop = false,
            .speed = 1.0f
        }
    };

    R3D_BoneLookAtParams lookAt = {
        .localForward = {0.0f, 0.0f, 1.0f},
        .localUp = {0.0f, 1.0f, 0.0f},
        .modelUp = {0.0f, 1.0f, 0.0f},
        .weight = 0.5f,
        .maxAngleRadians = 0.0f
    };

    R3D_BoneAimParams aim = {
        .localForward = {0.0f, 0.0f, 1.0f},
        .localUp = {0.0f, 1.0f, 0.0f},
        .weight = 1.0f,
        .maxAngleRadians = 0.0f
    };

    int failures = 0;

    R3D_AnimationTree tree = R3D_LoadAnimationTree(player, 1);
    R3D_AnimationTreeNode* node = R3D_CreateAnimationNode(&tree, nodeParams);

    if (!node)
    {
        printf("Failed to create animation node\n");
        R3D_UnloadAnimationTree(tree);
        return 1;
    }

    R3D_AddRootAnimationNode(&tree, node);

    // ========================================
    // STAGE 1: ADVANCE ANIMATION
    // ========================================

    failures += check("Advance animation",
        R3D_AdvanceAnimationTree(&tree, 0.25f));

    failures += check("Playback time updated",
        near(R3D_GetAnimationNodeTimeSeconds(node), 0.25f));

    failures += check("Advance does not sample pose",
        near(localPose[3].m12, 0.0f));

    // ========================================
    // STAGE 2: SAMPLE AUTHORED POSE
    // ========================================

    failures += check("Sample authored animation",
        R3D_ComputeAnimationTreeLocalPose(&tree));

    failures += check("Animated root sampled",
        near(localPose[3].m12, 0.5f));

    failures += check("Unanimated bones retain bind pose",
        R3D_MatrixEquals(localPose[0], localBind[0]) &&
        R3D_MatrixEquals(localPose[1], localBind[1]) &&
        R3D_MatrixEquals(localPose[2], localBind[2]));

    failures += check("Compute initial model pose",
        R3D_ComputeAnimationTreeModelPose(&tree));

    failures += check("Root animation reaches descendants",
        near_position(modelPose[0], 0.5f, 1.5f, 0.0f) &&
        near_position(modelPose[1], 1.0f, 1.0f, 0.0f));

    // ========================================
    // STAGE 3: LOOKAT
    // ========================================

    Vector3 headTarget = {10.5f, 1.5f, 0.0f};
    Matrix originalHeadModel = modelPose[0];

    failures += check("Apply LookAt",
        R3D_ApplyAnimationBoneLookAt(&tree.player, 0, headTarget, lookAt));

    failures += check("LookAt does not recompute automatically",
        R3D_MatrixEquals(modelPose[0], originalHeadModel));

    failures += check("Recompute LookAt subtree",
        R3D_RecomputeAnimationModelPoseFromBone(&tree.player, 0));

    Vector3 headForward = bone_direction(modelPose[0], lookAt.localForward);

    failures += check("Weighted LookAt gives 45-degree rotation",
        near(headForward.x, 0.707107f) &&
        near(headForward.z, 0.707107f));

    failures += check("LookAt preserves head position",
        near_position(modelPose[0], 0.5f, 1.5f, 0.0f));

    // ========================================
    // STAGE 4: AIM
    // ========================================

    Vector3 weaponTarget = {11.0f, 1.0f, 0.0f};
    Matrix headAfterLookAt = modelPose[0];
    Matrix muzzleBeforeAim = modelPose[2];

    failures += check("Apply Aim after LookAt",
        R3D_ApplyAnimationBoneAim(&tree.player, 1, weaponTarget, aim));

    failures += check("Aim does not recompute automatically",
        R3D_MatrixEquals(modelPose[2], muzzleBeforeAim));

    failures += check("Recompute aimed subtree",
        R3D_RecomputeAnimationModelPoseFromBone(&tree.player, 1));

    failures += check("Weapon points toward target",
        aimed_at(modelPose[1], aim.localForward, weaponTarget));

    failures += check("Muzzle follows weapon orientation",
        near_position(modelPose[2], 2.0f, 1.0f, 0.0f));

    failures += check("Aim does not overwrite LookAt",
        R3D_MatrixEquals(modelPose[0], headAfterLookAt));

    // ========================================
    // STAGE 5: FUTURE IK INTEGRATION POINT
    // ========================================
    //
    // IK is not implemented yet.
    // Simulate a downstream procedural modification.
    // It must be possible to modify and recompute another bone
    // without destroying the earlier LookAt/Aim results.

    Transform muzzleAdjustment = {
        .translation = {0.0f, 0.0f, 2.0f},
        .rotation = QuaternionIdentity(),
        .scale = {1.0f, 1.0f, 1.0f}
    };

    failures += check("Apply downstream procedural modification",
        R3D_SetAnimationBoneLocalTransform(&tree.player, 2, muzzleAdjustment));

    failures += check("Recompute downstream bone",
        R3D_RecomputeAnimationModelPoseFromBone(&tree.player, 2));

    failures += check("Downstream modification follows Aim",
        near_position(modelPose[2], 3.0f, 1.0f, 0.0f));

    failures += check("Downstream stage preserves LookAt",
        R3D_MatrixEquals(modelPose[0], headAfterLookAt));

    failures += check("Downstream stage preserves Aim",
        aimed_at(modelPose[1], aim.localForward, weaponTarget));

    failures += check("Procedural stages do not advance animation",
        near(R3D_GetAnimationNodeTimeSeconds(node), 0.25f));

    // ========================================
    // STAGE 6: NEXT FRAME
    // ========================================
    //
    // The next animation sample must restore the authored pose.
    // Procedural corrections should not accumulate between frames.

    failures += check("Advance next frame",
        R3D_AdvanceAnimationTree(&tree, 0.25f));

    failures += check("Sample next frame",
        R3D_ComputeAnimationTreeLocalPose(&tree));

    failures += check("Authored pose restored before procedures",
        R3D_MatrixEquals(localPose[0], localBind[0]) &&
        R3D_MatrixEquals(localPose[1], localBind[1]) &&
        R3D_MatrixEquals(localPose[2], localBind[2]));

    failures += check("Second frame root movement",
        near(localPose[3].m12, 1.0f));

    failures += check("Recompute second frame model pose",
        R3D_ComputeAnimationTreeModelPose(&tree));

    failures += check("No accumulated procedural rotation",
        near(headForward.x, 0.707107f) &&
        near(bone_direction(modelPose[0], lookAt.localForward).z, 1.0f) &&
        near(bone_direction(modelPose[1], aim.localForward).z, 1.0f));

    // Reapply procedures using targets relative to the moved character.
    headTarget = (Vector3){11.0f, 1.5f, 0.0f};
    weaponTarget = (Vector3){11.5f, 1.0f, 0.0f};

    bool secondFrame =
        R3D_ApplyAnimationBoneLookAt(&tree.player, 0, headTarget, lookAt) &&
        R3D_RecomputeAnimationModelPoseFromBone(&tree.player, 0) &&
        R3D_ApplyAnimationBoneAim(&tree.player, 1, weaponTarget, aim) &&
        R3D_RecomputeAnimationModelPoseFromBone(&tree.player, 1);

    failures += check("Procedures can be reapplied next frame", secondFrame);

    failures += check("LookAt produces consistent second frame",
        near(bone_direction(modelPose[0], lookAt.localForward).x, 0.707107f));

    failures += check("Aim produces consistent second frame",
        aimed_at(modelPose[1], aim.localForward, weaponTarget));

    failures += check("Final animation time unchanged by procedures",
        near(R3D_GetAnimationNodeTimeSeconds(node), 0.5f));

    // GPU upload is intentionally omitted:
    // This is a headless CPU integration test.
    // R3D_UploadAnimationPose() is already covered by the separate
    // animation_skin_upload regression.

    R3D_UnloadAnimationTree(tree);

    printf("\nLookAt/Aim staged pipeline tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}