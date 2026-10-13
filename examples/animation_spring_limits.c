#include <r3d/r3d_animation_spring.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

enum {
    SPRING = 0,
    ROOT = 1,
    BONE_COUNT = 2
};

static int check(const char* name, bool passed)
{
    printf("%s: %s\n", name, passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 0.005f;
}

static Vector3 position(Matrix matrix)
{
    return (Vector3){
        matrix.m12,
        matrix.m13,
        matrix.m14
    };
}

static Vector3 spring_forward(Matrix matrix)
{
    Vector3 origin = position(matrix);

    return Vector3Normalize(
        Vector3Subtract(
            Vector3Transform((Vector3){0, 1, 0}, matrix),
            origin
        )
    );
}

static float horizontal_angle(Matrix matrix)
{
    Vector3 forward = spring_forward(matrix);
    return atan2f(forward.x, forward.y);
}

static float vertical_angle(Matrix matrix)
{
    Vector3 forward = spring_forward(matrix);

    return atan2f(
        forward.z,
        sqrtf(forward.x * forward.x +
              forward.y * forward.y)
    );
}

static bool solve(
    R3D_AnimationPlayer* player,
    R3D_SpringBoneState* state,
    R3D_SpringBoneParams params,
    Vector3 simulatedTip,
    Vector3 simulatedVelocity)
{
    // Fresh authored pose every time.
    if (!R3D_SetAnimationBoneLocalMatrix(
        player,
        SPRING,
        MatrixTranslate(0, 1, 0)))
    {
        return false;
    }

    if (!R3D_RecomputeAnimationModelPose(player))
    {
        return false;
    }

    // Explicit spring history allows deterministic
    // testing without running hundreds of frames.
    *state = (R3D_SpringBoneState){
        .initialized = true,
        .tipModel = simulatedTip,
        .velocityModel = simulatedVelocity
    };

    if (!R3D_ApplyAnimationSpringBone(
        player, SPRING, state, params, 0.0f))
    {
        return false;
    }

    return R3D_RecomputeAnimationModelPoseFromBone(
        player, SPRING
    );
}

int main(void)
{
    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "Spring", .parent = ROOT},
        {.name = "Root", .parent = -1}
    };

    Matrix localPose[BONE_COUNT] = {
        MatrixTranslate(0, 1, 0),
        MatrixIdentity()
    };

    Matrix modelPose[BONE_COUNT] = {
        MatrixIdentity(),
        MatrixIdentity()
    };

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

    R3D_SpringBoneState state = {0};

    R3D_SpringBoneParams params = {
        .localAxis = {0, 1, 0},
        .length = 1.0f,
        .frequencyHz = 2.0f,
        .dampingRatio = 0.5f,
        .weight = 1.0f,

        .limits = {
            .enabled = true,
            .localReferenceAxis = {1, 0, 0},

            .minHorizontalRadians = -40.0f * DEG2RAD,
            .maxHorizontalRadians =  15.0f * DEG2RAD,

            .minVerticalRadians = -10.0f * DEG2RAD,
            .maxVerticalRadians =  25.0f * DEG2RAD
        }
    };

    int failures = 0;

    failures += check(
        "Initial hierarchy",
        R3D_RecomputeAnimationModelPose(&player)
    );

    // ========================================
    // HORIZONTAL POSITIVE LIMIT
    // ========================================

    failures += check(
        "Positive horizontal limit accepted",
        solve(
            &player, &state, params,
            (Vector3){1, 1, 0},
            Vector3Zero()
        )
    );

    failures += check(
        "Horizontal maximum is +15 degrees",
        near(horizontal_angle(modelPose[SPRING]),
             15.0f * DEG2RAD)
    );

    failures += check(
        "Horizontal-only displacement has zero vertical angle",
        near(vertical_angle(modelPose[SPRING]), 0.0f)
    );

    // ========================================
    // HORIZONTAL NEGATIVE LIMIT
    // ========================================

    failures += check(
        "Negative horizontal limit accepted",
        solve(
            &player, &state, params,
            (Vector3){-1, 1, 0},
            Vector3Zero()
        )
    );

    failures += check(
        "Horizontal minimum is -40 degrees",
        near(horizontal_angle(modelPose[SPRING]),
             -40.0f * DEG2RAD)
    );

    // ========================================
    // VERTICAL POSITIVE LIMIT
    // ========================================

    failures += check(
        "Positive vertical limit accepted",
        solve(
            &player, &state, params,
            (Vector3){0, 1, 1},
            Vector3Zero()
        )
    );

    failures += check(
        "Vertical maximum is +25 degrees",
        near(vertical_angle(modelPose[SPRING]),
             25.0f * DEG2RAD)
    );

    failures += check(
        "Vertical bending does not introduce horizontal angle",
        near(horizontal_angle(modelPose[SPRING]), 0.0f)
    );

    // ========================================
    // VERTICAL NEGATIVE LIMIT
    // ========================================

    failures += check(
        "Negative vertical limit accepted",
        solve(
            &player, &state, params,
            (Vector3){0, 1, -1},
            Vector3Zero()
        )
    );

    failures += check(
        "Vertical minimum is -10 degrees",
        near(vertical_angle(modelPose[SPRING]),
             -10.0f * DEG2RAD)
    );

    // ========================================
    // BOTH AXES SIMULTANEOUSLY
    // ========================================

    failures += check(
        "Combined positive angles accepted",
        solve(
            &player, &state, params,
            (Vector3){1, 1, 1},
            Vector3Zero()
        )
    );

    failures += check(
        "Combined horizontal limit",
        near(horizontal_angle(modelPose[SPRING]),
             15.0f * DEG2RAD)
    );

    failures += check(
        "Combined vertical limit",
        near(vertical_angle(modelPose[SPRING]),
             25.0f * DEG2RAD)
    );

    // ========================================
    // LOCK BOTH AXES
    // ========================================

    R3D_SpringBoneParams locked = params;

    locked.limits.minHorizontalRadians = 0;
    locked.limits.maxHorizontalRadians = 0;
    locked.limits.minVerticalRadians = 0;
    locked.limits.maxVerticalRadians = 0;

    failures += check(
        "Fully locked spring accepted",
        solve(
            &player, &state, locked,
            (Vector3){1, 1, 1},
            Vector3Zero()
        )
    );

    failures += check(
        "Locked spring preserves authored forward direction",
        Vector3Distance(
            spring_forward(modelPose[SPRING]),
            (Vector3){0, 1, 0}
        ) < 0.005f
    );

    // ========================================
    // SINGLE-AXIS HINGE
    // ========================================

    R3D_SpringBoneParams hinge = params;

    hinge.limits.minHorizontalRadians = 0;
    hinge.limits.maxHorizontalRadians = 0;

    failures += check(
        "Single-axis hinge accepted",
        solve(
            &player, &state, hinge,
            (Vector3){1, 1, 1},
            Vector3Zero()
        )
    );

    failures += check(
        "Hinge prevents horizontal bending",
        near(horizontal_angle(modelPose[SPRING]), 0)
    );

    failures += check(
        "Hinge still allows vertical bending",
        near(vertical_angle(modelPose[SPRING]),
             25.0f * DEG2RAD)
    );

    // ========================================
    // FIXED NONZERO ANGLE
    // ========================================

    R3D_SpringBoneParams fixed = params;

    fixed.limits.minHorizontalRadians = 10.0f * DEG2RAD;
    fixed.limits.maxHorizontalRadians = 10.0f * DEG2RAD;
    fixed.limits.minVerticalRadians = 0;
    fixed.limits.maxVerticalRadians = 0;

    failures += check(
        "Fixed nonzero angle accepted",
        solve(
            &player, &state, fixed,
            (Vector3){1, 1, 1},
            Vector3Zero()
        )
    );

    failures += check(
        "Fixed horizontal angle is 10 degrees",
        near(horizontal_angle(modelPose[SPRING]),
             10.0f * DEG2RAD)
    );

    failures += check(
        "Fixed vertical angle is zero",
        near(vertical_angle(modelPose[SPRING]), 0)
    );

    // ========================================
    // VELOCITY CLAMPING
    // ========================================

    failures += check(
        "Outward velocity test accepted",
        solve(
            &player, &state, params,
            (Vector3){1, 1, 0},
            (Vector3){0, -1, 0}
        )
    );

    Vector3 forward = spring_forward(modelPose[SPRING]);

    Vector3 yawTangent = {
        cosf(15.0f * DEG2RAD),
        -sinf(15.0f * DEG2RAD),
        0
    };

    failures += check(
        "Outward horizontal velocity removed",
        fabsf(Vector3DotProduct(
            state.velocityModel, yawTangent
        )) < 0.005f
    );

    failures += check(
        "Simulated tip follows constrained direction",
        Vector3Distance(
            Vector3Normalize(
                Vector3Subtract(
                    state.tipModel,
                    position(modelPose[SPRING])
                )
            ),
            forward
        ) < 0.005f
    );

    // ========================================
    // INVALID CONSTRAINTS
    // ========================================

    Matrix preservedLocal = localPose[SPRING];
    R3D_SpringBoneState preservedState = state;

    R3D_SpringBoneParams invalid = params;

    invalid.limits.minHorizontalRadians =
        30.0f * DEG2RAD;
    invalid.limits.maxHorizontalRadians =
        -30.0f * DEG2RAD;

    failures += check(
        "Inverted horizontal limits rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, invalid, 0
        )
    );

    invalid = params;
    invalid.limits.maxVerticalRadians = PI;

    failures += check(
        "Vertical angle above 90 degrees rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, invalid, 0
        )
    );

    invalid = params;
    invalid.limits.localReferenceAxis =
        (Vector3){0, 1, 0};

    failures += check(
        "Parallel reference axis rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, invalid, 0
        )
    );

    failures += check(
        "Invalid constraints preserve local pose",
        R3D_MatrixEquals(
            localPose[SPRING],
            preservedLocal
        )
    );

    failures += check(
        "Invalid constraints preserve simulation state",
        Vector3Distance(
            state.tipModel,
            preservedState.tipModel
        ) < 0.00001f &&
        Vector3Distance(
            state.velocityModel,
            preservedState.velocityModel
        ) < 0.00001f &&
        state.initialized == preservedState.initialized
    );

    // ========================================
    // DISABLED CONSTRAINTS
    // ========================================

    R3D_SpringBoneParams unrestricted = params;
    unrestricted.limits.enabled = false;

    failures += check(
        "Unrestricted spring accepted",
        solve(
            &player, &state, unrestricted,
            (Vector3){1, 1, 0},
            Vector3Zero()
        )
    );

    failures += check(
        "Disabled constraints permit 90-degree bend",
        near(horizontal_angle(modelPose[SPRING]),
             PI * 0.5f)
    );

    printf(
        "\nSpring bone angle-limit tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED"
    );

    return failures == 0 ? 0 : 1;
}