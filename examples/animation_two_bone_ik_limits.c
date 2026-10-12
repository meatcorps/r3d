#include <r3d/r3d_animation_ik.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

enum {
    WRIST = 0,
    SHOULDER = 1,
    ELBOW = 2,
    ROOT = 3,
    BONE_COUNT = 4
};

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
    return Vector3Distance(a, b) < 0.003f;
}

static Vector3 position(Matrix matrix)
{
    return (Vector3){matrix.m12, matrix.m13, matrix.m14};
}

static float bend_angle(const R3D_AnimationPlayer* player)
{
    Vector3 shoulder = position(player->modelPose[SHOULDER]);
    Vector3 elbow = position(player->modelPose[ELBOW]);
    Vector3 wrist = position(player->modelPose[WRIST]);

    Vector3 upper = Vector3Normalize(Vector3Subtract(elbow, shoulder));
    Vector3 lower = Vector3Normalize(Vector3Subtract(wrist, elbow));

    return acosf(Clamp(Vector3DotProduct(upper, lower), -1.0f, 1.0f));
}

static bool restore_chain(R3D_AnimationPlayer* player)
{
    return
        R3D_SetAnimationBoneLocalMatrix(player, ROOT, MatrixTranslate(1, 0, 0)) &&
        R3D_SetAnimationBoneLocalMatrix(player, SHOULDER, MatrixIdentity()) &&
        R3D_SetAnimationBoneLocalMatrix(player, ELBOW, MatrixTranslate(1, 0, 0)) &&
        R3D_SetAnimationBoneLocalMatrix(player, WRIST, MatrixTranslate(1, 0, 0)) &&
        R3D_RecomputeAnimationModelPose(player);
}

static bool solve(R3D_AnimationPlayer* player, R3D_TwoBoneIKParams params)
{
    return R3D_ApplyAnimationTwoBoneIK(player, SHOULDER, ELBOW, WRIST, params) &&
           R3D_RecomputeAnimationModelPoseFromBone(player, SHOULDER);
}

int main(void)
{
    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "Wrist", .parent = ELBOW},
        {.name = "Shoulder", .parent = ROOT},
        {.name = "Elbow", .parent = SHOULDER},
        {.name = "Root", .parent = -1}
    };

    Matrix localPose[BONE_COUNT] = {
        MatrixTranslate(1, 0, 0),
        MatrixIdentity(),
        MatrixTranslate(1, 0, 0),
        MatrixTranslate(1, 0, 0)
    };

    Matrix modelPose[BONE_COUNT] = {
        MatrixIdentity(), MatrixIdentity(),
        MatrixIdentity(), MatrixIdentity()
    };

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = BONE_COUNT,
            .rootBind = MatrixTranslate(10, 0, 0)
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    R3D_TwoBoneIKParams params = {
        .targetModel = {12, 1, 0},
        .poleModel = {11, 0, 3},
        .weight = 1.0f
    };

    int failures = 0;

    failures += check("Initial hierarchy",
        restore_chain(&player));

    // ========================================
    // 1. BACKWARD COMPATIBILITY
    // ========================================

    failures += check("Unconstrained IK",
        solve(&player, params));

    failures += check("Unconstrained wrist reaches target",
        near_vec3(position(modelPose[WRIST]), params.targetModel));

    failures += check("Unconstrained bend is 90 degrees",
        near(bend_angle(&player), PI / 2.0f));

    // ========================================
    // 2. MAXIMUM BEND LIMIT
    // ========================================

    restore_chain(&player);

    params.targetModel = (Vector3){11, 1, 0};
    params.bend.enabled = true;
    params.bend.minAngleRadians = 0.0f;
    params.bend.maxAngleRadians = PI / 3.0f;

    // At 60 degrees, two unit-length segments have
    // a combined reach of sqrt(3).

    failures += check("60-degree bend limit accepted",
        solve(&player, params));

    failures += check("Elbow stops at 60 degrees",
        near(bend_angle(&player), PI / 3.0f));

    failures += check("Constrained wrist stops at allowed reach",
        near_vec3(position(modelPose[WRIST]),
                  (Vector3){11, sqrtf(3.0f), 0}));

    // ========================================
    // 3. MINIMUM BEND LIMIT
    // ========================================

    restore_chain(&player);

    params.targetModel = (Vector3){13, 0, 0};
    params.bend.minAngleRadians = PI / 6.0f;
    params.bend.maxAngleRadians = PI;

    const float minimumBendReach = sqrtf(
        2.0f + 2.0f * cosf(PI / 6.0f)
    );

    failures += check("30-degree minimum bend accepted",
        solve(&player, params));

    failures += check("Elbow does not straighten below 30 degrees",
        near(bend_angle(&player), PI / 6.0f));

    failures += check("Wrist respects minimum bend distance",
        near_vec3(position(modelPose[WRIST]),
                  (Vector3){11.0f + minimumBendReach, 0, 0}));

    // ========================================
    // 4. COMPLETELY LOCKED STRAIGHT HINGE
    // ========================================

    restore_chain(&player);

    params.targetModel = (Vector3){11, 1, 0};
    params.bend.minAngleRadians = 0.0f;
    params.bend.maxAngleRadians = 0.0f;

    Matrix endLocalBefore = localPose[WRIST];
    Matrix endModelBefore = modelPose[WRIST];

    failures += check("Locked straight hinge accepted",
        R3D_ApplyAnimationTwoBoneIK(&player, SHOULDER, ELBOW, WRIST, params));

    failures += check("IK remains deferred",
        R3D_MatrixEquals(modelPose[WRIST], endModelBefore));

    failures += check("Locked hinge preserves wrist local matrix",
        R3D_MatrixEquals(localPose[WRIST], endLocalBefore));

    failures += check("Recompute locked chain",
        R3D_RecomputeAnimationModelPoseFromBone(&player, SHOULDER));

    failures += check("Locked elbow cannot bend",
        near(bend_angle(&player), 0.0f));

    failures += check("Locked arm rotates as straight chain",
        near_vec3(position(modelPose[WRIST]), (Vector3){11, 2, 0}));

    // ========================================
    // 5. LOCKED 90-DEGREE BEND
    // ========================================

    restore_chain(&player);

    params.targetModel = (Vector3){13, 0, 0};
    params.bend.minAngleRadians = PI / 2.0f;
    params.bend.maxAngleRadians = PI / 2.0f;

    failures += check("Locked 90-degree hinge accepted",
        solve(&player, params));

    failures += check("Locked elbow retains 90-degree bend",
        near(bend_angle(&player), PI / 2.0f));

    failures += check("Fixed bend limits wrist reach",
        near_vec3(position(modelPose[WRIST]),
                  (Vector3){11 + sqrtf(2.0f), 0, 0}));

    // ========================================
    // 6. FULLY FOLDED HINGE
    // ========================================

    restore_chain(&player);

    params.targetModel = (Vector3){12, 0, 0};
    params.bend.minAngleRadians = PI;
    params.bend.maxAngleRadians = PI;

    failures += check("Fully folded hinge accepted",
        solve(&player, params));

    failures += check("Elbow folds 180 degrees",
        near(bend_angle(&player), PI));

    failures += check("Equal-length folded wrist returns to shoulder",
        near_vec3(position(modelPose[WRIST]),
                  position(modelPose[SHOULDER])));

    // ========================================
    // 7. REACHABLE TARGET WITHIN ANGLE RANGE
    // ========================================

    restore_chain(&player);

    params.targetModel = (Vector3){12, 1, 0};
    params.bend.minAngleRadians = PI / 6.0f;
    params.bend.maxAngleRadians = 2.0f * PI / 3.0f;

    failures += check("Target within bend limits",
        solve(&player, params));

    failures += check("Reachable constrained target reached",
        near_vec3(position(modelPose[WRIST]), params.targetModel));

    failures += check("Bend remains inside allowed range",
        bend_angle(&player) >= params.bend.minAngleRadians - 0.003f &&
        bend_angle(&player) <= params.bend.maxAngleRadians + 0.003f);

    // ========================================
    // 8. ZERO WEIGHT
    // ========================================

    restore_chain(&player);

    params.weight = 0.0f;
    params.bend.minAngleRadians = 0.0f;
    params.bend.maxAngleRadians = 0.0f;

    Matrix savedShoulder = localPose[SHOULDER];
    Matrix savedElbow = localPose[ELBOW];

    failures += check("Zero weight accepted with bend limits",
        R3D_ApplyAnimationTwoBoneIK(&player, SHOULDER, ELBOW, WRIST, params));

    failures += check("Zero weight preserves authored local pose",
        R3D_MatrixEquals(localPose[SHOULDER], savedShoulder) &&
        R3D_MatrixEquals(localPose[ELBOW], savedElbow));

    // ========================================
    // 9. INVALID LIMITS
    // ========================================

    params.weight = 1.0f;

    params.bend.minAngleRadians = -0.1f;

    failures += check("Negative minimum rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, SHOULDER, ELBOW, WRIST, params));

    params.bend.minAngleRadians = 0.0f;
    params.bend.maxAngleRadians = PI + 0.1f;

    failures += check("Maximum above 180 degrees rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, SHOULDER, ELBOW, WRIST, params));

    params.bend.minAngleRadians = 1.0f;
    params.bend.maxAngleRadians = 0.5f;

    failures += check("Inverted limits rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, SHOULDER, ELBOW, WRIST, params));

    params.bend.minAngleRadians = NAN;
    params.bend.maxAngleRadians = PI;

    failures += check("Nonfinite limit rejected",
        !R3D_ApplyAnimationTwoBoneIK(&player, SHOULDER, ELBOW, WRIST, params));

    failures += check("Rejected limits do not mutate pose",
        R3D_MatrixEquals(localPose[SHOULDER], savedShoulder) &&
        R3D_MatrixEquals(localPose[ELBOW], savedElbow));

    // ========================================
    // 10. DISABLED LIMITS
    // ========================================

    restore_chain(&player);

    params.bend.enabled = false;
    params.targetModel = (Vector3){12, 1, 0};

    failures += check("Disabling constraint restores free IK",
        solve(&player, params));

    failures += check("Free IK reaches the original target",
        near_vec3(position(modelPose[WRIST]), params.targetModel));

    printf("\nTwo-bone IK bend-limit tests: %s\n",
           failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}