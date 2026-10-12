#include <r3d/r3d_animation_tree.h>
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
    return fabsf(a - b) < 0.0001f;
}

static bool near_vec3(Vector3 a, Vector3 b)
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

static bool near_rotation(Quaternion a, Quaternion b)
{
    const float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    return near(fabsf(dot), 1.0f);
}

int main(void)
{
    R3D_BoneInfo bones[3] = {
        {.parent = -1},
        {.parent = 0},
        {.parent = 1}
    };

    Matrix localPose[3] = {
        MatrixTranslate(2.0f, 0.0f, 0.0f),
        MatrixTranslate(0.0f, 3.0f, 0.0f),
        MatrixTranslate(0.0f, 0.0f, 4.0f)
    };

    Matrix modelPose[3] = {
        MatrixIdentity(),
        MatrixIdentity(),
        MatrixIdentity()
    };

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = 3,
            .rootBind = MatrixTranslate(10.0f, 0.0f, 0.0f)
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    // The existing staged hierarchy computation is CPU-only.
    R3D_AnimationTree tree = {.player = player};

    int failures = 0;
    Transform result = {0};
    Matrix matrix = {0};

    failures += check("Initial hierarchy computation",
        R3D_ComputeAnimationTreeModelPose(&tree));

    failures += check("Root correction applied",
        near(modelPose[0].m12, 12.0f));

    failures += check("Child inherits root",
        near(modelPose[1].m12, 12.0f) &&
        near(modelPose[1].m13, 3.0f));

    failures += check("Exact model matrix getter",
        R3D_GetAnimationBoneModelMatrix(&player, 1, &matrix) &&
        R3D_MatrixEquals(matrix, modelPose[1]));

    failures += check("Model transform getter",
        R3D_GetAnimationBoneModelTransform(&player, 1, &result) &&
        near_vec3(result.translation, (Vector3){12.0f, 3.0f, 0.0f}));

    Transform desiredChild = {
        .translation = {14.0f, 6.0f, 1.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 0.0f, 1.0f}, PI / 2.0f),
        .scale = {1.5f, 2.0f, 3.0f}
    };

    Matrix previousModel = modelPose[1];

    failures += check("Set child model transform",
        R3D_SetAnimationBoneModelTransform(&player, 1, desiredChild));

    failures += check("Model pose not automatically recomputed",
        R3D_MatrixEquals(modelPose[1], previousModel));

    failures += check("Child converted into parent-local space",
        near(localPose[1].m12, 2.0f) &&
        near(localPose[1].m13, 6.0f) &&
        near(localPose[1].m14, 1.0f));

    failures += check("Recompute updated hierarchy",
        R3D_ComputeAnimationTreeModelPose(&tree));

    failures += check("Model translation round trip",
        R3D_GetAnimationBoneModelTransform(&player, 1, &result) &&
        near_vec3(result.translation, desiredChild.translation));

    failures += check("Model rotation round trip",
        near_rotation(result.rotation, desiredChild.rotation));

    failures += check("Model scale round trip",
        near_vec3(result.scale, desiredChild.scale));

    failures += check("Grandchild inherits child transform",
        near(modelPose[2].m12, 14.0f) &&
        near(modelPose[2].m13, 6.0f) &&
        near(modelPose[2].m14, 13.0f));

    // Override the root in model space, respecting rootBind.
    Transform desiredRoot = {
        .translation = {20.0f, -2.0f, 0.0f},
        .rotation = QuaternionIdentity(),
        .scale = {1.0f, 1.0f, 1.0f}
    };

    failures += check("Set root model transform",
        R3D_SetAnimationBoneModelTransform(&player, 0, desiredRoot));

    failures += check("Root correction inverted",
        near(localPose[0].m12, 10.0f) &&
        near(localPose[0].m13, -2.0f));

    failures += check("Recompute root override",
        R3D_ComputeAnimationTreeModelPose(&tree) &&
        R3D_GetAnimationBoneModelTransform(&player, 0, &result) &&
        near_vec3(result.translation, desiredRoot.translation));

    // A rotated, nonuniformly scaled parent can produce a sheared
    // local matrix when requesting an exact model-space TRS.
    Transform scaledRoot = {
        .translation = {1.0f, 0.0f, 0.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 0.0f, 1.0f}, 0.4f),
        .scale = {2.0f, 3.0f, 4.0f}
    };

    R3D_SetAnimationBoneLocalTransform(&player, 0, scaledRoot);
    R3D_ComputeAnimationTreeModelPose(&tree);

    Transform desiredScaledChild = {
        .translation = {12.0f, 5.0f, 2.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 1.0f, 0.0f}, 0.7f),
        .scale = {1.1f, 1.2f, 1.3f}
    };

    failures += check("Set under rotated nonuniform parent",
        R3D_SetAnimationBoneModelTransform(&player, 1, desiredScaledChild));

    // Construct the expected matrix through the already-tested local
    // TRS setter, rather than introducing another matrix convention.
    Matrix expectedBuffer[1] = {MatrixIdentity()};

    R3D_AnimationPlayer scratch = {
        .skeleton = {.boneCount = 1},
        .localPose = expectedBuffer
    };

    R3D_SetAnimationBoneLocalTransform(&scratch, 0, desiredScaledChild);

    failures += check("Recompute nonuniform hierarchy",
        R3D_ComputeAnimationTreeModelPose(&tree));

    failures += check("Exact model override survives shear",
        R3D_MatrixEquals(modelPose[1], expectedBuffer[0]));

    // A singular parent cannot be inverted.
    Transform singularRoot = scaledRoot;
    singularRoot.scale.x = 0.0f;

    R3D_SetAnimationBoneLocalTransform(&player, 0, singularRoot);
    R3D_ComputeAnimationTreeModelPose(&tree);

    Matrix previousLocal = localPose[1];

    failures += check("Singular parent rejected",
        !R3D_SetAnimationBoneModelTransform(&player, 1, desiredScaledChild));

    failures += check("Rejected override leaves local pose unchanged",
        R3D_MatrixEquals(localPose[1], previousLocal));

    // Invalid arguments.
    failures += check("Negative index rejected",
        !R3D_GetAnimationBoneModelTransform(&player, -1, &result) &&
        !R3D_SetAnimationBoneModelTransform(&player, -1, desiredChild));

    failures += check("Out-of-range index rejected",
        !R3D_GetAnimationBoneModelMatrix(&player, 3, &matrix) &&
        !R3D_SetAnimationBoneModelTransform(&player, 3, desiredChild));

    failures += check("NULL arguments rejected",
        !R3D_GetAnimationBoneModelTransform(NULL, 0, &result) &&
        !R3D_GetAnimationBoneModelMatrix(&player, 0, NULL) &&
        !R3D_SetAnimationBoneModelTransform(NULL, 0, desiredChild));

    Transform invalid = desiredChild;
    invalid.translation.x = NAN;

    failures += check("Non-finite transform rejected",
        !R3D_SetAnimationBoneModelTransform(&player, 1, invalid));

    printf("\nModel-space pose access tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}