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

static bool near_position(Matrix matrix, float x, float y, float z)
{
    return near(matrix.m12, x) && near(matrix.m13, y) && near(matrix.m14, z);
}

int main(void)
{
    // Two independent hierarchies, deliberately interleaved.
    //
    // Root A (0)
    //   Child A (4)
    //     Hand A (2)
    //       Tool A (5)
    //
    // Root B (1)
    //   Child B (3)

    R3D_BoneInfo bones[6] = {
        {.parent = -1},
        {.parent = -1},
        {.parent = 4},
        {.parent = 1},
        {.parent = 0},
        {.parent = 2}
    };

    Matrix localPose[6] = {
        MatrixTranslate(1.0f, 0.0f, 0.0f),
        MatrixTranslate(20.0f, 0.0f, 0.0f),
        MatrixTranslate(0.0f, 0.0f, 3.0f),
        MatrixTranslate(0.0f, 5.0f, 0.0f),
        MatrixTranslate(0.0f, 2.0f, 0.0f),
        MatrixTranslate(0.0f, 0.0f, 4.0f)
    };

    Matrix modelPose[6] = {
        MatrixIdentity(), MatrixIdentity(), MatrixIdentity(),
        MatrixIdentity(), MatrixIdentity(), MatrixIdentity()
    };

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = 6,
            .rootBind = MatrixTranslate(10.0f, 0.0f, 0.0f)
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    int failures = 0;

    failures += check("Full hierarchy recomputation",
        R3D_RecomputeAnimationModelPose(&player));

    failures += check("Root A includes root correction",
        near_position(modelPose[0], 11.0f, 0.0f, 0.0f));

    failures += check("Root B includes root correction",
        near_position(modelPose[1], 30.0f, 0.0f, 0.0f));

    failures += check("Child with higher index",
        near_position(modelPose[4], 11.0f, 2.0f, 0.0f));

    failures += check("Child with lower index than parent",
        near_position(modelPose[2], 11.0f, 2.0f, 3.0f));

    failures += check("Deep descendant",
        near_position(modelPose[5], 11.0f, 2.0f, 7.0f));

    failures += check("Independent branch",
        near_position(modelPose[3], 30.0f, 5.0f, 0.0f));

    // Modify Child A without recomputing the hierarchy.
    Transform childA = {
        .translation = {0.0f, 7.0f, 0.0f},
        .rotation = QuaternionIdentity(),
        .scale = {1.0f, 1.0f, 1.0f}
    };

    failures += check("Modify local bone",
        R3D_SetAnimationBoneLocalTransform(&player, 4, childA));

    failures += check("Model pose remains unchanged",
        near_position(modelPose[4], 11.0f, 2.0f, 0.0f));

    failures += check("Recompute subtree",
        R3D_RecomputeAnimationModelPoseFromBone(&player, 4));

    failures += check("Subtree root updated",
        near_position(modelPose[4], 11.0f, 7.0f, 0.0f));

    failures += check("Lower-index descendant updated",
        near_position(modelPose[2], 11.0f, 7.0f, 3.0f));

    failures += check("Deep descendant updated",
        near_position(modelPose[5], 11.0f, 7.0f, 7.0f));

    failures += check("Subtree parent unchanged",
        near_position(modelPose[0], 11.0f, 0.0f, 0.0f));

    failures += check("Other root unchanged",
        near_position(modelPose[1], 30.0f, 0.0f, 0.0f));

    failures += check("Other branch unchanged",
        near_position(modelPose[3], 30.0f, 5.0f, 0.0f));

    // Recompute the complete hierarchy again after modifying Root A.
    Transform rootA = {
        .translation = {3.0f, 0.0f, 0.0f},
        .rotation = QuaternionIdentity(),
        .scale = {1.0f, 1.0f, 1.0f}
    };

    R3D_SetAnimationBoneLocalTransform(&player, 0, rootA);

    failures += check("Recompute from parent root",
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0));

    failures += check("Root override propagated",
        near_position(modelPose[0], 13.0f, 0.0f, 0.0f));

    failures += check("Descendants inherit root override",
        near_position(modelPose[2], 13.0f, 7.0f, 3.0f) &&
        near_position(modelPose[5], 13.0f, 7.0f, 7.0f));

    failures += check("Unrelated branch remains unchanged",
        near_position(modelPose[3], 30.0f, 5.0f, 0.0f));

    // Validation checks.
    failures += check("Negative subtree index rejected",
        !R3D_RecomputeAnimationModelPoseFromBone(&player, -1));

    failures += check("Invalid subtree index rejected",
        !R3D_RecomputeAnimationModelPoseFromBone(&player, 6));

    failures += check("NULL player rejected",
        !R3D_RecomputeAnimationModelPose(NULL) &&
        !R3D_RecomputeAnimationModelPoseFromBone(NULL, 0));

    R3D_AnimationPlayer invalid = player;
    invalid.modelPose = NULL;

    failures += check("Missing pose buffer rejected",
        !R3D_RecomputeAnimationModelPose(&invalid));

    // Introduce a cycle: Root A -> Hand A -> Child A -> Root A.
    bones[0].parent = 2;

    failures += check("Cyclic hierarchy rejected",
        !R3D_RecomputeAnimationModelPose(&player));

    bones[0].parent = -1;

    failures += check("Hierarchy works after restoring parent",
        R3D_RecomputeAnimationModelPose(&player) &&
        near_position(modelPose[5], 13.0f, 7.0f, 7.0f));

    // AnimationTree should use the same shared recomputation implementation.
    R3D_AnimationTree tree = {.player = player};

    failures += check("AnimationTree recomputation uses shared helper",
        R3D_ComputeAnimationTreeModelPose(&tree));

    printf("\nPose recomputation tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}