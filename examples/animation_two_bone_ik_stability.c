#include <r3d/r3d_animation_ik.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

#define WRIST       0
#define SHOULDER    1
#define ELBOW       2
#define ROOT        3

static int check(const char* name, bool condition)
{
    printf("%s: %s\n", name, condition ? "PASS" : "FAIL");
    return condition ? 0 : 1;
}

static bool near(float a, float b, float epsilon)
{
    return fabsf(a - b) <= epsilon;
}

static bool near_vec3(Vector3 a, Vector3 b, float epsilon)
{
    return Vector3Distance(a, b) <= epsilon;
}

static Vector3 position(Matrix matrix)
{
    return (Vector3){matrix.m12, matrix.m13, matrix.m14};
}

static bool finite_matrix(Matrix matrix)
{
    float16 values = MatrixToFloatV(matrix);

    for (int i = 0; i < 16; i++)
    {
        if (!isfinite(values.v[i])) return false;
    }

    return true;
}

static bool solve_sample(
    R3D_AnimationPlayer* player,
    Matrix authoredShoulder,
    Vector3 target,
    Vector3 pole,
    Vector3* outElbow,
    Vector3* outWrist,
    bool* outDeferred)
{
    // A fresh authored pose is evaluated for every sample.
    // Procedural corrections must not accumulate across frames.

    if (!R3D_SetAnimationBoneLocalMatrix(player, ROOT, MatrixTranslate(1, 0, 0)) ||
        !R3D_SetAnimationBoneLocalMatrix(player, SHOULDER, authoredShoulder) ||
        !R3D_SetAnimationBoneLocalMatrix(player, ELBOW, MatrixTranslate(1, 0, 0)) ||
        !R3D_SetAnimationBoneLocalMatrix(player, WRIST, MatrixTranslate(1, 0, 0)) ||
        !R3D_RecomputeAnimationModelPose(player))
    {
        return false;
    }

    const Matrix originalWristLocal = player->localPose[WRIST];
    const Matrix originalWristModel = player->modelPose[WRIST];

    R3D_TwoBoneIKParams params = {
        .targetModel = target,
        .poleModel = pole,
        .weight = 1.0f
    };

    if (!R3D_ApplyAnimationTwoBoneIK(player, SHOULDER, ELBOW, WRIST, params))
    {
        return false;
    }

    // Solver only writes the two joint local matrices.
    *outDeferred =
        R3D_MatrixEquals(player->modelPose[WRIST], originalWristModel) &&
        R3D_MatrixEquals(player->localPose[WRIST], originalWristLocal);

    if (!R3D_RecomputeAnimationModelPoseFromBone(player, SHOULDER))
    {
        return false;
    }

    *outElbow = position(player->modelPose[ELBOW]);
    *outWrist = position(player->modelPose[WRIST]);

    return true;
}

static bool valid_chain(const R3D_AnimationPlayer* player)
{
    Vector3 shoulder = position(player->modelPose[SHOULDER]);
    Vector3 elbow = position(player->modelPose[ELBOW]);
    Vector3 wrist = position(player->modelPose[WRIST]);

    if (!finite_matrix(player->localPose[SHOULDER]) ||
        !finite_matrix(player->localPose[ELBOW]) ||
        !finite_matrix(player->modelPose[SHOULDER]) ||
        !finite_matrix(player->modelPose[ELBOW]) ||
        !finite_matrix(player->modelPose[WRIST]))
    {
        return false;
    }

    return near(Vector3Distance(shoulder, elbow), 1.0f, 0.003f) &&
           near(Vector3Distance(elbow, wrist), 1.0f, 0.003f);
}

int main(void)
{
    // Deliberately unordered hierarchy:
    //
    // Root (3)
    //   Shoulder (1)
    //     Elbow (2)
    //       Wrist (0)

    R3D_BoneInfo bones[4] = {
        {.name = "Wrist", .parent = ELBOW},
        {.name = "Shoulder", .parent = ROOT},
        {.name = "Elbow", .parent = SHOULDER},
        {.name = "Root", .parent = -1}
    };

    Matrix localPose[4] = {
        MatrixTranslate(1, 0, 0),
        MatrixIdentity(),
        MatrixTranslate(1, 0, 0),
        MatrixTranslate(1, 0, 0)
    };

    Matrix modelPose[4] = {
        MatrixIdentity(),
        MatrixIdentity(),
        MatrixIdentity(),
        MatrixIdentity()
    };

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = 4,
            .rootBind = MatrixTranslate(10, 0, 0)
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    const Matrix straightPose = MatrixIdentity();

    // Slightly bent authored pose, useful for a stable fallback
    // when the pole becomes exactly collinear with the target.
    const Matrix bentPose = MatrixRotateY(-0.03f);

    int failures = 0;

    // ========================================
    // TEST 1: MAXIMUM REACH BOUNDARY
    // ========================================
    //
    // Sweep across 100% reach in both directions.
    //
    // Each segment has length 1, total maximum reach = 2.
    // The pole stays on +Z, so the elbow should remain
    // on the same side of the target direction.
    //
    // This deliberately includes small distances from
    // the exact extension singularity.

    const float reachSteps[] = {
        1.90f,
        1.95f,
        1.99f,
        1.999f,
        1.9999f,
        2.0f,
        2.0001f,
        2.02f,
        2.0001f,
        2.0f,
        1.9999f,
        1.999f,
        1.99f,
        1.95f,
        1.90f
    };

    const int reachCount = sizeof(reachSteps) / sizeof(reachSteps[0]);

    bool reachSolved = true;
    bool reachPositions = true;
    bool reachLengths = true;
    bool reachContinuity = true;
    bool reachBendSide = true;
    bool reachDeferred = true;

    Vector3 previousElbow = {0};
    Vector3 firstElbow = {0};
    bool havePrevious = false;

    for (int i = 0; i < reachCount; i++)
    {
        float distance = reachSteps[i];

        Vector3 target = {11.0f + distance, 0.0f, 0.0f};
        Vector3 pole = {11.0f, 0.0f, 3.0f};

        Vector3 elbow = {0};
        Vector3 wrist = {0};
        bool deferred = false;

        bool solved = solve_sample(
            &player, straightPose, target, pole,
            &elbow, &wrist, &deferred
        );

        if (!solved)
        {
            printf("  Reach sample %d failed at %.6f\n", i, distance);
            reachSolved = false;
            break;
        }

        reachDeferred &= deferred;
        reachLengths &= valid_chain(&player);

        // Unreachable targets must clamp to full extension.
        float expectedReach = fminf(distance, 2.0f);
        Vector3 expectedWrist = {11.0f + expectedReach, 0, 0};

        reachPositions &= near_vec3(wrist, expectedWrist, 0.004f);

        // The elbow should not unexpectedly reverse its bend side.
        if (distance <= 1.999f)
        {
            reachBendSide &= elbow.z > 0.01f;
        }
        else
        {
            reachBendSide &= elbow.z >= -0.003f;
        }

        // Adjacent target samples should result in nearby elbows.
        // This threshold is larger than the expected geometric
        // movement but catches a large sudden reversal.
        if (havePrevious)
        {
            float movement = Vector3Distance(previousElbow, elbow);

            if (movement > 0.20f)
            {
                printf("  Elbow jump at %.6f: %.6f\n", distance, movement);
                reachContinuity = false;
            }
        }
        else
        {
            firstElbow = elbow;
        }

        previousElbow = elbow;
        havePrevious = true;
    }

    failures += check("Reach sweep completed", reachSolved);
    failures += check("Reach boundary clamping", reachSolved && reachPositions);
    failures += check("Segment lengths preserved throughout sweep", reachSolved && reachLengths);
    failures += check("Elbow remains on intended bend side", reachSolved && reachBendSide);
    failures += check("No large elbow jumps near full extension", reachSolved && reachContinuity);
    failures += check("IK modifications remain staged", reachSolved && reachDeferred);

    failures += check("Returning target restores initial elbow",
        reachSolved && near_vec3(previousElbow, firstElbow, 0.004f));

    // ========================================
    // TEST 2: NEARLY COLLINEAR POLE
    // ========================================
    //
    // An exactly collinear pole cannot uniquely define a
    // bend direction.
    //
    // With a slightly bent authored pose, our solver should
    // fall back to that existing bend direction.
    //
    // We approach zero from +Z, touch zero, then return.

    const float poleSteps[] = {
        3.0f,
        0.03f,
        0.00003f,
        0.00000003f,
        0.0f,
        0.00000003f,
        0.00003f,
        0.03f,
        3.0f
    };

    const int poleCount = sizeof(poleSteps) / sizeof(poleSteps[0]);

    bool poleSolved = true;
    bool poleReach = true;
    bool poleLengths = true;
    bool poleContinuity = true;
    bool poleBendSide = true;

    havePrevious = false;

    for (int i = 0; i < poleCount; i++)
    {
        Vector3 target = {12.5f, 0, 0};
        Vector3 pole = {11.0f, 0, poleSteps[i]};

        Vector3 elbow = {0};
        Vector3 wrist = {0};
        bool deferred = false;

        bool solved = solve_sample(
            &player, bentPose, target, pole,
            &elbow, &wrist, &deferred
        );

        if (!solved)
        {
            printf("  Pole sample %d failed at %.9f\n", i, poleSteps[i]);
            poleSolved = false;
            break;
        }

        poleReach &= near_vec3(wrist, target, 0.004f);
        poleLengths &= valid_chain(&player);
        poleBendSide &= elbow.z > 0.1f;

        if (havePrevious)
        {
            float movement = Vector3Distance(previousElbow, elbow);

            if (movement > 0.06f)
            {
                printf("  Pole continuity jump at %.9f: %.6f\n",
                       poleSteps[i], movement);

                poleContinuity = false;
            }
        }

        previousElbow = elbow;
        havePrevious = true;
    }

    failures += check("Nearly collinear pole sweep completed", poleSolved);
    failures += check("Near-parallel pole still reaches target", poleSolved && poleReach);
    failures += check("Near-parallel pole preserves segment lengths", poleSolved && poleLengths);
    failures += check("Authored bend side used at singular pole", poleSolved && poleBendSide);
    failures += check("No elbow pop approaching collinear pole", poleSolved && poleContinuity);

    // ========================================
    // TEST 3: INTENTIONAL POLE REVERSAL
    // ========================================
    //
    // Crossing to the opposite pole side SHOULD reverse
    // the bend. That is expected behaviour, not instability.

    Vector3 reverseElbow = {0};
    Vector3 reverseWrist = {0};
    bool reverseDeferred = false;

    bool reversed = solve_sample(
        &player,
        bentPose,
        (Vector3){12.5f, 0, 0},
        (Vector3){11.0f, 0, -3.0f},
        &reverseElbow,
        &reverseWrist,
        &reverseDeferred
    );

    failures += check("Opposite pole solves",
        reversed);

    failures += check("Opposite pole reverses elbow",
        reversed && reverseElbow.z < -0.1f);

    failures += check("Opposite pole retains wrist target",
        reversed && near_vec3(reverseWrist, (Vector3){12.5f, 0, 0}, 0.004f));

    // ========================================
    // TEST 4: TARGET DIRECTION JITTER
    // ========================================
    //
    // Simulate many frames with small changes in target
    // direction while the limb is nearly straight.
    //
    // No frame should produce invalid matrices, an unexpected
    // elbow flip, or a large discontinuity.

    bool jitterSolved = true;
    bool jitterReach = true;
    bool jitterLengths = true;
    bool jitterContinuity = true;

    havePrevious = false;

    for (int i = 0; i < 180; i++)
    {
        float t = (float)i * 0.21f;

        Vector3 target = {
            12.99f,
            0.003f * sinf(t),
            0.003f * cosf(t)
        };

        Vector3 pole = {11.0f, 0.0f, 3.0f};

        Vector3 elbow = {0};
        Vector3 wrist = {0};
        bool deferred = false;

        bool solved = solve_sample(
            &player, straightPose, target, pole,
            &elbow, &wrist, &deferred
        );

        if (!solved)
        {
            printf("  Target jitter failed at frame %d\n", i);
            jitterSolved = false;
            break;
        }

        jitterReach &= near_vec3(wrist, target, 0.004f);
        jitterLengths &= valid_chain(&player);

        if (havePrevious)
        {
            float movement = Vector3Distance(previousElbow, elbow);

            if (movement > 0.05f)
            {
                printf("  Target jitter elbow jump at frame %d: %.6f\n",
                       i, movement);

                jitterContinuity = false;
            }
        }

        previousElbow = elbow;
        havePrevious = true;
    }

    failures += check("180-frame target jitter completed", jitterSolved);
    failures += check("Nearly straight limb tracks jittering target", jitterSolved && jitterReach);
    failures += check("Segment lengths stable over 180 frames", jitterSolved && jitterLengths);
    failures += check("No unexpected elbow jumps across frames", jitterSolved && jitterContinuity);

    // ========================================
    // TEST 5: FULLY STRAIGHT + EXACT PARALLEL POLE
    // ========================================
    //
    // Both the authored limb and the pole are singular.
    // There is no unique bend plane, but the solver should
    // still select a finite, valid solution.

    Vector3 singularElbow = {0};
    Vector3 singularWrist = {0};
    bool singularDeferred = false;

    bool singularSolved = solve_sample(
        &player,
        straightPose,
        (Vector3){12.5f, 0, 0},
        (Vector3){11.0f, 0, 0},
        &singularElbow,
        &singularWrist,
        &singularDeferred
    );

    failures += check("Exactly collinear fallback accepted", singularSolved);

    failures += check("Singular fallback gives valid matrices and lengths",
        singularSolved && valid_chain(&player));

    failures += check("Singular fallback reaches target",
        singularSolved && near_vec3(singularWrist, (Vector3){12.5f, 0, 0}, 0.004f));

    printf("\nTwo-bone IK stability tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}