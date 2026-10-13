#include <r3d/r3d_animation_spring_chain.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

enum {
    BASE = 0,
    ROOT = 1,
    SECOND = 2,
    TIP = 3,
    BONE_COUNT = 4
};

static int check(const char* name, bool passed)
{
    printf("%s: %s\n", name, passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}

static Vector3 pos(Matrix m)
{
    return (Vector3){m.m12, m.m13, m.m14};
}

static Vector3 forward(Matrix m)
{
    return Vector3Normalize(Vector3Subtract(
        Vector3Transform((Vector3){0, 1, 0}, m),
        pos(m)
    ));
}

static bool near_vec3(Vector3 a, Vector3 b)
{
    return Vector3Distance(a, b) < 0.001f;
}

static R3D_SpringBoneParams make_params(void)
{
    return (R3D_SpringBoneParams){
        .localAxis = {0, 1, 0},
        .length = 1.0f,
        .frequencyHz = 1.8f,
        .dampingRatio = 0.65f,
        .weight = 1.0f
    };
}

static void restore_pose(
    R3D_AnimationPlayer* player,
    const Matrix* authored)
{
    for (int i = 0; i < BONE_COUNT; i++)
    {
        player->localPose[i] = authored[i];
    }
}

static bool step_bone(
    R3D_AnimationPlayer* player,
    const Matrix* authored,
    R3D_SpringBoneState* state,
    R3D_SpringBoneParams params,
    Matrix world,
    float dt)
{
    restore_pose(player, authored);

    return R3D_RecomputeAnimationModelPose(player) &&
           R3D_ApplyAnimationSpringBoneWorld(
               player, BASE, state, params, world, dt
           ) &&
           R3D_RecomputeAnimationModelPoseFromBone(
               player, BASE
           );
}

static bool step_chain(
    R3D_AnimationPlayer* player,
    const Matrix* authored,
    R3D_SpringChain* chain,
    Matrix world,
    float dt)
{
    restore_pose(player, authored);

    return R3D_RecomputeAnimationModelPose(player) &&
           R3D_UpdateSpringChainWorld(
               chain, player, world, dt
           );
}

int main(void)
{
    // Root -> Base -> Second -> Tip.
    // Indices are deliberately unordered.

    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "Base", .parent = ROOT},
        {.name = "Root", .parent = -1},
        {.name = "Second", .parent = BASE},
        {.name = "Tip", .parent = SECOND}
    };

    Matrix authored[BONE_COUNT] = {
        MatrixTranslate(0, 1, 0),
        MatrixIdentity(),
        MatrixTranslate(0, 1, 0),
        MatrixTranslate(0, 1, 0)
    };

    Matrix localPose[BONE_COUNT];
    Matrix modelPose[BONE_COUNT];

    for (int i = 0; i < BONE_COUNT; i++)
    {
        localPose[i] = authored[i];
        modelPose[i] = MatrixIdentity();
    }

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
    R3D_SpringBoneParams params = make_params();

    const Matrix identity = MatrixIdentity();
    const float dt = 1.0f / 60.0f;

    int failures = 0;

    failures += check(
        "Initial hierarchy",
        R3D_RecomputeAnimationModelPose(&player)
    );

    // ========================================
    // 1. LEGACY ZERO-FORCE BEHAVIOUR
    // ========================================

    failures += check(
        "Legacy spring initialization",
        R3D_ApplyAnimationSpringBone(
            &player, BASE, &state, params, dt
        )
    );

    failures += check(
        "Identity world history initialized",
        state.initialized &&
        state.worldInitialized &&
        R3D_MatrixEquals(state.previousWorld, identity)
    );

    failures += check(
        "Zero-force spring preserves authored direction",
        near_vec3(
            forward(modelPose[BASE]),
            (Vector3){0, 1, 0}
        )
    );

    // ========================================
    // 2. GRAVITY
    // ========================================

    R3D_ResetAnimationSpringBone(&state);

    params = make_params();
    params.forces.gravityWorld = (Vector3){12, 0, 0};

    bool gravitySteps = true;

    for (int i = 0; i < 240; i++)
    {
        if (!step_bone(
            &player, authored, &state,
            params, identity, dt))
        {
            gravitySteps = false;
            break;
        }
    }

    failures += check(
        "Gravity simulation completed",
        gravitySteps
    );

    Vector3 gravityForward = forward(modelPose[BASE]);

    printf(
        "Gravity forward X: %.6f (expected approximately +0.093)\n",
        gravityForward.x
    );

    failures += check(
        "Gravity pulls spring toward positive X",
        gravitySteps &&
        gravityForward.x > 0.07f &&
        gravityForward.x < 0.12f
    );

    failures += check(
        "Gravity keeps virtual segment normalized",
        gravitySteps &&
        fabsf(Vector3Length(
            Vector3Subtract(state.tipModel, pos(modelPose[BASE]))
        ) - 1.0f) < 0.005f
    );

    // ========================================
    // 3. WORLD GRAVITY DIRECTION
    // ========================================

    R3D_ResetAnimationSpringBone(&state);

    params = make_params();
    params.forces.gravityWorld = (Vector3){0, -12, 0};

    const Matrix rotatedWorld = MatrixRotateZ(PI * 0.5f);

    bool rotatedSteps = true;

    for (int i = 0; i < 240; i++)
    {
        if (!step_bone(
            &player, authored, &state,
            params, rotatedWorld, dt))
        {
            rotatedSteps = false;
            break;
        }
    }

    failures += check(
        "Rotated character gravity simulation completed",
        rotatedSteps
    );

    printf(
        "Rotated gravity forward X: %.6f (expected approximately -0.093)\n",
        forward(modelPose[BASE]).x
    );

    failures += check(
        "World-down gravity transforms into model-space force",
        rotatedSteps &&
        forward(modelPose[BASE]).x < -0.07f &&
        forward(modelPose[BASE]).x > -0.12f
    );

    // ========================================
    // 4. INDEPENDENT DRAG
    // ========================================

    params = make_params();
    params.frequencyHz = 1.0f;
    params.dampingRatio = 0.0f;

    R3D_SpringBoneState noDrag = {
        .tipModel = {0, 2, 0},
        .velocityModel = {4, 0, 0},
        .initialized = true
    };

    R3D_SpringBoneState highDrag = noDrag;

    params.forces.drag = 0.0f;

    bool noDragOk = step_bone(
        &player, authored, &noDrag,
        params, identity, 1.0f / 30.0f
    );

    params.forces.drag = 30.0f;

    bool highDragOk = step_bone(
        &player, authored, &highDrag,
        params, identity, 1.0f / 30.0f
    );

    failures += check(
        "Both drag simulations accepted",
        noDragOk && highDragOk
    );

    failures += check(
        "Independent drag reduces simulated velocity",
        noDragOk && highDragOk &&
        Vector3Length(highDrag.velocityModel) <
        Vector3Length(noDrag.velocityModel)
    );

    // ========================================
    // 5. WORLD-SPACE INERTIA
    // ========================================

    const Matrix movedWorld = MatrixTranslate(0.8f, 0, 0);

    params = make_params();
    params.frequencyHz = 1.5f;
    params.forces.worldInertia = 0.0f;

    R3D_SpringBoneState noInertia = {0};

    bool noInertiaOk =
        step_bone(
            &player, authored, &noInertia,
            params, identity, dt
        ) &&
        step_bone(
            &player, authored, &noInertia,
            params, movedWorld, dt
        );

    Vector3 noInertiaForward = forward(modelPose[BASE]);

    R3D_SpringBoneState fullInertia = {0};
    params.forces.worldInertia = 1.0f;

    bool inertiaOk =
        step_bone(
            &player, authored, &fullInertia,
            params, identity, dt
        ) &&
        step_bone(
            &player, authored, &fullInertia,
            params, movedWorld, dt
        );

    Vector3 inertiaForward = forward(modelPose[BASE]);

    failures += check(
        "World inertia comparison completed",
        noInertiaOk && inertiaOk
    );

    failures += check(
        "Zero inertia follows character translation",
        noInertiaOk &&
        fabsf(noInertiaForward.x) < 0.005f
    );

    failures += check(
        "Full inertia trails moving character",
        inertiaOk &&
        inertiaForward.x < -0.20f
    );

    failures += check(
        "World transform history updated",
        inertiaOk &&
        R3D_MatrixEquals(fullInertia.previousWorld, movedWorld)
    );

    // ========================================
    // 6. ANGULAR LIMITS WITH EXTERNAL FORCES
    // ========================================

    R3D_ResetAnimationSpringBone(&state);

    params = make_params();
    params.forces.gravityWorld = (Vector3){200, 0, 0};

    params.limits = (R3D_SpringBoneAngleLimits){
        .enabled = true,
        .localReferenceAxis = {1, 0, 0},
        .minHorizontalRadians = -25.0f * DEG2RAD,
        .maxHorizontalRadians =  25.0f * DEG2RAD,
        .minVerticalRadians = -10.0f * DEG2RAD,
        .maxVerticalRadians =  10.0f * DEG2RAD
    };

    bool limitedSteps = true;

    for (int i = 0; i < 180; i++)
    {
        if (!step_bone(
            &player, authored, &state,
            params, identity, dt))
        {
            limitedSteps = false;
            break;
        }
    }

    Vector3 limitedForward = forward(modelPose[BASE]);

    float horizontalAngle = atan2f(
        limitedForward.x,
        limitedForward.y
    );

    failures += check(
        "Gravity with angular limits completed",
        limitedSteps
    );

    failures += check(
        "Gravity respects horizontal maximum",
        limitedSteps &&
        horizontalAngle <= 25.0f * DEG2RAD + 0.005f &&
        horizontalAngle >= 20.0f * DEG2RAD
    );

    // ========================================
    // 7. INVALID INPUT PRESERVES STATE
    // ========================================

    Matrix beforeInvalid = localPose[BASE];
    R3D_SpringBoneState beforeInvalidState = state;

    R3D_SpringBoneParams invalid = params;
    invalid.forces.drag = -1.0f;

    failures += check(
        "Negative drag rejected",
        !R3D_ApplyAnimationSpringBoneWorld(
            &player, BASE, &state,
            invalid, identity, dt
        )
    );

    invalid = params;
    invalid.forces.worldInertia = 1.5f;

    failures += check(
        "World inertia above one rejected",
        !R3D_ApplyAnimationSpringBoneWorld(
            &player, BASE, &state,
            invalid, identity, dt
        )
    );

    invalid = params;
    invalid.forces.gravityWorld.x = NAN;

    failures += check(
        "Nonfinite gravity rejected",
        !R3D_ApplyAnimationSpringBoneWorld(
            &player, BASE, &state,
            invalid, identity, dt
        )
    );

    const Matrix singularWorld = MatrixScale(0, 1, 1);

    failures += check(
        "Noninvertible world transform rejected",
        !R3D_ApplyAnimationSpringBoneWorld(
            &player, BASE, &state,
            params, singularWorld, dt
        )
    );

    failures += check(
        "Rejected forces preserve local pose",
        R3D_MatrixEquals(localPose[BASE], beforeInvalid)
    );

    failures += check(
        "Rejected forces preserve spring state",
        near_vec3(state.tipModel, beforeInvalidState.tipModel) &&
        near_vec3(
            state.velocityModel,
            beforeInvalidState.velocityModel
        ) &&
        R3D_MatrixEquals(
            state.previousWorld,
            beforeInvalidState.previousWorld
        )
    );

    // ========================================
    // 8. SPRING CHAIN WITH WORLD INERTIA
    // ========================================

    params = make_params();
    params.forces.gravityWorld = (Vector3){3, 0, 0};
    params.forces.drag = 1.0f;
    params.forces.worldInertia = 1.0f;

    R3D_SpringChainBoneDesc descriptors[2] = {
        {.boneIndex = SECOND, .params = params},
        {.boneIndex = BASE, .params = params}
    };

    R3D_SpringChain chain = R3D_LoadSpringChain(
        &player, descriptors, 2
    );

    failures += check(
        "World-aware chain loaded",
        R3D_IsSpringChainValid(&chain)
    );

    if (R3D_IsSpringChainValid(&chain))
    {
        bool chainOk =
            step_chain(
                &player, authored, &chain,
                identity, dt
            ) &&
            step_chain(
                &player, authored, &chain,
                movedWorld, dt
            );

        failures += check(
            "World-aware chain updates",
            chainOk
        );

        failures += check(
            "Both chain bones retain world history",
            chainOk &&
            chain.bones[0].state.worldInitialized &&
            chain.bones[1].state.worldInitialized &&
            R3D_MatrixEquals(
                chain.bones[0].state.previousWorld,
                movedWorld
            ) &&
            R3D_MatrixEquals(
                chain.bones[1].state.previousWorld,
                movedWorld
            )
        );

        failures += check(
            "Chain base reacts to world translation",
            chainOk &&
            forward(modelPose[BASE]).x < -0.15f
        );

        Matrix beforeBadChain = localPose[BASE];
        chain.bones[1].params.forces.drag = -1.0f;

        failures += check(
            "Chain preflight rejects invalid drag",
            !R3D_UpdateSpringChainWorld(
                &chain, &player, movedWorld, dt
            )
        );

        failures += check(
            "Chain preflight prevents pose mutation",
            R3D_MatrixEquals(
                localPose[BASE],
                beforeBadChain
            )
        );

        R3D_ResetSpringChain(&chain);

        failures += check(
            "Chain reset clears world history",
            !chain.bones[0].state.worldInitialized &&
            !chain.bones[1].state.worldInitialized
        );
    }

    R3D_UnloadSpringChain(chain);

    // ========================================
    // 9. RESET
    // ========================================

    R3D_ResetAnimationSpringBone(&state);

    failures += check(
        "Spring reset clears world transform history",
        !state.initialized &&
        !state.worldInitialized
    );

    printf(
        "\nSpring forces tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED"
    );

    return failures == 0 ? 0 : 1;
}