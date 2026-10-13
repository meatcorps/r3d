#include <r3d/r3d_animation_spring_chain.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

#define SPRING_COUNT 4
#define BONE_COUNT 6
#define SAMPLE_COUNT 8

enum {
    BONE_B = 0,
    BONE_ROOT = 1,
    BONE_D = 2,
    BONE_C = 3,
    BONE_A = 4,
    BONE_TIP = 5
};

// Unordered skeleton, ordered spring path.
static const int path[SPRING_COUNT] = {
    BONE_A, BONE_B, BONE_C, BONE_D
};

typedef struct SimulationResult {
    Vector3 middle[SAMPLE_COUNT];
    Vector3 tip[SAMPLE_COUNT];

    float maxLengthError;
    float finalVelocity;
    bool success;
} SimulationResult;

static int check(const char* name, bool passed)
{
    printf("%s: %s\n", name, passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}

static Vector3 position(Matrix matrix)
{
    return (Vector3){
        matrix.m12,
        matrix.m13,
        matrix.m14
    };
}

static bool finite_vector(Vector3 value)
{
    return isfinite(value.x) &&
           isfinite(value.y) &&
           isfinite(value.z);
}

static R3D_SpringBoneParams make_params(int segment)
{
    const float t =
        (float)segment / (float)(SPRING_COUNT - 1);

    return (R3D_SpringBoneParams){
        .localAxis = {0, 1, 0},
        .length = 0.35f,

        .frequencyHz = 3.0f - 0.8f * t,
        .dampingRatio = 0.70f - 0.15f * t,
        .weight = 1.0f,

        .limits = {
            .enabled = true,
            .localReferenceAxis = {1, 0, 0},

            .minHorizontalRadians = -40.0f * DEG2RAD,
            .maxHorizontalRadians =  40.0f * DEG2RAD,

            .minVerticalRadians = -30.0f * DEG2RAD,
            .maxVerticalRadians =  30.0f * DEG2RAD
        },

        .forces = {
            .gravityWorld = {4.0f, -10.0f, 2.0f},
            .drag = 1.4f,
            .worldInertia = 0.85f
        }
    };
}

/**
 * Same continuous authored motion for every update rate.
 *
 * The character moves for two seconds, then stops.
 * This creates a useful settling test afterward.
 */
static Matrix authored_root(float time, bool moving)
{
    float t = moving ? fminf(time, 2.0f) : 0.0f;

    float angle = moving
        ? 0.22f * sinf(2.3f * t)
        : 0.0f;

    Matrix result = MatrixRotateZ(angle);

    result.m12 = moving
        ? 0.22f * sinf(1.9f * t)
        : 0.0f;

    result.m13 = 0.0f;
    result.m14 = 0.0f;

    return result;
}

/**
 * Model-to-world transform supplied by the external engine.
 *
 * This is intentionally separate from the authored root.
 */
static Matrix character_world(float time, bool moving)
{
    float t = moving ? fminf(time, 2.0f) : 0.0f;

    float angle = 0.35f;

    if (moving)
    {
        angle += 0.20f * sinf(2.1f * t);
    }

    Matrix world = MatrixRotateZ(angle);

    world.m12 = moving
        ? 0.45f * sinf(2.0f * t)
        : 0.0f;

    world.m13 = 0.0f;

    world.m14 = moving
        ? 0.28f * (cosf(1.5f * t) - 1.0f)
        : 0.0f;

    return world;
}

static void restore_authored_pose(
    Matrix* localPose,
    float time,
    bool moving)
{
    for (int i = 0; i < BONE_COUNT; i++)
    {
        localPose[i] = MatrixTranslate(0, 0.35f, 0);
    }

    localPose[BONE_ROOT] = authored_root(time, moving);
}

static float segment_length_error(const Matrix* modelPose)
{
    float maxError = 0.0f;

    for (int i = 0; i < SPRING_COUNT; i++)
    {
        int next = i == SPRING_COUNT - 1
            ? BONE_TIP
            : path[i + 1];

        float length = Vector3Distance(
            position(modelPose[path[i]]),
            position(modelPose[next])
        );

        maxError = fmaxf(
            maxError,
            fabsf(length - 0.35f)
        );
    }

    return maxError;
}

/**
 * Simulates four seconds at an externally supplied fixed rate.
 *
 * No accumulator, raylib window, GPU, or engine update loop.
 */
static SimulationResult simulate(
    int updateRate,
    bool moving)
{
    SimulationResult result = {0};

    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "B",    .parent = BONE_A},
        {.name = "Root", .parent = -1},
        {.name = "D",    .parent = BONE_C},
        {.name = "C",    .parent = BONE_B},
        {.name = "A",    .parent = BONE_ROOT},
        {.name = "Tip",  .parent = BONE_D}
    };

    Matrix localPose[BONE_COUNT];
    Matrix modelPose[BONE_COUNT];

    for (int i = 0; i < BONE_COUNT; i++)
    {
        modelPose[i] = MatrixIdentity();
    }

    restore_authored_pose(localPose, 0.0f, moving);

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = BONE_COUNT,
            .rootBind = MatrixIdentity()
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    // Deliberately scrambled descriptors.
    R3D_SpringChainBoneDesc descriptors[SPRING_COUNT] = {
        {.boneIndex = BONE_C, .params = make_params(2)},
        {.boneIndex = BONE_A, .params = make_params(0)},
        {.boneIndex = BONE_D, .params = make_params(3)},
        {.boneIndex = BONE_B, .params = make_params(1)}
    };

    R3D_SpringChain chain = R3D_LoadSpringChain(
        &player,
        descriptors,
        SPRING_COUNT
    );

    if (!R3D_IsSpringChainValid(&chain))
    {
        return result;
    }

    const float dt = 1.0f / (float)updateRate;
    const int totalSteps = 4 * updateRate;
    const int sampleInterval = updateRate / 2;

    // Initialize from exactly the same t=0 pose
    // for all three update rates.
    Matrix initialWorld = character_world(0.0f, moving);

    if (!R3D_RecomputeAnimationModelPose(&player) ||
        !R3D_UpdateSpringChainWorld(
            &chain,
            &player,
            initialWorld,
            0.0f))
    {
        R3D_UnloadSpringChain(chain);
        return result;
    }

    bool valid = true;
    int sampleIndex = 0;

    for (int step = 1; step <= totalSteps; step++)
    {
        // Compute the current simulation time from the step
        // index, avoiding accumulated floating-point drift.
        float time = (float)step / (float)updateRate;

        // ========================================
        // AUTHORED ANIMATION
        // ========================================

        restore_authored_pose(localPose, time, moving);

        if (!R3D_RecomputeAnimationModelPose(&player))
        {
            valid = false;
            break;
        }

        // ========================================
        // EXTERNAL WORLD TRANSFORM
        // ========================================

        Matrix world = character_world(time, moving);

        // ========================================
        // SPRING CHAIN UPDATE
        // ========================================

        if (!R3D_UpdateSpringChainWorld(
            &chain,
            &player,
            world,
            dt))
        {
            valid = false;
            break;
        }

        // ========================================
        // VALIDATE EACH STEP
        // ========================================

        result.maxLengthError = fmaxf(
            result.maxLengthError,
            segment_length_error(modelPose)
        );

        for (int i = 0; i < chain.boneCount; i++)
        {
            const R3D_SpringBoneState* state =
                &chain.bones[i].state;

            if (!state->initialized ||
                !finite_vector(state->tipModel) ||
                !finite_vector(state->velocityModel))
            {
                valid = false;
                break;
            }
        }

        if (!valid) break;

        // ========================================
        // CAPTURE MATCHING TIMES
        // ========================================

        // Samples at:
        // 0.5, 1.0, 1.5, ..., 4.0 seconds.
        if (step % sampleInterval == 0)
        {
            if (sampleIndex >= SAMPLE_COUNT)
            {
                valid = false;
                break;
            }

            result.middle[sampleIndex] = Vector3Transform(
                position(modelPose[BONE_C]),
                world
            );

            result.tip[sampleIndex] = Vector3Transform(
                position(modelPose[BONE_TIP]),
                world
            );

            if (!finite_vector(result.middle[sampleIndex]) ||
                !finite_vector(result.tip[sampleIndex]))
            {
                valid = false;
                break;
            }

            sampleIndex++;
        }
    }

    if (valid && sampleIndex == SAMPLE_COUNT)
    {
        const R3D_SpringBoneState* tipSpring =
            &chain.bones[SPRING_COUNT - 1].state;

        result.finalVelocity =
            Vector3Length(tipSpring->velocityModel);

        result.success = isfinite(result.finalVelocity);
    }

    R3D_UnloadSpringChain(chain);

    return result;
}

static float max_pose_difference(
    const SimulationResult* a,
    const SimulationResult* b)
{
    float maximum = 0.0f;

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        maximum = fmaxf(
            maximum,
            Vector3Distance(a->middle[i], b->middle[i])
        );

        maximum = fmaxf(
            maximum,
            Vector3Distance(a->tip[i], b->tip[i])
        );
    }

    return maximum;
}

static void print_comparison(
    const char* name,
    const SimulationResult* result30,
    const SimulationResult* result60,
    const SimulationResult* result120)
{
    printf("\n=== %s ===\n", name);

    printf(
        "30 Hz  max length error: %.7f | final velocity: %.6f\n",
        result30->maxLengthError,
        result30->finalVelocity
    );

    printf(
        "60 Hz  max length error: %.7f | final velocity: %.6f\n",
        result60->maxLengthError,
        result60->finalVelocity
    );

    printf(
        "120 Hz max length error: %.7f | final velocity: %.6f\n",
        result120->maxLengthError,
        result120->finalVelocity
    );

    printf(
        "Maximum pose difference 30 vs 60:  %.6f\n",
        max_pose_difference(result30, result60)
    );

    printf(
        "Maximum pose difference 60 vs 120: %.6f\n",
        max_pose_difference(result60, result120)
    );

    printf(
        "Maximum pose difference 30 vs 120: %.6f\n",
        max_pose_difference(result30, result120)
    );

    printf("\nTip positions at matching timestamps:\n");

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        printf(
            "t=%4.1fs | 30/60: %.6f | 60/120: %.6f\n",
            (float)(i + 1) * 0.5f,
            Vector3Distance(
                result30->tip[i], result60->tip[i]
            ),
            Vector3Distance(
                result60->tip[i], result120->tip[i]
            )
        );
    }
}

int main(void)
{
    int failures = 0;

    // ========================================
    // TEST 1: STATIONARY CHARACTER
    // ========================================

    SimulationResult stationary30 = simulate(30, false);
    SimulationResult stationary60 = simulate(60, false);
    SimulationResult stationary120 = simulate(120, false);

    failures += check(
        "Stationary simulations completed",
        stationary30.success &&
        stationary60.success &&
        stationary120.success
    );

    if (stationary30.success &&
        stationary60.success &&
        stationary120.success)
    {
        print_comparison(
            "STATIONARY GRAVITY / DRAG",
            &stationary30,
            &stationary60,
            &stationary120
        );

        float difference = max_pose_difference(
            &stationary30, &stationary120
        );

        failures += check(
            "Stationary pose remains consistent across rates",
            difference < 0.02f
        );

        failures += check(
            "Stationary chain lengths remain stable",
            stationary30.maxLengthError < 0.002f &&
            stationary60.maxLengthError < 0.002f &&
            stationary120.maxLengthError < 0.002f
        );
    }

    // ========================================
    // TEST 2: MOVING CHARACTER
    // ========================================

    SimulationResult moving30 = simulate(30, true);
    SimulationResult moving60 = simulate(60, true);
    SimulationResult moving120 = simulate(120, true);

    failures += check(
        "Moving simulations completed",
        moving30.success &&
        moving60.success &&
        moving120.success
    );

    if (moving30.success &&
        moving60.success &&
        moving120.success)
    {
        print_comparison(
            "MOVING CHARACTER / WORLD INERTIA / LIMITS",
            &moving30,
            &moving60,
            &moving120
        );

        float difference30to60 = max_pose_difference(
            &moving30, &moving60
        );

        float difference60to120 = max_pose_difference(
            &moving60, &moving120
        );

        failures += check(
            "Moving chain lengths remain stable",
            moving30.maxLengthError < 0.002f &&
            moving60.maxLengthError < 0.002f &&
            moving120.maxLengthError < 0.002f
        );

        // A practical initial tolerance for this 1.4-unit
        // chain under animated targets and world motion.
        // The printed measurements are more informative
        // than this single broad threshold.
        failures += check(
            "Moving poses stay within initial tolerance",
            max_pose_difference(&moving30, &moving120) < 0.25f
        );

        // A finer timestep should generally converge
        // toward a stable solution, although nonlinear
        // constraints can prevent strict monotonicity.
        failures += check(
            "Higher update rates show reasonable convergence",
            difference60to120 <= difference30to60 + 0.08f
        );
    }

    printf(
        "\nSpring timestep validation: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED"
    );

    return failures == 0 ? 0 : 1;
}