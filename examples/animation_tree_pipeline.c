#include <r3d/r3d_animation_tree.h>
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
    return fabsf(a - b) < 0.0001f;
}

int main(void)
{
    const float times[] = {0.0f, 1.0f};
    const Vector3 positions[] = {
        {0.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f}
    };

    R3D_AnimationChannel channel = {
        .boneIndex = 0,
        .translation = {
            .times = times,
            .values = positions,
            .count = 2
        }
    };

    R3D_Animation animation = {
        .name = "move",
        .channels = &channel,
        .channelCount = 1,
        .boneCount = 2,
        .duration = 1.0f,
        .ticksPerSecond = 1.0f
    };

    R3D_AnimationLib library = {
        .animations = &animation,
        .count = 1
    };

    R3D_BoneInfo bones[2] = {
        {.parent = -1},
        {.parent = 0}
    };

    Matrix localBind[2] = {
        MatrixIdentity(),
        MatrixTranslate(0.0f, 1.0f, 0.0f)
    };

    Matrix modelBind[2] = {
        MatrixIdentity(),
        MatrixTranslate(0.0f, 1.0f, 0.0f)
    };

    Matrix localPose[2] = {
        MatrixIdentity(),
        MatrixIdentity()
    };

    Matrix modelPose[2] = {
        MatrixIdentity(),
        MatrixIdentity()
    };

    R3D_Skeleton skeleton = {
        .bones = bones,
        .boneCount = 2,
        .localBind = localBind,
        .modelBind = modelBind,
        .rootBind = MatrixIdentity()
    };

    R3D_AnimationPlayer player = {
        .skeleton = skeleton,
        .animLib = library,
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    R3D_AnimationNodeParams params = {
        .name = "move",
        .state = {
            .play = true,
            .loop = false,
            .speed = 1.0f
        }
    };

    int failures = 0;

    R3D_AnimationTree tree = R3D_LoadAnimationTree(player, 1);
    R3D_AnimationTreeNode* node = R3D_CreateAnimationNode(&tree, params);

    if (!node)
    {
        printf("Failed to create animation node\n");
        R3D_UnloadAnimationTree(tree);
        return 1;
    }

    R3D_AddRootAnimationNode(&tree, node);

    failures += check("Advance tree", R3D_AdvanceAnimationTree(&tree, 0.25f));
    failures += check("Time advanced", near(R3D_GetAnimationNodeTimeSeconds(node), 0.25f));
    failures += check("Advance does not sample", near(localPose[0].m12, 0.0f));

    failures += check("Compute local pose", R3D_ComputeAnimationTreeLocalPose(&tree));
    failures += check("Local translation", near(localPose[0].m12, 0.5f));
    failures += check("Missing channel preserves bind", near(localPose[1].m13, 1.0f));
    failures += check("Model not yet computed", near(modelPose[0].m12, 0.0f));

    // Simulate a procedural bone modification between sampling and hierarchy accumulation.
    localPose[0].m12 += 4.0f;

    failures += check("Compute model pose", R3D_ComputeAnimationTreeModelPose(&tree));
    failures += check("Modified root reaches model pose", near(modelPose[0].m12, 4.5f));
    failures += check("Child inherits modified root", near(modelPose[1].m12, 4.5f));
    failures += check("Child retains local offset", near(modelPose[1].m13, 1.0f));
    failures += check("Model computation does not advance time", near(R3D_GetAnimationNodeTimeSeconds(node), 0.25f));

    // Negative time should be rejected without moving playback.
    failures += check("Negative delta rejected", !R3D_AdvanceAnimationTree(&tree, -1.0f));
    failures += check("Rejected delta preserves time", near(R3D_GetAnimationNodeTimeSeconds(node), 0.25f));

    R3D_UnloadAnimationTree(tree);

    // Repeat with root motion enabled.
    R3D_AnimationTree rootTree = R3D_LoadAnimationTreeEx(player, 1, 0);
    R3D_AnimationTreeNode* rootNode = R3D_CreateAnimationNode(&rootTree, params);

    if (!rootNode)
    {
        printf("Failed to create root-motion node\n");
        R3D_UnloadAnimationTree(rootTree);
        return 1;
    }

    R3D_AddRootAnimationNode(&rootTree, rootNode);

    Transform motion = {0};
    Transform distance = {0};

    failures += check("Advance root-motion tree", R3D_AdvanceAnimationTree(&rootTree, 0.25f));
    failures += check("Evaluate root-motion tree", R3D_ComputeAnimationTreeLocalPoseEx(&rootTree, &motion, &distance));
    failures += check("Root motion delta", near(motion.translation.x, 0.5f));
    failures += check("Root motion distance", near(distance.translation.x, 0.5f));
    failures += check("Root motion removed from local pose", near(localPose[0].m12, 0.0f));

    failures += check("Advance second frame", R3D_AdvanceAnimationTree(&rootTree, 0.25f));
    failures += check("Evaluate second frame", R3D_ComputeAnimationTreeLocalPoseEx(&rootTree, &motion, &distance));
    failures += check("Second root-motion delta", near(motion.translation.x, 0.5f));
    failures += check("Accumulated root distance", near(distance.translation.x, 1.0f));

    R3D_UnloadAnimationTree(rootTree);

    printf("\nAnimationTree pipeline tests: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}