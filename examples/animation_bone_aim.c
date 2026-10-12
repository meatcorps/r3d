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

static Vector3 matrix_direction(Matrix matrix, Vector3 axis)
{
    Vector3 origin = Vector3Transform(Vector3Zero(), matrix);
    Vector3 direction = Vector3Subtract(Vector3Transform(axis, matrix), origin);
    return Vector3Normalize(direction);
}

static bool aimed_at(Matrix matrix, Vector3 localForward, Vector3 target)
{
    Vector3 origin = {matrix.m12, matrix.m13, matrix.m14};
    Vector3 desired = Vector3Normalize(Vector3Subtract(target, origin));
    return Vector3DotProduct(matrix_direction(matrix, localForward), desired) > 0.999f;
}

int main(void)
{
    // Unordered hierarchy: Aim(0), Other(1), Root(2).
    R3D_BoneInfo bones[3] = {
        {.name = "Aim", .parent = 2},
        {.name = "Other", .parent = 2},
        {.name = "Root", .parent = -1}
    };

    Matrix localPose[3] = {
        MatrixTranslate(0.0f, 1.0f, 0.0f),
        MatrixTranslate(0.0f, 2.0f, 0.0f),
        MatrixTranslate(3.0f, 0.0f, 0.0f)
    };

    Matrix modelPose[3] = {MatrixIdentity(), MatrixIdentity(), MatrixIdentity()};

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

    R3D_BoneAimParams params = {
        .localForward = {0.0f, 0.0f, 1.0f},
        .localUp = {0.0f, 1.0f, 0.0f},
        .weight = 1.0f,
        .maxAngleRadians = 0.0f
    };

    int failures = 0;
    const Vector3 targetX = {23.0f, 1.0f, 0.0f};
    const Vector3 targetZ = {13.0f, 1.0f, 10.0f};
    const Vector3 targetMinusZ = {13.0f, 1.0f, -10.0f};

    // Authored animation includes 45 degrees of roll around local forward.
    Transform authored = {
        .translation = {0.0f, 1.0f, 0.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0, 0, 1}, PI / 4.0f),
        .scale = {1.0f, 1.0f, 1.0f}
    };

    failures += check("Set rolled authored pose", R3D_SetAnimationBoneLocalTransform(&player, 0, authored));
    Matrix originalLocal = localPose[0];

    failures += check("Initial hierarchy computation", R3D_RecomputeAnimationModelPose(&player));
    Matrix otherModel = modelPose[1];
    Matrix oldAimModel = modelPose[0];

    failures += check("Aim toward target", R3D_ApplyAnimationBoneAim(&player, 0, targetX, params));
    failures += check("Aim does not automatically recompute", near_matrix(modelPose[0], oldAimModel));
    failures += check("Recompute aimed subtree", R3D_RecomputeAnimationModelPoseFromBone(&player, 0));
    failures += check("Forward points to target", aimed_at(modelPose[0], params.localForward, targetX));
    failures += check("Bone origin preserved", near_vec3((Vector3){modelPose[0].m12, modelPose[0].m13, modelPose[0].m14}, (Vector3){13, 1, 0}));
    failures += check("Sibling unchanged", near_matrix(modelPose[1], otherModel));

    // Verify shortest-arc aiming doesn't remove authored roll/twist.
    const Quaternion expectedSwing = QuaternionFromAxisAngle((Vector3){0, 1, 0}, PI / 2.0f);
    const Vector3 originalUp = matrix_direction(originalLocal, params.localUp);
    const Vector3 expectedUp = Vector3RotateByQuaternion(originalUp, expectedSwing);
    const Vector3 actualUp = matrix_direction(modelPose[0], params.localUp);

    failures += check("Authored roll carried through swing", near_vec3(expectedUp, actualUp));

    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalLocal);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);

    failures += check("Aligned aim succeeds", R3D_ApplyAnimationBoneAim(&player, 0, targetZ, params));
    failures += check("Aligned aim preserves authored roll", near_matrix(localPose[0], originalLocal));

    // Same target with LookAt would use modelUp to adjust roll.
    R3D_BoneLookAtParams lookAt = {
        .localForward = {0, 0, 1},
        .localUp = {0, 1, 0},
        .modelUp = {0, 1, 0},
        .weight = 1.0f,
        .maxAngleRadians = 0.0f
    };

    failures += check("LookAt resolves roll differently",
        R3D_ApplyAnimationBoneLookAt(&player, 0, targetZ, lookAt) &&
        !near_matrix(localPose[0], originalLocal));

    // Zero weight leaves animation intact.
    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalLocal);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);
    params.weight = 0.0f;

    failures += check("Zero weight accepted", R3D_ApplyAnimationBoneAim(&player, 0, targetX, params));
    failures += check("Zero weight leaves authored pose", near_matrix(localPose[0], originalLocal));

    params.weight = 0.5f;
    failures += check("Half weight applied", R3D_ApplyAnimationBoneAim(&player, 0, targetX, params) && R3D_RecomputeAnimationModelPoseFromBone(&player, 0));
    Vector3 halfForward = matrix_direction(modelPose[0], params.localForward);
    failures += check("Half weight produces 45-degree swing", near(halfForward.x, 0.707107f) && near(halfForward.z, 0.707107f));

    // A 30-degree limit applies to the directional correction.
    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalLocal);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);
    params.weight = 1.0f;
    params.maxAngleRadians = PI / 6.0f;

    failures += check("Limited aim applied", R3D_ApplyAnimationBoneAim(&player, 0, targetX, params) && R3D_RecomputeAnimationModelPoseFromBone(&player, 0));
    Vector3 limited = matrix_direction(modelPose[0], params.localForward);
    failures += check("30-degree limit respected", near(limited.x, 0.5f) && near(limited.z, 0.866025f));

    // Antiparallel forward must not create a zero quaternion/NaN.
    R3D_SetAnimationBoneLocalMatrix(&player, 0, originalLocal);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);
    params.maxAngleRadians = 0.0f;

    failures += check("Opposite-direction aim succeeds",
        R3D_ApplyAnimationBoneAim(&player, 0, targetMinusZ, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0));
    failures += check("Opposite-direction aim aligns", aimed_at(modelPose[0], params.localForward, targetMinusZ));

    // Different rig axes.
    R3D_SetAnimationBoneLocalMatrix(&player, 0, MatrixTranslate(0, 1, 0));
    R3D_RecomputeAnimationModelPoseFromBone(&player, 0);
    params.localForward = (Vector3){1, 0, 0};
    params.localUp = (Vector3){0, 0, 1};

    failures += check("Configurable forward axis",
        R3D_ApplyAnimationBoneAim(&player, 0, targetZ, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0) &&
        aimed_at(modelPose[0], params.localForward, targetZ));

    // Rotated, nonuniform parent with a sheared local bone matrix.
    Transform root = {
        .translation = {3, 0, 0},
        .rotation = QuaternionFromAxisAngle((Vector3){0, 0, 1}, 0.4f),
        .scale = {2, 3, 4}
    };

    R3D_SetAnimationBoneLocalTransform(&player, 2, root);
    Matrix sheared = MatrixTranslate(0, 1, 0);
    sheared.m0 = 2.0f;
    sheared.m4 = 0.35f;
    R3D_SetAnimationBoneLocalMatrix(&player, 0, sheared);
    R3D_RecomputeAnimationModelPose(&player);

    params.localForward = (Vector3){0, 0, 1};
    params.localUp = (Vector3){0, 1, 0};
    Vector3 position = {modelPose[0].m12, modelPose[0].m13, modelPose[0].m14};
    Vector3 scaledTarget = Vector3Add(position, (Vector3){5, 3, 2});

    Vector3 xBefore = matrix_direction(sheared, (Vector3){1, 0, 0});
    Vector3 yBefore = matrix_direction(sheared, (Vector3){0, 1, 0});
    const float xLength = Vector3Length(Vector3Subtract(Vector3Transform((Vector3){1, 0, 0}, sheared), Vector3Transform(Vector3Zero(), sheared)));
    const float yLength = Vector3Length(Vector3Subtract(Vector3Transform((Vector3){0, 1, 0}, sheared), Vector3Transform(Vector3Zero(), sheared)));
    const float shearAngle = Vector3DotProduct(xBefore, yBefore);

    failures += check("Aim with nonuniform parent and shear",
        R3D_ApplyAnimationBoneAim(&player, 0, scaledTarget, params) &&
        R3D_RecomputeAnimationModelPoseFromBone(&player, 0) &&
        aimed_at(modelPose[0], params.localForward, scaledTarget));

    Vector3 xAfter = Vector3Subtract(Vector3Transform((Vector3){1, 0, 0}, localPose[0]), Vector3Transform(Vector3Zero(), localPose[0]));
    Vector3 yAfter = Vector3Subtract(Vector3Transform((Vector3){0, 1, 0}, localPose[0]), Vector3Transform(Vector3Zero(), localPose[0]));

    failures += check("Aim preserves local scale and shear",
        near(Vector3Length(xAfter), xLength) &&
        near(Vector3Length(yAfter), yLength) &&
        near(Vector3DotProduct(Vector3Normalize(xAfter), Vector3Normalize(yAfter)), shearAngle));
    failures += check("Aim preserves local translation",
        near(localPose[0].m12, sheared.m12) && near(localPose[0].m13, sheared.m13) && near(localPose[0].m14, sheared.m14));

    // Invalid requests must leave the local matrix untouched.
    Matrix saved = localPose[0];
    params.localForward = Vector3Zero();
    failures += check("Zero forward rejected", !R3D_ApplyAnimationBoneAim(&player, 0, scaledTarget, params));

    params.localForward = (Vector3){0, 0, 1};
    params.localUp = params.localForward;
    failures += check("Parallel axes rejected", !R3D_ApplyAnimationBoneAim(&player, 0, scaledTarget, params));

    params.localUp = (Vector3){0, 1, 0};
    params.weight = 1.5f;
    failures += check("Invalid weight rejected", !R3D_ApplyAnimationBoneAim(&player, 0, scaledTarget, params));

    params.weight = 1.0f;
    params.maxAngleRadians = -0.25f;
    failures += check("Negative angle limit rejected", !R3D_ApplyAnimationBoneAim(&player, 0, scaledTarget, params));

    params.maxAngleRadians = 0.0f;
    failures += check("Coincident target rejected", !R3D_ApplyAnimationBoneAim(&player, 0, position, params));
    failures += check("Invalid indices rejected", !R3D_ApplyAnimationBoneAim(&player, -1, scaledTarget, params) && !R3D_ApplyAnimationBoneAim(&player, 3, scaledTarget, params));
    failures += check("NULL player rejected", !R3D_ApplyAnimationBoneAim(NULL, 0, scaledTarget, params));
    failures += check("Invalid inputs did not modify pose", near_matrix(saved, localPose[0]));

    // Singular parent cannot be used for parent-space target conversion.
    Transform singular = root;
    singular.scale.x = 0.0f;
    R3D_SetAnimationBoneLocalTransform(&player, 2, singular);
    R3D_RecomputeAnimationModelPoseFromBone(&player, 2);
    failures += check("Singular parent rejected", !R3D_ApplyAnimationBoneAim(&player, 0, scaledTarget, params));

    printf("\nBone Aim tests: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");
    return failures == 0 ? 0 : 1;
}
