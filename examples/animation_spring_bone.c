#include <r3d/r3d_animation_spring.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

enum {
    SPRING = 0,
    CHILD = 1,
    ROOT = 2,
    OTHER = 3,
    BONE_COUNT = 4
};

static int check(const char* name, bool condition)
{
    printf("%s: %s\n", name, condition ? "PASS" : "FAIL");
    return condition ? 0 : 1;
}

static bool near(float a, float b)
{
    return fabsf(a - b) <= 0.003f;
}

static bool near_vec3(Vector3 a, Vector3 b)
{
    return Vector3Distance(a, b) <= 0.003f;
}

static Vector3 position(Matrix matrix)
{
    return (Vector3){
        matrix.m12,
        matrix.m13,
        matrix.m14
    };
}

static Vector3 direction(Matrix matrix, Vector3 axis)
{
    Vector3 origin = position(matrix);

    return Vector3Normalize(
        Vector3Subtract(
            Vector3Transform(axis, matrix),
            origin
        )
    );
}

static bool set_authored_spring(R3D_AnimationPlayer* player, float angle)
{
    Transform authored = {
        .translation = {0, 1, 0},
        .rotation = QuaternionFromAxisAngle(
            (Vector3){0, 1, 0},
            angle
        ),
        .scale = {1, 1, 1}
    };

    return R3D_SetAnimationBoneLocalTransform(
        player, SPRING, authored
    );
}

int main(void)
{
    // Unordered skeleton:
    //
    // Root (2)
    //   Spring (0)
    //     Child (1)
    //   Other (3)

    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "Spring", .parent = ROOT},
        {.name = "Child", .parent = SPRING},
        {.name = "Root", .parent = -1},
        {.name = "Other", .parent = ROOT}
    };

    Matrix localPose[BONE_COUNT] = {
        MatrixTranslate(0, 1, 0),
        MatrixTranslate(0, 0, 1),
        MatrixTranslate(1, 0, 0),
        MatrixTranslate(0, 2, 0)
    };

    Matrix modelPose[BONE_COUNT] = {
        MatrixIdentity(),
        MatrixIdentity(),
        MatrixIdentity(),
        MatrixIdentity()
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

    R3D_SpringBoneState state = {0};

    R3D_SpringBoneParams params = {
        .localAxis = {0, 0, 1},
        .length = 1.0f,
        .frequencyHz = 3.0f,
        .dampingRatio = 0.65f,
        .weight = 1.0f
    };

    int failures = 0;

    // ========================================
    // 1. INITIALIZATION
    // ========================================

    failures += check("Compute initial hierarchy",
        R3D_RecomputeAnimationModelPose(&player));

    failures += check("Initial spring position",
        near_vec3(position(modelPose[SPRING]), (Vector3){11, 1, 0}));

    failures += check("Initial child position",
        near_vec3(position(modelPose[CHILD]), (Vector3){11, 1, 1}));

    Matrix originalSpringLocal = localPose[SPRING];
    Matrix originalSpringModel = modelPose[SPRING];
    Matrix originalSiblingModel = modelPose[OTHER];

    failures += check("Initialize spring",
        R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, params, 1.0f / 60.0f
        ));

    failures += check("Spring state initialized",
        state.initialized);

    failures += check("Initial tip matches authored pose",
        near_vec3(state.tipModel, (Vector3){11, 1, 1}));

    failures += check("Initialization preserves authored pose",
        R3D_MatrixEquals(localPose[SPRING], originalSpringLocal) &&
        R3D_MatrixEquals(modelPose[SPRING], originalSpringModel));

    // ========================================
    // 2. ANIMATED BONE CHANGES DIRECTION
    // ========================================
    //
    // Rotate the authored bone by 90 degrees.
    // Its virtual tip moves from +Z toward +X.
    //
    // The spring should initially lag behind.

    failures += check("Set new authored direction",
        set_authored_spring(&player, PI / 2.0f));

    failures += check("Compute new authored model pose",
        R3D_RecomputeAnimationModelPose(&player));

    Matrix authoredModel = modelPose[SPRING];
    Matrix childBeforeSpring = modelPose[CHILD];

    Vector3 authoredForward = direction(
        authoredModel, (Vector3){0, 0, 1}
    );

    failures += check("Authored direction points along X",
        near_vec3(authoredForward, (Vector3){1, 0, 0}));

    failures += check("Apply spring after authored animation",
        R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, params, 1.0f / 60.0f
        ));

    failures += check("Spring does not recompute model pose",
        R3D_MatrixEquals(modelPose[SPRING], authoredModel) &&
        R3D_MatrixEquals(modelPose[CHILD], childBeforeSpring));

    failures += check("Spring advances persistent tip",
        Vector3Distance(state.tipModel, (Vector3){11, 1, 1}) > 0.001f);

    failures += check("Recompute spring subtree",
        R3D_RecomputeAnimationModelPoseFromBone(&player, SPRING));

    Vector3 springForward = direction(
        modelPose[SPRING], (Vector3){0, 0, 1}
    );

    failures += check("Spring visually lags authored movement",
        Vector3DotProduct(springForward, authoredForward) < 0.98f);

    failures += check("Spring still points toward original direction",
        springForward.z > 0.1f);

    failures += check("Spring preserves bone origin",
        near_vec3(position(modelPose[SPRING]), (Vector3){11, 1, 0}));

    failures += check("Child inherits spring rotation",
        near_vec3(
            position(modelPose[CHILD]),
            Vector3Add(position(modelPose[SPRING]), springForward)
        ));

    failures += check("Spring preserves segment length",
        near(Vector3Distance(
            position(modelPose[SPRING]),
            position(modelPose[CHILD])
        ), 1.0f));

    failures += check("Unrelated sibling remains unchanged",
        R3D_MatrixEquals(modelPose[OTHER], originalSiblingModel));

    // ========================================
    // 3. SETTLING OVER MANY FRAMES
    // ========================================
    //
    // Animation is sampled afresh every frame.
    // Only state.tipModel and state.velocityModel persist.

    bool settled = true;

    for (int frame = 0; frame < 180; frame++)
    {
        if (!set_authored_spring(&player, PI / 2.0f) ||
            !R3D_RecomputeAnimationModelPose(&player) ||
            !R3D_ApplyAnimationSpringBone(
                &player, SPRING, &state, params, 1.0f / 60.0f
            ) ||
            !R3D_RecomputeAnimationModelPoseFromBone(&player, SPRING))
        {
            settled = false;
            break;
        }
    }

    failures += check("180 spring simulation frames completed",
        settled);

    Vector3 settledForward = direction(
        modelPose[SPRING], (Vector3){0, 0, 1}
    );

    failures += check("Spring settles toward animated direction",
        settled && Vector3DotProduct(
            settledForward,
            (Vector3){1, 0, 0}
        ) > 0.999f);

    failures += check("Segment length remains unchanged after settling",
        settled && near(Vector3Distance(
            position(modelPose[SPRING]),
            position(modelPose[CHILD])
        ), 1.0f));

    // ========================================
    // 4. ZERO INFLUENCE
    // ========================================

    R3D_ResetAnimationSpringBone(&state);

    failures += check("Reset clears initialization",
        !state.initialized);

    failures += check("Restore authored pose",
        set_authored_spring(&player, PI / 2.0f) &&
        R3D_RecomputeAnimationModelPose(&player));

    params.weight = 0.0f;

    failures += check("Initialize zero-weight spring",
        R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, params, 1.0f / 60.0f
        ));

    Vector3 previousTip = state.tipModel;

    failures += check("Change authored pose again",
        set_authored_spring(&player, 0) &&
        R3D_RecomputeAnimationModelPose(&player));

    Matrix zeroWeightAuthored = localPose[SPRING];

    failures += check("Zero-weight spring succeeds",
        R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, params, 1.0f / 60.0f
        ));

    failures += check("Zero weight preserves exact local pose",
        R3D_MatrixEquals(localPose[SPRING], zeroWeightAuthored));

    failures += check("Zero weight still updates spring state",
        Vector3Distance(previousTip, state.tipModel) > 0.001f);

    // ========================================
    // 5. PAUSED SIMULATION
    // ========================================

    Vector3 pausedTip = state.tipModel;
    Vector3 pausedVelocity = state.velocityModel;

    failures += check("Zero delta time accepted",
        R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, params, 0.0f
        ));

    failures += check("Zero delta time preserves simulation state",
        near_vec3(state.tipModel, pausedTip) &&
        near_vec3(state.velocityModel, pausedVelocity));

    // ========================================
    // 6. INVALID INPUT
    // ========================================

    Matrix beforeInvalid = localPose[SPRING];
    R3D_SpringBoneState beforeInvalidState = state;

    R3D_SpringBoneParams invalid = params;
    invalid.localAxis = Vector3Zero();

    failures += check("Zero local axis rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, invalid, 1.0f / 60.0f
        ));

    invalid = params;
    invalid.length = 0;

    failures += check("Zero virtual length rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, invalid, 1.0f / 60.0f
        ));

    invalid = params;
    invalid.frequencyHz = 0;

    failures += check("Zero spring frequency rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, invalid, 1.0f / 60.0f
        ));

    invalid = params;
    invalid.dampingRatio = 1.5f;

    failures += check("Invalid damping ratio rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, invalid, 1.0f / 60.0f
        ));

    failures += check("Negative delta time rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, params, -1.0f
        ));

    failures += check("Excessively large delta time rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, params, 2.0f
        ));

    failures += check("Invalid bone index rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, 99, &state, params, 1.0f / 60.0f
        ));

    failures += check("NULL player rejected",
        !R3D_ApplyAnimationSpringBone(
            NULL, SPRING, &state, params, 1.0f / 60.0f
        ));

    failures += check("NULL state rejected",
        !R3D_ApplyAnimationSpringBone(
            &player, SPRING, NULL, params, 1.0f / 60.0f
        ));

    failures += check("Invalid calls preserve local pose",
        R3D_MatrixEquals(localPose[SPRING], beforeInvalid));

    failures += check("Invalid calls preserve simulation state",
        near_vec3(state.tipModel, beforeInvalidState.tipModel) &&
        near_vec3(state.velocityModel, beforeInvalidState.velocityModel) &&
        state.initialized == beforeInvalidState.initialized);

    // ========================================
    // 7. TELEPORT / RESET BEHAVIOUR
    // ========================================

    R3D_ResetAnimationSpringBone(&state);

    failures += check("Reset clears velocity",
        near_vec3(state.velocityModel, Vector3Zero()));

    failures += check("Reset clears tip",
        near_vec3(state.tipModel, Vector3Zero()));

    failures += check("Reset returns to uninitialized state",
        !state.initialized);

    failures += check("Spring reinitializes from current pose",
        R3D_ApplyAnimationSpringBone(
            &player, SPRING, &state, params, 1.0f / 60.0f
        ));

    failures += check("Reinitialized tip follows authored pose",
        near_vec3(
            state.tipModel,
            Vector3Transform((Vector3){0, 0, 1}, modelPose[SPRING])
        ));

    printf("\nSpring bone tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}