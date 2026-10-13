#include <r3d/r3d_animation_spring_chain.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

enum {
    SPRING = 0,
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

static bool near_vec(Vector3 a, Vector3 b)
{
    return Vector3Distance(a, b) < 0.001f;
}

static bool finite_vec(Vector3 v)
{
    return isfinite(v.x) &&
           isfinite(v.y) &&
           isfinite(v.z);
}

static Vector3 position(Matrix m)
{
    return (Vector3){m.m12, m.m13, m.m14};
}

static Vector3 forward(Matrix m)
{
    return Vector3Normalize(Vector3Subtract(
        Vector3Transform((Vector3){0, 1, 0}, m),
        position(m)
    ));
}

static float yaw(Matrix m)
{
    Vector3 f = forward(m);
    return atan2f(f.x, f.y);
}

static void restore(
    R3D_AnimationPlayer* player,
    Matrix root)
{
    player->localPose[ROOT] = root;

    player->localPose[SPRING] =
        MatrixTranslate(0, 1, 0);

    player->localPose[SECOND] =
        MatrixTranslate(0, 1, 0);

    player->localPose[TIP] =
        MatrixTranslate(0, 1, 0);
}

static bool step_bone(
    R3D_AnimationPlayer* player,
    R3D_SpringBoneState* state,
    R3D_SpringBoneParams params,
    Matrix root,
    Matrix world,
    float dt)
{
    restore(player, root);

    return R3D_RecomputeAnimationModelPose(player) &&
           R3D_ApplyAnimationSpringBoneWorld(
               player, SPRING, state, params, world, dt
           ) &&
           R3D_RecomputeAnimationModelPoseFromBone(
               player, SPRING
           );
}

static bool step_chain(
    R3D_AnimationPlayer* player,
    R3D_SpringChain* chain,
    Matrix root,
    Matrix world,
    float dt)
{
    restore(player, root);

    return R3D_RecomputeAnimationModelPose(player) &&
           R3D_UpdateSpringChainWorld(
               chain, player, world, dt
           );
}

static R3D_SpringBoneParams make_params(void)
{
    return (R3D_SpringBoneParams){
        .localAxis = {0, 1, 0},
        .length = 1.0f,
        .frequencyHz = 2.0f,
        .dampingRatio = 0.5f,
        .weight = 1.0f,

        .limits = {
            .enabled = true,
            .localReferenceAxis = {1, 0, 0},

            .minHorizontalRadians = -30.0f * DEG2RAD,
            .maxHorizontalRadians =  30.0f * DEG2RAD,

            .minVerticalRadians = -30.0f * DEG2RAD,
            .maxVerticalRadians =  30.0f * DEG2RAD
        }
    };
}

static R3D_SpringBoneState angled_state(
    float angleRadians,
    float outwardSpeed)
{
    float sine = sinf(angleRadians);
    float cosine = cosf(angleRadians);

    return (R3D_SpringBoneState){
        .tipModel = {sine, 1.0f + cosine, 0},
        .velocityModel = {
            cosine * outwardSpeed,
            -sine * outwardSpeed,
            0
        },
        .initialized = true
    };
}

int main(void)
{
    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "Spring", .parent = ROOT},
        {.name = "Root", .parent = -1},
        {.name = "Second", .parent = SPRING},
        {.name = "Tip", .parent = SECOND}
    };

    Matrix localPose[BONE_COUNT];
    Matrix modelPose[BONE_COUNT];

    for (int i = 0; i < BONE_COUNT; i++)
    {
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

    const Matrix identity = MatrixIdentity();
    const float dt = 1.0f / 60.0f;

    R3D_SpringBoneParams params = make_params();
    R3D_SpringBoneState state = {0};

    int failures = 0;

    // ========================================
    // 1. SOFT ANGULAR LIMITS
    // ========================================

    const float angle = 25.0f * DEG2RAD;

    R3D_SpringBoneState hard = angled_state(angle, 6.0f);
    R3D_SpringBoneState soft = hard;

    params.limits.softZoneRadians = 0.0f;

    bool hardOk = step_bone(
        &player, &hard, params,
        identity, identity, 0.0f
    );

    float hardSpeed = Vector3Length(hard.velocityModel);

    params.limits.softZoneRadians = 10.0f * DEG2RAD;

    bool softOk = step_bone(
        &player, &soft, params,
        identity, identity, 0.0f
    );

    float softSpeed = Vector3Length(soft.velocityModel);

    failures += check(
        "Hard and soft limits accepted",
        hardOk && softOk
    );

    failures += check(
        "Soft zone reduces outward velocity",
        hardOk && softOk &&
        softSpeed < hardSpeed * 0.7f &&
        softSpeed > hardSpeed * 0.3f
    );

    failures += check(
        "Soft zone preserves permitted angle",
        softOk &&
        fabsf(yaw(modelPose[SPRING]) - angle) < 0.002f
    );

    printf(
        "Soft limit: outward speed %.3f -> %.3f\n",
        hardSpeed, softSpeed
    );

    // Extremely large angular displacement must
    // still respect the hard maximum.
    state = angled_state(80.0f * DEG2RAD, 20.0f);

    bool limitOk = step_bone(
        &player, &state, params,
        identity, identity, 0.0f
    );

    failures += check(
        "Soft constraint never exceeds hard angular limit",
        limitOk &&
        yaw(modelPose[SPRING]) <= 30.0f * DEG2RAD + 0.002f
    );

    // ========================================
    // 2. WORLD TRANSLATION TELEPORT
    // ========================================

    params = make_params();
    params.forces.worldInertia = 1.0f;
    params.stability.teleportDistance = 1.0f;

    R3D_ResetAnimationSpringBone(&state);

    const Matrix movedWorld = MatrixTranslate(5, 0, 0);

    bool translationOk =
        step_bone(
            &player, &state, params,
            identity, identity, dt
        ) &&
        step_bone(
            &player, &state, params,
            identity, movedWorld, dt
        );

    failures += check(
        "World translation teleport accepted",
        translationOk
    );

    failures += check(
        "World teleport resets spring to authored tip",
        translationOk &&
        near_vec(state.tipModel, (Vector3){0, 2, 0}) &&
        near_vec(state.velocityModel, Vector3Zero())
    );

    failures += check(
        "World teleport preserves authored orientation",
        translationOk &&
        near_vec(forward(modelPose[SPRING]),
                 (Vector3){0, 1, 0})
    );

    failures += check(
        "World teleport updates tracking",
        translationOk &&
        state.trackingInitialized &&
        near_vec(
            state.previousOriginWorld,
            (Vector3){5, 1, 0}
        )
    );

    // ========================================
    // 3. WORLD ROTATION TELEPORT
    // ========================================

    params.stability.teleportDistance = 0.0f;
    params.stability.teleportAngleRadians =
        45.0f * DEG2RAD;

    R3D_ResetAnimationSpringBone(&state);

    const Matrix rotatedWorld = MatrixRotateZ(PI * 0.5f);

    bool rotationOk =
        step_bone(
            &player, &state, params,
            identity, identity, dt
        ) &&
        step_bone(
            &player, &state, params,
            identity, rotatedWorld, dt
        );

    failures += check(
        "World rotation teleport accepted",
        rotationOk
    );

    failures += check(
        "World rotation teleport resets inertia",
        rotationOk &&
        near_vec(state.tipModel, (Vector3){0, 2, 0}) &&
        near_vec(state.velocityModel, Vector3Zero())
    );

    // ========================================
    // 4. AUTHORED SKELETON TELEPORT
    // ========================================

    params.stability.teleportDistance = 1.0f;
    params.stability.teleportAngleRadians = 0.0f;

    R3D_ResetAnimationSpringBone(&state);

    bool authoredTeleportOk =
        step_bone(
            &player, &state, params,
            identity, identity, dt
        ) &&
        step_bone(
            &player, &state, params,
            MatrixTranslate(5, 0, 0),
            identity, dt
        );

    failures += check(
        "Authored root teleport accepted",
        authoredTeleportOk
    );

    failures += check(
        "Authored root teleport resets spring history",
        authoredTeleportOk &&
        near_vec(state.tipModel, (Vector3){5, 2, 0}) &&
        near_vec(state.velocityModel, Vector3Zero())
    );

    // ========================================
    // 5. EXTREME MOVEMENT
    // ========================================

    params = make_params();

    params.forces.worldInertia = 1.0f;
    params.forces.gravityWorld = (Vector3){120000, 0, 0};

    params.stability.maxTipOffset = 2.0f;
    params.stability.maxTipSpeed = 3.0f;

    R3D_ResetAnimationSpringBone(&state);

    bool extremeInitialized = step_bone(
        &player, &state, params,
        identity, identity, dt
    );

    // Simulate an extreme inherited state/impulse.
    state.velocityModel = (Vector3){100000, -5000, 0};

    bool extremeOk = extremeInitialized &&
        step_bone(
            &player, &state, params,
            identity,
            MatrixTranslate(10000, 0, 0),
            1.0f / 30.0f
        );

    failures += check(
        "Extreme world movement and gravity accepted",
        extremeOk
    );

    failures += check(
        "Extreme movement produces finite state",
        extremeOk &&
        finite_vec(state.tipModel) &&
        finite_vec(state.velocityModel)
    );

    failures += check(
        "Extreme movement respects velocity cap",
        extremeOk &&
        Vector3Length(state.velocityModel) <= 3.001f
    );

    failures += check(
        "Extreme movement preserves spring segment length",
        extremeOk &&
        fabsf(Vector3Distance(
            state.tipModel,
            position(modelPose[SPRING])
        ) - 1.0f) < 0.002f
    );

    failures += check(
        "Extreme movement respects angular limits",
        extremeOk &&
        fabsf(yaw(modelPose[SPRING])) <=
            30.0f * DEG2RAD + 0.002f
    );

    // ========================================
    // 6. INVALID SETTINGS
    // ========================================

    R3D_SpringBoneState preservedState = state;
    Matrix preservedPose = localPose[SPRING];

    R3D_SpringBoneParams invalid = params;
    invalid.stability.maxTipSpeed = -1.0f;

    failures += check(
        "Negative speed cap rejected",
        !R3D_ApplyAnimationSpringBoneWorld(
            &player, SPRING, &state,
            invalid, identity, dt
        )
    );

    invalid = params;
    invalid.stability.teleportAngleRadians = PI + 0.1f;

    failures += check(
        "Invalid teleport angle rejected",
        !R3D_ApplyAnimationSpringBoneWorld(
            &player, SPRING, &state,
            invalid, identity, dt
        )
    );

    invalid = params;
    invalid.limits.softZoneRadians = -0.1f;

    failures += check(
        "Negative soft zone rejected",
        !R3D_ApplyAnimationSpringBoneWorld(
            &player, SPRING, &state,
            invalid, identity, dt
        )
    );

    failures += check(
        "Invalid settings preserve local pose",
        R3D_MatrixEquals(
            localPose[SPRING],
            preservedPose
        )
    );

    failures += check(
        "Invalid settings preserve simulation state",
        near_vec(state.tipModel, preservedState.tipModel) &&
        near_vec(state.velocityModel,
                 preservedState.velocityModel) &&
        near_vec(state.previousOriginWorld,
                 preservedState.previousOriginWorld)
    );

    // ========================================
    // 7. CHAIN-WIDE TELEPORT RECOVERY
    // ========================================

    params = make_params();
    params.forces.worldInertia = 1.0f;
    params.stability.teleportDistance = 1.0f;
    params.stability.maxTipOffset = 2.0f;
    params.stability.maxTipSpeed = 10.0f;

    R3D_SpringChainBoneDesc descriptors[2] = {
        {.boneIndex = SECOND, .params = params},
        {.boneIndex = SPRING, .params = params}
    };

    R3D_SpringChain chain = R3D_LoadSpringChain(
        &player, descriptors, 2
    );

    bool chainValid = R3D_IsSpringChainValid(&chain);

    failures += check(
        "Stability chain loaded and sorted",
        chainValid &&
        chain.bones[0].boneIndex == SPRING &&
        chain.bones[1].boneIndex == SECOND
    );

    if (chainValid)
    {
        bool chainOk =
            step_chain(
                &player, &chain,
                identity, identity, dt
            ) &&
            step_chain(
                &player, &chain,
                identity,
                MatrixTranslate(5, 0, 0),
                dt
            );

        failures += check(
            "Chain teleport simulation succeeds",
            chainOk
        );

        failures += check(
            "All chain bones recover at authored tips",
            chainOk &&
            near_vec(
                chain.bones[0].state.tipModel,
                (Vector3){0, 2, 0}
            ) &&
            near_vec(
                chain.bones[1].state.tipModel,
                (Vector3){0, 3, 0}
            )
        );

        failures += check(
            "All chain velocities reset on teleport",
            chainOk &&
            near_vec(
                chain.bones[0].state.velocityModel,
                Vector3Zero()
            ) &&
            near_vec(
                chain.bones[1].state.velocityModel,
                Vector3Zero()
            )
        );

        Matrix previousLocal = localPose[SPRING];
        R3D_SpringBoneState previousChainState =
            chain.bones[0].state;

        chain.bones[1].params.stability.maxTipOffset = -1;

        failures += check(
            "Chain preflight rejects invalid stability setting",
            !R3D_UpdateSpringChainWorld(
                &chain,
                &player,
                identity,
                dt
            )
        );

        failures += check(
            "Chain preflight prevents partial mutation",
            R3D_MatrixEquals(
                previousLocal,
                localPose[SPRING]
            ) &&
            near_vec(
                previousChainState.tipModel,
                chain.bones[0].state.tipModel
            )
        );

        R3D_ResetSpringChain(&chain);

        failures += check(
            "Chain reset clears teleport tracking",
            !chain.bones[0].state.trackingInitialized &&
            !chain.bones[1].state.trackingInitialized
        );
    }

    R3D_UnloadSpringChain(chain);

    printf(
        "\nSpring stability tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED"
    );

    return failures == 0 ? 0 : 1;
}