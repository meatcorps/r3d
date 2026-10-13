#include <r3d/r3d_animation_spring_chain.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

enum {
    B = 0,
    ROOT = 1,
    C = 2,
    TIP = 3,
    A = 4,
    OTHER = 5,
    BONE_COUNT = 6
};

static int check(const char* name, bool passed)
{
    printf("%s: %s\n", name, passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}

static bool near_vec3(Vector3 a, Vector3 b)
{
    return Vector3Distance(a, b) < 0.0001f;
}

static R3D_SpringBoneParams make_params(float frequency)
{
    return (R3D_SpringBoneParams){
        .localAxis = {0, 1, 0},
        .length = 0.6f,
        .frequencyHz = frequency,
        .dampingRatio = 0.5f,
        .weight = 1.0f,
        .limits = {
            .enabled = true,
            .localReferenceAxis = {1, 0, 0},
            .minHorizontalRadians = -60.0f * DEG2RAD,
            .maxHorizontalRadians =  60.0f * DEG2RAD,
            .minVerticalRadians = -30.0f * DEG2RAD,
            .maxVerticalRadians =  30.0f * DEG2RAD
        }
    };
}

static bool manual_update(
    R3D_AnimationPlayer* player,
    const R3D_SpringChain* chain,
    R3D_SpringBoneState* states,
    float dt)
{
    for (int i = 0; i < chain->boneCount; i++)
    {
        if (!R3D_ApplyAnimationSpringBone(
                player,
                chain->bones[i].boneIndex,
                &states[i],
                chain->bones[i].params,
                dt))
        {
            return false;
        }

        if (!R3D_RecomputeAnimationModelPoseFromBone(
                player,
                chain->bones[i].boneIndex))
        {
            return false;
        }
    }

    return true;
}

int main(void)
{
    // Root (1)
    //   A (4) -> B (0) -> C (2) -> Tip (3)
    //   Other (5)
    //
    // Input descriptors are deliberately C, A, B.

    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "B", .parent = A},
        {.name = "Root", .parent = -1},
        {.name = "C", .parent = B},
        {.name = "Tip", .parent = C},
        {.name = "A", .parent = ROOT},
        {.name = "Other", .parent = ROOT}
    };

    Matrix initial[BONE_COUNT] = {
        MatrixTranslate(0, 0.6f, 0),
        MatrixTranslate(1, 0, 0),
        MatrixTranslate(0, 0.6f, 0),
        MatrixTranslate(0, 0.6f, 0),
        MatrixTranslate(0, 0.6f, 0),
        MatrixTranslate(-1, 0.5f, 0)
    };

    Matrix localPose[BONE_COUNT];
    Matrix modelPose[BONE_COUNT];

    Matrix manualLocal[BONE_COUNT];
    Matrix manualModel[BONE_COUNT];

    for (int i = 0; i < BONE_COUNT; i++)
    {
        localPose[i] = initial[i];
        manualLocal[i] = initial[i];

        modelPose[i] = MatrixIdentity();
        manualModel[i] = MatrixIdentity();
    }

    R3D_Skeleton skeleton = {
        .bones = bones,
        .boneCount = BONE_COUNT,
        .rootBind = MatrixTranslate(10, 0, 0)
    };

    R3D_AnimationPlayer player = {
        .skeleton = skeleton,
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    R3D_AnimationPlayer manual = {
        .skeleton = skeleton,
        .localPose = manualLocal,
        .modelPose = manualModel,
        .activeAnimIndex = -1
    };

    R3D_SpringChainBoneDesc descriptors[] = {
        {.boneIndex = C, .params = make_params(2.0f)},
        {.boneIndex = A, .params = make_params(5.0f)},
        {.boneIndex = B, .params = make_params(3.5f)}
    };

    int failures = 0;

    R3D_SpringChain chain = R3D_LoadSpringChain(
        &player, descriptors, 3
    );

    failures += check("Load unordered spring chain",
        R3D_IsSpringChainValid(&chain));

    if (!R3D_IsSpringChainValid(&chain))
    {
        return 1;
    }

    failures += check("Chain automatically sorted root to tip",
        chain.bones[0].boneIndex == A &&
        chain.bones[1].boneIndex == B &&
        chain.bones[2].boneIndex == C);

    failures += check("Per-bone configuration preserved",
        chain.bones[0].params.frequencyHz == 5.0f &&
        chain.bones[1].params.frequencyHz == 3.5f &&
        chain.bones[2].params.frequencyHz == 2.0f);

    // ========================================
    // INVALID CHAIN DEFINITIONS
    // ========================================

    R3D_SpringChainBoneDesc duplicates[] = {
        {.boneIndex = A, .params = make_params(2)},
        {.boneIndex = A, .params = make_params(2)}
    };

    R3D_SpringChain invalid = R3D_LoadSpringChain(
        &player, duplicates, 2
    );

    failures += check("Duplicate bones rejected",
        !R3D_IsSpringChainValid(&invalid));

    R3D_UnloadSpringChain(invalid);

    R3D_SpringChainBoneDesc disconnected[] = {
        {.boneIndex = A, .params = make_params(2)},
        {.boneIndex = C, .params = make_params(2)}
    };

    invalid = R3D_LoadSpringChain(
        &player, disconnected, 2
    );

    failures += check("Disconnected bones rejected",
        !R3D_IsSpringChainValid(&invalid));

    R3D_UnloadSpringChain(invalid);

    R3D_SpringChainBoneDesc branching[] = {
        {.boneIndex = A, .params = make_params(2)},
        {.boneIndex = B, .params = make_params(2)},
        {.boneIndex = OTHER, .params = make_params(2)}
    };

    invalid = R3D_LoadSpringChain(
        &player, branching, 3
    );

    failures += check("Multiple paths rejected",
        !R3D_IsSpringChainValid(&invalid));

    R3D_UnloadSpringChain(invalid);

    R3D_SpringChainBoneDesc invalidParams[] = {
        {.boneIndex = A, .params = make_params(0)}
    };

    invalid = R3D_LoadSpringChain(
        &player, invalidParams, 1
    );

    failures += check("Invalid parameters rejected at creation",
        !R3D_IsSpringChainValid(&invalid));

    R3D_UnloadSpringChain(invalid);

    // ========================================
    // FIRST EVALUATION
    // ========================================

    failures += check("Compute chain hierarchy",
        R3D_RecomputeAnimationModelPose(&player) &&
        R3D_RecomputeAnimationModelPose(&manual));

    R3D_SpringBoneState manualStates[3] = {0};

    failures += check("Initialize complete spring chain",
        R3D_UpdateSpringChain(&chain, &player, 1.0f / 60.0f));

    failures += check("Initialize manual reference springs",
        manual_update(
            &manual, &chain, manualStates, 1.0f / 60.0f
        ));

    bool initialized = true;
    bool untouched = true;

    for (int i = 0; i < 3; i++)
    {
        initialized &= chain.bones[i].state.initialized;
    }

    for (int i = 0; i < BONE_COUNT; i++)
    {
        untouched &= R3D_MatrixEquals(localPose[i], initial[i]);
    }

    failures += check("All spring states initialized", initialized);
    failures += check("First evaluation preserves authored pose", untouched);

    // ========================================
    // ANIMATED ROOT MOVEMENT
    // ========================================

    Transform animatedRoot = {
        .translation = {1, 0, 0},
        .rotation = QuaternionFromAxisAngle(
            (Vector3){0, 0, 1}, -0.65f
        ),
        .scale = {1, 1, 1}
    };

    failures += check("Change authored root transform",
        R3D_SetAnimationBoneLocalTransform(
            &player, ROOT, animatedRoot
        ) &&
        R3D_SetAnimationBoneLocalTransform(
            &manual, ROOT, animatedRoot
        ));

    failures += check("Recompute new authored pose",
        R3D_RecomputeAnimationModelPose(&player) &&
        R3D_RecomputeAnimationModelPose(&manual));

    Matrix siblingBefore = modelPose[OTHER];
    Matrix firstBefore = localPose[A];

    failures += check("Update entire spring chain",
        R3D_UpdateSpringChain(&chain, &player, 1.0f / 60.0f));

    failures += check("Update manual springs in chain order",
        manual_update(
            &manual, &chain, manualStates, 1.0f / 60.0f
        ));

    bool equivalent = true;
    bool statesEquivalent = true;

    for (int i = 0; i < BONE_COUNT; i++)
    {
        equivalent &=
            R3D_MatrixEquals(localPose[i], manualLocal[i]) &&
            R3D_MatrixEquals(modelPose[i], manualModel[i]);
    }

    for (int i = 0; i < 3; i++)
    {
        statesEquivalent &=
            near_vec3(
                chain.bones[i].state.tipModel,
                manualStates[i].tipModel
            ) &&
            near_vec3(
                chain.bones[i].state.velocityModel,
                manualStates[i].velocityModel
            );
    }

    failures += check("Chain matches individual spring evaluation",
        equivalent);

    failures += check("Persistent spring states match manual evaluation",
        statesEquivalent);

    failures += check("Spring correction modifies animated pose",
        !R3D_MatrixEquals(localPose[A], firstBefore));

    failures += check("Unrelated sibling remains unchanged",
        R3D_MatrixEquals(modelPose[OTHER], siblingBefore));

    // ========================================
    // VALIDATION BEFORE MUTATION
    // ========================================

    Matrix beforeInvalid = localPose[A];
    R3D_SpringBoneState stateBeforeInvalid = chain.bones[0].state;

    chain.bones[2].params.dampingRatio = 1.5f;

    failures += check("Invalid runtime settings rejected",
        !R3D_UpdateSpringChain(&chain, &player, 1.0f / 60.0f));

    failures += check("Invalid settings do not partially modify pose",
        R3D_MatrixEquals(localPose[A], beforeInvalid));

    failures += check("Invalid settings preserve state",
        near_vec3(
            chain.bones[0].state.tipModel,
            stateBeforeInvalid.tipModel
        ));

    chain.bones[2].params.dampingRatio = 0.5f;

    R3D_BoneInfo otherBones[BONE_COUNT];

    for (int i = 0; i < BONE_COUNT; i++)
    {
        otherBones[i] = bones[i];
    }

    R3D_AnimationPlayer wrongPlayer = player;
    wrongPlayer.skeleton.bones = otherBones;

    failures += check("Different skeleton instance rejected",
        !R3D_UpdateSpringChain(
            &chain, &wrongPlayer, 1.0f / 60.0f
        ));

    failures += check("Negative delta time rejected",
        !R3D_UpdateSpringChain(&chain, &player, -0.01f));

    // ========================================
    // RESET / REINITIALIZE
    // ========================================

    R3D_ResetSpringChain(&chain);

    bool allReset = true;

    for (int i = 0; i < chain.boneCount; i++)
    {
        allReset &= !chain.bones[i].state.initialized;
        allReset &= near_vec3(
            chain.bones[i].state.velocityModel,
            Vector3Zero()
        );
    }

    failures += check("Reset clears all simulation states", allReset);

    // Restore freshly sampled local spring poses.
    localPose[A] = initial[A];
    localPose[B] = initial[B];
    localPose[C] = initial[C];

    failures += check("Recompute freshly sampled pose",
        R3D_RecomputeAnimationModelPose(&player));

    Matrix authoredA = localPose[A];

    failures += check("Reinitialize complete chain",
        R3D_UpdateSpringChain(&chain, &player, 1.0f / 60.0f));

    failures += check("Reinitialization preserves authored rotation",
        R3D_MatrixEquals(localPose[A], authoredA));

    R3D_UnloadSpringChain(chain);

    printf("\nSpring chain tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}