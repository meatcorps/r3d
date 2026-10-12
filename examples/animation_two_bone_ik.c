#include <r3d/r3d_animation_ik.h>
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
    return fabsf(a - b) < 0.003f;
}

static bool near_vec3(Vector3 a, Vector3 b)
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

static Vector3 position(Matrix matrix)
{
    return (Vector3){matrix.m12, matrix.m13, matrix.m14};
}

static Vector3 basis_axis(Matrix matrix, Vector3 axis)
{
    return Vector3Subtract(Vector3Transform(axis, matrix), position(matrix));
}

static bool restore_chain(R3D_AnimationPlayer* player)
{
    return R3D_SetAnimationBoneLocalMatrix(player, 1, MatrixIdentity()) &&
           R3D_SetAnimationBoneLocalMatrix(player, 2, MatrixTranslate(1.0f, 0.0f, 0.0f)) &&
           R3D_RecomputeAnimationModelPose(player);
}

int main(void)
{
    // Unordered bone indices:
    //
    // Root (3)
    //   Shoulder (1)
    //     Elbow (2)
    //       Wrist (0)
    //   Other (4)

    R3D_BoneInfo bones[5] = {
        {.name = "Wrist", .parent = 2},
        {.name = "Shoulder", .parent = 3},
        {.name = "Elbow", .parent = 1},
        {.name = "Root", .parent = -1},
        {.name = "Other", .parent = 3}
    };

    Matrix localPose[5] = {
        MatrixTranslate(1.0f, 0.0f, 0.0f),
        MatrixIdentity(),
        MatrixTranslate(1.0f, 0.0f, 0.0f),
        MatrixTranslate(1.0f, 0.0f, 0.0f),
        MatrixTranslate(0.0f, 2.0f, 0.0f)
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

    R3D_TwoBoneIKParams params = {
        .targetModel = {12.0f, 1.0f, 0.0f},
        .poleModel = {11.0f, 0.0f, 3.0f},
        .weight = 1.0f
    };

    int failures = 0;

    failures += check("Compute initial hierarchy",
        R3D_RecomputeAnimationModelPose(&player));

    failures += check("Initial shoulder position",
        near_vec3(position(modelPose[1]), (Vector3){11, 0, 0}));

    failures += check("Initial elbow position",
        near_vec3(position(modelPose[2]), (Vector3){12, 0, 0}));

    failures += check("Initial wrist position",
        near_vec3(position(modelPose[0]), (Vector3){13, 0, 0}));

    Matrix originalEndLocal = localPose[0];
    Matrix originalEndModel = modelPose[0];
    Matrix originalSiblingModel = modelPose[4];
    Matrix originalRootModel = modelPose[3];

    // ========================================
    // REACHABLE TARGET
    // ========================================

    failures += check("Apply two-bone IK",
        R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params));

    failures += check("IK does not recompute model pose",
        R3D_MatrixEquals(modelPose[0], originalEndModel));

    failures += check("End local transform preserved",
        R3D_MatrixEquals(localPose[0], originalEndLocal));

    failures += check("Recompute IK subtree",
        R3D_RecomputeAnimationModelPoseFromBone(&player, 1));

    failures += check("Wrist reaches target",
        near_vec3(position(modelPose[0]), params.targetModel));

    failures += check("Elbow bends toward pole",
        modelPose[2].m14 > 0.5f);

    failures += check("Upper segment length preserved",
        near(Vector3Distance(position(modelPose[1]), position(modelPose[2])), 1.0f));

    failures += check("Lower segment length preserved",
        near(Vector3Distance(position(modelPose[2]), position(modelPose[0])), 1.0f));

    failures += check("Unrelated sibling unchanged",
        R3D_MatrixEquals(modelPose[4], originalSiblingModel));

    failures += check("Root hierarchy unchanged",
        R3D_MatrixEquals(modelPose[3], originalRootModel));

    // ========================================
    // REVERSE POLE
    // ========================================

    failures += check("Restore authored chain",
        restore_chain(&player));

    params.poleModel = (Vector3){11.0f, 0.0f, -3.0f};

    failures += check("IK accepts reverse pole",
        R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 1));

    failures += check("Reverse pole changes bend direction",
        modelPose[2].m14 < -0.5f);

    failures += check("Reverse pole still reaches target",
        near_vec3(position(modelPose[0]), params.targetModel));

    // ========================================
    // WEIGHT ZERO / PARTIAL WEIGHT
    // ========================================

    restore_chain(&player);
    params.poleModel = (Vector3){11.0f, 0.0f, 3.0f};
    params.weight = 0.0f;

    Matrix authoredRootLocal = localPose[1];
    Matrix authoredMiddleLocal = localPose[2];

    failures += check("Zero weight succeeds",
        R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params));

    failures += check("Zero weight preserves authored pose",
        R3D_MatrixEquals(localPose[1], authoredRootLocal) &&
        R3D_MatrixEquals(localPose[2], authoredMiddleLocal));

    params.weight = 0.5f;

    failures += check("Half-weight IK applied",
        R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 1));

    float partialError = Vector3Distance(position(modelPose[0]), params.targetModel);

    failures += check("Half weight moves wrist toward target",
        partialError < 1.414f && partialError > 0.05f);

    // ========================================
    // UNREACHABLE TARGET
    // ========================================

    restore_chain(&player);
    params.weight = 1.0f;
    params.targetModel = (Vector3){11.0f, 10.0f, 0.0f};

    failures += check("Unreachable target accepted",
        R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 1));

    failures += check("Chain extends toward unreachable target",
        near_vec3(position(modelPose[0]), (Vector3){11, 2, 0}));

    // ========================================
    // CLOSE TARGET / FOLDED CHAIN
    // ========================================

    restore_chain(&player);
    params.targetModel = (Vector3){11.0f, 0.5f, 0.0f};

    failures += check("Close target accepted",
        R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 1));

    failures += check("Folded chain reaches target",
        near_vec3(position(modelPose[0]), params.targetModel));

    // ========================================
    // PARALLEL POLE FALLBACK
    // ========================================

    restore_chain(&player);
    params.targetModel = (Vector3){11.0f, 1.0f, 0.0f};
    params.poleModel = (Vector3){11.0f, 10.0f, 0.0f};

    failures += check("Parallel pole fallback",
        R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 1));

    failures += check("Fallback still reaches target",
        near_vec3(position(modelPose[0]), params.targetModel));

    // ========================================
    // ROTATED, NONUNIFORM PARENT
    // ========================================

    Transform parentTransform = {
        .translation = {1, 0, 0},
        .rotation = QuaternionFromAxisAngle((Vector3){0, 0, 1}, 0.6f),
        .scale = {2, 3, 1}
    };

    failures += check("Set nonuniform parent",
        R3D_SetAnimationBoneLocalTransform(&player, 3, parentTransform) &&
        restore_chain(&player));

    Vector3 shoulder = position(modelPose[1]);

    params.targetModel = Vector3Add(shoulder, (Vector3){1, 2, 1});
    params.poleModel = Vector3Add(shoulder, (Vector3){0, 0, 5});

    Matrix siblingBefore = modelPose[4];

    failures += check("IK under nonuniform parent",
        R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 1));

    failures += check("Nonuniform chain reaches target",
        near_vec3(position(modelPose[0]), params.targetModel));

    failures += check("Nonuniform solve preserves sibling",
        R3D_MatrixEquals(modelPose[4], siblingBefore));

    // ========================================
    // SHEAR PRESERVATION
    // ========================================

    R3D_SetAnimationBoneLocalMatrix(&player, 3, MatrixTranslate(1, 0, 0));
    restore_chain(&player);

    Matrix shearedShoulder = localPose[1];
    shearedShoulder.m4 = 0.25f;

    R3D_SetAnimationBoneLocalMatrix(&player, 1, shearedShoulder);
    R3D_RecomputeAnimationModelPose(&player);

    Matrix beforeShear = modelPose[1];

    Vector3 xBefore = basis_axis(beforeShear, (Vector3){1, 0, 0});
    Vector3 yBefore = basis_axis(beforeShear, (Vector3){0, 1, 0});

    params.targetModel = (Vector3){12, 1, 0};
    params.poleModel = (Vector3){11, 0, 3};

    failures += check("IK accepts sheared chain",
        R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 1));

    Vector3 xAfter = basis_axis(modelPose[1], (Vector3){1, 0, 0});
    Vector3 yAfter = basis_axis(modelPose[1], (Vector3){0, 1, 0});

    failures += check("Model-space shear and scale preserved",
        near(Vector3Length(xBefore), Vector3Length(xAfter)) &&
        near(Vector3Length(yBefore), Vector3Length(yAfter)) &&
        near(Vector3DotProduct(xBefore, yBefore), Vector3DotProduct(xAfter, yAfter)));

    failures += check("Sheared chain reaches target",
        near_vec3(position(modelPose[0]), params.targetModel));

    // ========================================
    // VALIDATION
    // ========================================

    Matrix savedRoot = localPose[1];
    Matrix savedMiddle = localPose[2];

    failures += check("Negative bone index rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, -1, 2, 0, params));

    failures += check("Out-of-range index rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 5, params));

    failures += check("Invalid hierarchy rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, 1, 3, 0, params));

    failures += check("NULL player rejected",
        !R3D_ApplyAnimationTwoBoneIK(NULL, 1, 2, 0, params));

    params.weight = -0.1f;

    failures += check("Negative weight rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params));

    params.weight = 1.1f;

    failures += check("Weight over one rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params));

    params.weight = 1.0f;
    params.targetModel = position(modelPose[1]);

    failures += check("Coincident target rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params));

    params.targetModel.x = NAN;

    failures += check("Non-finite target rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params));

    failures += check("Invalid calls preserve local pose",
        R3D_MatrixEquals(localPose[1], savedRoot) &&
        R3D_MatrixEquals(localPose[2], savedMiddle));

    printf("\nTwo-bone IK tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}