#include <r3d/r3d_animation_player.h>
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

static bool near_matrix(Matrix a, Matrix b)
{
    float16 av = MatrixToFloatV(a);
    float16 bv = MatrixToFloatV(b);

    for (int i = 0; i < 16; i++)
    {
        if (!near(av.v[i], bv.v[i])) return false;
    }

    return true;
}

static bool near_position(Matrix m, float x, float y, float z)
{
    return near(m.m12, x) && near(m.m13, y) && near(m.m14, z);
}

int main(void)
{
    // Index order intentionally differs from hierarchy order.
    //
    // Root (2)
    //   Hand (0)
    //     Tool (3)
    //   Other arm (1)
    //
    // Separate root (4)

    R3D_BoneInfo bones[5] = {
        {.parent = 2},
        {.parent = 2},
        {.parent = -1},
        {.parent = 0},
        {.parent = -1}
    };

    Matrix localPose[5] = {
        MatrixTranslate(0.0f, 1.0f, 0.0f),
        MatrixTranslate(0.0f, -1.0f, 0.0f),
        MatrixTranslate(2.0f, 0.0f, 0.0f),
        MatrixTranslate(0.0f, 0.0f, 3.0f),
        MatrixTranslate(8.0f, 0.0f, 0.0f)
    };

    Matrix modelPose[5] = {
        MatrixIdentity(), MatrixIdentity(), MatrixIdentity(),
        MatrixIdentity(), MatrixIdentity()
    };

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = 5,
            .rootBind = MatrixTranslate(10.0f, 0.0f, 0.0f)
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    int failures = 0;

    failures += check("Initial hierarchy computation",
        R3D_RecomputeAnimationModelPose(&player));

    failures += check("Unordered hierarchy initialized",
        near_position(modelPose[0], 12.0f, 1.0f, 0.0f) &&
        near_position(modelPose[3], 12.0f, 1.0f, 3.0f));

    // Exact local matrix access, including shear.
    Matrix shearedLocal = MatrixTranslate(0.0f, 2.0f, 0.0f);
    shearedLocal.m4 = 0.25f;

    Matrix previousModel = modelPose[0];
    Matrix returnedLocal = {0};

    failures += check("Set exact local matrix",
        R3D_SetAnimationBoneLocalMatrix(&player, 0, shearedLocal));

    failures += check("Get exact local matrix",
        R3D_GetAnimationBoneLocalMatrix(&player, 0, &returnedLocal) &&
        near_matrix(returnedLocal, shearedLocal));

    failures += check("Local modification does not recompute",
        near_matrix(modelPose[0], previousModel));

    failures += check("Recompute modified subtree",
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0));

    failures += check("Modified hand propagated",
        near_position(modelPose[0], 12.0f, 2.0f, 0.0f));

    failures += check("Attached child propagated",
        near_position(modelPose[3], 12.0f, 2.0f, 3.0f));

    failures += check("Unrelated arm unchanged",
        near_position(modelPose[1], 12.0f, -1.0f, 0.0f));

    // Simulate an external system providing a model-space bone matrix.
    Matrix desiredModel = MatrixTranslate(15.0f, 5.0f, 1.0f);
    previousModel = modelPose[0];

    failures += check("Set exact model matrix",
        R3D_SetAnimationBoneModelMatrix(&player, 0, desiredModel));

    failures += check("Model setter is deferred",
        near_matrix(modelPose[0], previousModel));

    failures += check("Model target converted to local",
        near_position(localPose[0], 3.0f, 5.0f, 1.0f));

    failures += check("Recompute model override",
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0));

    failures += check("Requested model matrix reached",
        near_matrix(modelPose[0], desiredModel));

    failures += check("Child follows overridden hand",
        near_position(modelPose[3], 15.0f, 5.0f, 4.0f));

    // Existing Transform setter must also support unordered parents.
    Transform desiredTransform = {
        .translation = {16.0f, 6.0f, 2.0f},
        .rotation = QuaternionIdentity(),
        .scale = {1.0f, 1.0f, 1.0f}
    };

    failures += check("TRS setter accepts higher parent index",
        R3D_SetAnimationBoneModelTransform(&player, 0, desiredTransform));

    failures += check("TRS override recomputed",
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0) &&
        near_position(modelPose[0], 16.0f, 6.0f, 2.0f));

    // Nonuniformly scaled, rotated parent.
    Transform scaledRoot = {
        .translation = {2.0f, 0.0f, 0.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 0.0f, 1.0f}, 0.4f),
        .scale = {2.0f, 3.0f, 4.0f}
    };

    failures += check("Set rotated nonuniform parent",
        R3D_SetAnimationBoneLocalTransform(&player, 2, scaledRoot) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 2));

    // Use the public TRS setter to construct an independent target matrix.
    Matrix targetBuffer[1] = {MatrixIdentity()};

    R3D_AnimationPlayer scratch = {
        .skeleton = {.boneCount = 1},
        .localPose = targetBuffer
    };

    Transform physicsTarget = {
        .translation = {7.0f, 8.0f, 9.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 1.0f, 0.0f}, 0.7f),
        .scale = {1.1f, 1.2f, 1.3f}
    };

    R3D_SetAnimationBoneLocalTransform(&scratch, 0, physicsTarget);

    Matrix siblingBefore = modelPose[1];

    failures += check("Exact override under nonuniform parent",
        R3D_SetAnimationBoneModelMatrix(&player, 0, targetBuffer[0]));

    failures += check("Exact override recomputed",
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0));

    failures += check("Exact target preserved after recomputation",
        near_matrix(modelPose[0], targetBuffer[0]));

    failures += check("Sibling preserved",
        near_matrix(modelPose[1], siblingBefore));

    // Invalid matrix and hierarchy inputs.
    Matrix savedLocal = localPose[0];

    Matrix invalid = targetBuffer[0];
    invalid.m12 = NAN;

    failures += check("Non-finite matrix rejected",
        !R3D_SetAnimationBoneModelMatrix(&player, 0, invalid) &&
        near_matrix(localPose[0], savedLocal));

    invalid = targetBuffer[0];
    invalid.m3 = 0.5f;

    failures += check("Perspective matrix rejected",
        !R3D_SetAnimationBoneLocalMatrix(&player, 0, invalid) &&
        near_matrix(localPose[0], savedLocal));

    bones[0].parent = 0;

    failures += check("Self-parent rejected",
        !R3D_SetAnimationBoneModelMatrix(&player, 0, targetBuffer[0]));

    bones[0].parent = 2;

    failures += check("Invalid indices rejected",
        !R3D_SetAnimationBoneLocalMatrix(&player, -1, MatrixIdentity()) &&
        !R3D_SetAnimationBoneModelMatrix(&player, 5, MatrixIdentity()));

    failures += check("NULL pointers rejected",
        !R3D_GetAnimationBoneLocalMatrix(NULL, 0, &returnedLocal) &&
        !R3D_GetAnimationBoneLocalMatrix(&player, 0, NULL) &&
        !R3D_SetAnimationBoneModelMatrix(NULL, 0, MatrixIdentity()));

    // Singular parent cannot be inverted for a model-space override.
    Transform singularRoot = scaledRoot;
    singularRoot.scale.x = 0.0f;

    R3D_SetAnimationBoneLocalTransform(&player, 2, singularRoot);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 2);

    savedLocal = localPose[0];

    failures += check("Singular parent rejected",
        !R3D_SetAnimationBoneModelMatrix(&player, 0, targetBuffer[0]));

    failures += check("Rejected override preserves local pose",
        near_matrix(localPose[0], savedLocal));

    printf("\nExternal pose modification tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}