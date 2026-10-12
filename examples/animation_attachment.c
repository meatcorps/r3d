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

static Vector3 matrix_position(Matrix matrix)
{
    return Vector3Transform(Vector3Zero(), matrix);
}

static Vector3 matrix_direction(Matrix matrix, Vector3 direction)
{
    return Vector3Subtract(Vector3Transform(direction, matrix), matrix_position(matrix));
}

static bool get_attachment_world(const R3D_AnimationPlayer* player, int boneIndex, Matrix socketOffset, Matrix characterWorld, Matrix* outMatrix)
{
    if (!outMatrix) return false;

    Matrix boneModel;

    if (!R3D_GetAnimationBoneModelMatrix(player, boneIndex, &boneModel)) return false;

    Matrix socketModel = MatrixMultiply(socketOffset, boneModel);
    *outMatrix = MatrixMultiply(socketModel, characterWorld);

    return true;
}

int main(void)
{
    // Deliberately unordered skeleton:
    //
    // Root (2)
    //   RightHand (0)
    //   Head (1)

    R3D_BoneInfo bones[3] = {
        {.name = "RightHand", .parent = 2},
        {.name = "Head", .parent = 2},
        {.name = "Root", .parent = -1}
    };

    Matrix localPose[3] = {
        MatrixIdentity(),
        MatrixTranslate(0.0f, 2.0f, 0.0f),
        MatrixTranslate(1.0f, 0.0f, 0.0f)
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
            .rootBind = MatrixTranslate(2.0f, 0.0f, 0.0f)
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    int failures = 0;

    const int handBone = R3D_GetSkeletonBoneIndex(player.skeleton, "RightHand");
    const int headBone = R3D_GetSkeletonBoneIndex(player.skeleton, "Head");

    failures += check("Hand bone lookup", handBone == 0);
    failures += check("Head bone lookup", headBone == 1);
    failures += check("Missing bone lookup", R3D_GetSkeletonBoneIndex(player.skeleton, "Missing") == -1);

    // Character is rotated 90 degrees and placed at X=10.
    Matrix characterWorld = MatrixMultiply(MatrixRotateZ(PI / 2.0f), MatrixTranslate(10.0f, 0.0f, 0.0f));

    // Flashlight is mounted 0.5 units along the hand's local X axis.
    Matrix flashlightOffset = MatrixTranslate(0.5f, 0.0f, 0.0f);
    Matrix headOffset = MatrixIdentity();

    Transform hand = {
        .translation = {0.0f, 1.0f, 0.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 0.0f, 1.0f}, PI / 2.0f),
        .scale = {1.0f, 1.0f, 1.0f}
    };

    failures += check("Set hand pose",
        R3D_SetAnimationBoneLocalTransform(&player, handBone, hand));

    failures += check("Compute animated hierarchy",
        R3D_RecomputeAnimationModelPose(&player));

    Matrix flashlightWorld = {0};
    Matrix headWorld = {0};

    failures += check("Query flashlight attachment",
        get_attachment_world(&player, handBone, flashlightOffset, characterWorld, &flashlightWorld));

    failures += check("Query head attachment",
        get_attachment_world(&player, headBone, headOffset, characterWorld, &headWorld));

    failures += check("Flashlight initial world position",
        near_vec3(matrix_position(flashlightWorld), (Vector3){8.5f, 3.0f, 0.0f}));

    failures += check("Head initial world position",
        near_vec3(matrix_position(headWorld), (Vector3){8.0f, 3.0f, 0.0f}));

    failures += check("Flashlight inherits hand and character rotation",
        near_vec3(matrix_direction(flashlightWorld, (Vector3){1.0f, 0.0f, 0.0f}),
                  (Vector3){-1.0f, 0.0f, 0.0f}));

    Matrix previousHeadModel = modelPose[headBone];
    Matrix previousRootModel = modelPose[2];

    // Simulate the hand moving to another animation frame.
    hand.translation.y = 2.0f;
    hand.rotation = QuaternionFromAxisAngle((Vector3){0.0f, 0.0f, 1.0f}, PI);

    failures += check("Move and rotate hand",
        R3D_SetAnimationBoneLocalTransform(&player, handBone, hand));

    failures += check("Recompute moving hand",
        R3D_RecomputeAnimationModelPoseFromBone(&player, handBone));

    failures += check("Flashlight follows moving hand",
        get_attachment_world(&player, handBone, flashlightOffset, characterWorld, &flashlightWorld) &&
        near_vec3(matrix_position(flashlightWorld), (Vector3){8.0f, 2.5f, 0.0f}));

    failures += check("Flashlight direction follows rotation",
        near_vec3(matrix_direction(flashlightWorld, (Vector3){1.0f, 0.0f, 0.0f}),
                  (Vector3){0.0f, -1.0f, 0.0f}));

    failures += check("Head bone unaffected",
        near_matrix(modelPose[headBone], previousHeadModel));

    failures += check("Root bone unaffected",
        near_matrix(modelPose[2], previousRootModel));

    failures += check("Head attachment stays in place",
        get_attachment_world(&player, headBone, headOffset, characterWorld, &headWorld) &&
        near_vec3(matrix_position(headWorld), (Vector3){8.0f, 3.0f, 0.0f}));

    // Move the entire character without changing its animated pose.
    characterWorld = MatrixMultiply(MatrixRotateZ(PI / 2.0f), MatrixTranslate(20.0f, 0.0f, 0.0f));

    failures += check("Character world translation propagates",
        get_attachment_world(&player, handBone, flashlightOffset, characterWorld, &flashlightWorld) &&
        near_vec3(matrix_position(flashlightWorld), (Vector3){18.0f, 2.5f, 0.0f}));

    // Test exact matrix composition with nonuniform character scaling.
    Matrix scaledWorld = MatrixMultiply(MatrixScale(2.0f, 1.0f, 1.0f), MatrixTranslate(10.0f, 0.0f, 0.0f));

    failures += check("Nonuniform character transform accepted",
        get_attachment_world(&player, handBone, flashlightOffset, scaledWorld, &flashlightWorld));

    Matrix handModel = {0};
    R3D_GetAnimationBoneModelMatrix(&player, handBone, &handModel);

    Vector3 toolTip = {1.0f, 0.0f, 0.0f};
    Vector3 expectedTip = Vector3Transform(toolTip, flashlightOffset);

    expectedTip = Vector3Transform(expectedTip, handModel);
    expectedTip = Vector3Transform(expectedTip, scaledWorld);

    failures += check("Exact socket matrix composition",
        near_vec3(Vector3Transform(toolTip, flashlightWorld), expectedTip));

    // Input validation.
    Matrix output = {0};

    failures += check("Invalid bone rejected",
        !get_attachment_world(&player, -1, flashlightOffset, characterWorld, &output) &&
        !get_attachment_world(&player, 3, flashlightOffset, characterWorld, &output));

    failures += check("NULL output rejected",
        !get_attachment_world(&player, handBone, flashlightOffset, characterWorld, NULL));

    failures += check("NULL player rejected",
        !get_attachment_world(NULL, handBone, flashlightOffset, characterWorld, &output));

    printf("\nAttachment workflow tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}