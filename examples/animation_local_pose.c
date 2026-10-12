#include <r3d/r3d_animation_player.h>
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
    float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
    return near(fabsf(dot), 1.0f);
}

int main(void)
{
    Matrix localPose[2] = {
        MatrixIdentity(),
        MatrixTranslate(7.0f, 8.0f, 9.0f)
    };

    Matrix modelPose[2] = {
        MatrixIdentity(),
        MatrixTranslate(99.0f, 0.0f, 0.0f)
    };

    R3D_AnimationPlayer player = {
        .skeleton = {.boneCount = 2},
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    int failures = 0;

    Transform result = {0};

    failures += check("Get initial local transform",
        R3D_GetAnimationBoneLocalTransform(&player, 1, &result));

    failures += check("Initial local translation",
        near_vec3(result.translation, (Vector3){7.0f, 8.0f, 9.0f}));

    failures += check("Initial identity rotation",
        near_rotation(result.rotation, QuaternionIdentity()));

    failures += check("Initial unit scale",
        near_vec3(result.scale, (Vector3){1.0f, 1.0f, 1.0f}));

    Transform requested = {
        .translation = {3.0f, 4.0f, 5.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 0.0f, 1.0f}, PI / 2.0f),
        .scale = {2.0f, 3.0f, 4.0f}
    };

    Matrix previousModel = modelPose[1];

    failures += check("Set local transform",
        R3D_SetAnimationBoneLocalTransform(&player, 1, requested));

    Vector3 xAxis = Vector3Transform((Vector3){1.0f, 0.0f, 0.0f}, localPose[1]);
    Vector3 yAxis = Vector3Transform((Vector3){0.0f, 1.0f, 0.0f}, localPose[1]);

    failures += check("X axis rotated and scaled",
        near_vec3(xAxis, (Vector3){3.0f, 6.0f, 5.0f}));

    failures += check("Y axis rotated and scaled",
        near_vec3(yAxis, (Vector3){0.0f, 4.0f, 5.0f}));

    failures += check("Getter after setter",
        R3D_GetAnimationBoneLocalTransform(&player, 1, &result));

    failures += check("Translation round trip",
        near_vec3(result.translation, requested.translation));

    failures += check("Rotation round trip",
        near_rotation(result.rotation, requested.rotation));

    failures += check("Scale round trip",
        near_vec3(result.scale, requested.scale));

    failures += check("Model pose unchanged",
        R3D_MatrixEquals(modelPose[1], previousModel));

    failures += check("Other bone unchanged",
        R3D_MatrixEquals(localPose[0], MatrixIdentity()));

    // Invalid indices and pointers.
    failures += check("Negative index rejected",
        !R3D_GetAnimationBoneLocalTransform(&player, -1, &result) &&
        !R3D_SetAnimationBoneLocalTransform(&player, -1, requested));

    failures += check("Out-of-range index rejected",
        !R3D_GetAnimationBoneLocalTransform(&player, 2, &result) &&
        !R3D_SetAnimationBoneLocalTransform(&player, 2, requested));

    failures += check("NULL arguments rejected",
        !R3D_GetAnimationBoneLocalTransform(NULL, 0, &result) &&
        !R3D_GetAnimationBoneLocalTransform(&player, 0, NULL) &&
        !R3D_SetAnimationBoneLocalTransform(NULL, 0, requested));

    R3D_AnimationPlayer emptyPlayer = player;
    emptyPlayer.localPose = NULL;

    failures += check("Missing pose buffer rejected",
        !R3D_GetAnimationBoneLocalTransform(&emptyPlayer, 0, &result) &&
        !R3D_SetAnimationBoneLocalTransform(&emptyPlayer, 0, requested));

    // A rejected transform should not corrupt the previous local pose.
    Matrix previousLocal = localPose[1];

    Transform invalid = requested;
    invalid.translation.x = NAN;

    failures += check("Non-finite transform rejected",
        !R3D_SetAnimationBoneLocalTransform(&player, 1, invalid) &&
        R3D_MatrixEquals(localPose[1], previousLocal));

    // Reflected transforms should survive a get/set round trip.
    Transform reflected = {
        .translation = {1.0f, 2.0f, 3.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 1.0f, 0.0f}, 0.4f),
        .scale = {-2.0f, 3.0f, 4.0f}
    };

    failures += check("Set reflected transform",
        R3D_SetAnimationBoneLocalTransform(&player, 1, reflected));

    Matrix originalReflection = localPose[1];
    Transform decomposedReflection = {0};

    bool roundTrip = R3D_GetAnimationBoneLocalTransform(&player, 1, &decomposedReflection);

    if (roundTrip)
    {
        roundTrip = R3D_SetAnimationBoneLocalTransform(&player, 0, decomposedReflection);
    }

    failures += check("Reflected matrix round trip",
        roundTrip && R3D_MatrixEquals(localPose[0], originalReflection));

    printf("\nLocal pose access tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}