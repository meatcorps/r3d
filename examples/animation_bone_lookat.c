#include <r3d/r3d_animation_lookat.h>
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

static bool near_vec3(Vector3 a, Vector3 b)
{
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

static Vector3 direction(Matrix matrix, Vector3 axis)
{
    Vector3 origin = Vector3Transform(Vector3Zero(), matrix);
    return Vector3Normalize(Vector3Subtract(Vector3Transform(axis, matrix), origin));
}

static bool aimed_at(Matrix bone, Vector3 axis, Vector3 target)
{
    Vector3 origin = {bone.m12, bone.m13, bone.m14};
    Vector3 wanted = Vector3Normalize(Vector3Subtract(target, origin));
    return Vector3DotProduct(direction(bone, axis), wanted) > 0.999f;
}

int main(void)
{
    // Deliberately unordered: Hand(0), Head(1), Root(2).
    R3D_BoneInfo bones[3] = {
        {.name = "Hand", .parent = 2},
        {.name = "Head", .parent = 2},
        {.name = "Root", .parent = -1}
    };

    Matrix localPose[3] = {
        MatrixTranslate(0.0f, 1.0f, 0.0f),
        MatrixTranslate(0.0f, 2.0f, 0.0f),
        MatrixTranslate(3.0f, 0.0f, 0.0f)
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

    R3D_BoneLookAtParams params = {
        .localForward = {0.0f, 0.0f, 1.0f},
        .localUp = {0.0f, 1.0f, 0.0f},
        .modelUp = {0.0f, 1.0f, 0.0f},
        .weight = 1.0f,
        .maxAngleRadians = 0.0f
    };

    int failures = 0;
    Matrix originalHand = localPose[0];

    failures += check("Compute initial hierarchy",
        R3D_RecomputeAnimationModelPose(&player));

    Matrix originalHeadModel = modelPose[1];
    Vector3 target = {23.0f, 1.0f, 0.0f};

    failures += check("Apply full LookAt",
        R3D_ApplyAnimationBoneLookAt(&player, 0, target, params));

    failures += check("LookAt does not automatically recompute",
        near_vec3(direction(modelPose[0], (Vector3){0, 0, 1}), (Vector3){0, 0, 1}));

    failures += check("Recompute LookAt subtree",
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0));

    failures += check("Bone points toward target",
        aimed_at(modelPose[0], params.localForward, target));

    failures += check("Bone translation preserved",
        near_vec3((Vector3){modelPose[0].m12, modelPose[0].m13, modelPose[0].m14},
                  (Vector3){13.0f, 1.0f, 0.0f}));

    failures += check("Sibling pose preserved",
        near_vec3((Vector3){modelPose[1].m12, modelPose[1].m13, modelPose[1].m14},
                  (Vector3){originalHeadModel.m12, originalHeadModel.m13, originalHeadModel.m14}));

    // Weight zero: no modification.
    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalHand);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);

    Matrix before = localPose[0];
    params.weight = 0.0f;

    failures += check("Zero weight succeeds",
        R3D_ApplyAnimationBoneLookAt(&player, 0, target, params));

    failures += check("Zero weight preserves pose",
        near_vec3(direction(localPose[0], (Vector3){0, 0, 1}),
                  direction(before, (Vector3){0, 0, 1})));

    // Half weight should produce roughly 45 degrees from +Z to +X.
    params.weight = 0.5f;

    failures += check("Apply half-weight LookAt",
        R3D_ApplyAnimationBoneLookAt(&player, 0, target, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0));

    Vector3 halfForward = direction(modelPose[0], params.localForward);

    failures += check("Half weight produces 45 degree aim",
        near(halfForward.x, 0.707107f) &&
        near(halfForward.z, 0.707107f));

    // A 30-degree limit should restrict the 90-degree target correction.
    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalHand);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);

    params.weight = 1.0f;
    params.maxAngleRadians = PI / 6.0f;

    failures += check("Apply limited LookAt",
        R3D_ApplyAnimationBoneLookAt(&player, 0, target, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0));

    Vector3 limitedForward = direction(modelPose[0], params.localForward);

    failures += check("30-degree limit respected",
        near(limitedForward.x, 0.5f) &&
        near(limitedForward.z, 0.866025f));

    // Different rigs can define forward and up on different axes.
    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalHand);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);

    params.maxAngleRadians = 0.0f;
    params.localForward = (Vector3){1, 0, 0};
    params.localUp = (Vector3){0, 0, 1};

    Vector3 zTarget = {13, 1, 10};

    failures += check("Configurable forward/up axes",
        R3D_ApplyAnimationBoneLookAt(&player, 0, zTarget, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0) &&
        aimed_at(modelPose[0], params.localForward, zTarget));

    failures += check("Local up aligns with model up",
        near_vec3(direction(modelPose[0], params.localUp), (Vector3){0, 1, 0}));

    // Opposite-direction target: a 180-degree rotation.
    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalHand);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);

    params.localForward = (Vector3){0, 0, 1};
    params.localUp = (Vector3){0, 1, 0};

    failures += check("Opposite direction LookAt",
        R3D_ApplyAnimationBoneLookAt(&player, 0, (Vector3){13, 1, -10}, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0) &&
        near_vec3(direction(modelPose[0], params.localForward), (Vector3){0, 0, -1}));

    // Target parallel to reference up: should use a fallback.
    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalHand);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);

    failures += check("Vertical target fallback",
        R3D_ApplyAnimationBoneLookAt(&player, 0, (Vector3){13, 11, 0}, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0) &&
        aimed_at(modelPose[0], params.localForward, (Vector3){13, 11, 0}));

    // Verify correct targeting with a rotated, nonuniformly scaled parent.
    Transform root = {
        .translation = {3, 0, 0},
        .rotation = QuaternionFromAxisAngle((Vector3){0, 0, 1}, PI / 4.0f),
        .scale = {2, 3, 1}
    };

    R3D_SetAnimationBoneLocalTransform(&player, 2, root);
    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalHand);
    R3D_RecomputeAnimationModelPose(&player);

    Vector3 handOrigin = {modelPose[0].m12, modelPose[0].m13, modelPose[0].m14};
    Vector3 scaledTarget = Vector3Add(handOrigin, (Vector3){5, 0, 0});

    failures += check("LookAt with nonuniform parent",
        R3D_ApplyAnimationBoneLookAt(&player, 0, scaledTarget, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0) &&
        aimed_at(modelPose[0], params.localForward, scaledTarget));

    // Reset parent. Verify that an existing sheared local matrix
    // retains its translation, lengths and internal shear.
    R3D_SetAnimationBoneLocalMatrix(&player, 2, MatrixTranslate(3, 0, 0));

    Matrix shearedHand = originalHand;
    shearedHand.m0 = 2.0f;
    shearedHand.m4 = 0.35f;

    R3D_SetAnimationBoneLocalMatrix(&player, 0, shearedHand);
    R3D_RecomputeAnimationModelPose(&player);

    Vector3 xBefore = Vector3Subtract(Vector3Transform((Vector3){1, 0, 0}, shearedHand),
                                      Vector3Transform(Vector3Zero(), shearedHand));
    Vector3 yBefore = Vector3Subtract(Vector3Transform((Vector3){0, 1, 0}, shearedHand),
                                      Vector3Transform(Vector3Zero(), shearedHand));

    failures += check("LookAt preserves sheared matrix",
        R3D_ApplyAnimationBoneLookAt(&player, 0, target, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0));

    Vector3 xAfter = Vector3Subtract(Vector3Transform((Vector3){1, 0, 0}, localPose[0]),
                                     Vector3Transform(Vector3Zero(), localPose[0]));
    Vector3 yAfter = Vector3Subtract(Vector3Transform((Vector3){0, 1, 0}, localPose[0]),
                                     Vector3Transform(Vector3Zero(), localPose[0]));

    failures += check("Shear and scale preserved",
        near(Vector3Length(xBefore), Vector3Length(xAfter)) &&
        near(Vector3Length(yBefore), Vector3Length(yAfter)) &&
        near(Vector3DotProduct(xBefore, yBefore), Vector3DotProduct(xAfter, yAfter)));

    failures += check("Sheared bone still aims correctly",
        aimed_at(modelPose[0], params.localForward, target));

    // Invalid requests must not modify the current pose.
    Matrix saved = localPose[0];

    params.localForward = Vector3Zero();

    failures += check("Zero forward axis rejected",
        !R3D_ApplyAnimationBoneLookAt(&player, 0, target, params));

    params.localForward = (Vector3){0, 0, 1};
    params.localUp = params.localForward;

    failures += check("Parallel local axes rejected",
        !R3D_ApplyAnimationBoneLookAt(&player, 0, target, params));

    params.localUp = (Vector3){0, 1, 0};
    params.weight = 1.5f;

    failures += check("Invalid weight rejected",
        !R3D_ApplyAnimationBoneLookAt(&player, 0, target, params));

    params.weight = 1.0f;
    params.maxAngleRadians = -1.0f;

    failures += check("Negative angle limit rejected",
        !R3D_ApplyAnimationBoneLookAt(&player, 0, target, params));

    params.maxAngleRadians = 0.0f;

    failures += check("Coincident target rejected",
        !R3D_ApplyAnimationBoneLookAt(&player, 0,
            (Vector3){modelPose[0].m12, modelPose[0].m13, modelPose[0].m14}, params));

    failures += check("Invalid bone rejected",
        !R3D_ApplyAnimationBoneLookAt(&player, -1, target, params) &&
        !R3D_ApplyAnimationBoneLookAt(&player, 3, target, params));

    failures += check("NULL player rejected",
        !R3D_ApplyAnimationBoneLookAt(NULL, 0, target, params));

    failures += check("Invalid calls preserve local pose",
        near(saved.m0, localPose[0].m0) &&
        near(saved.m4, localPose[0].m4) &&
        near(saved.m12, localPose[0].m12));

    printf("\nBone LookAt tests: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}