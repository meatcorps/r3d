#include <r3d/r3d.h>
#include <r3d/r3d_animation_spring.h>
#include <r3d/r3d_animation_tree.h>
#include <raymath.h>
#include <math.h>

enum {
    TIP = 0,
    SPRING_B = 1,
    ROOT = 2,
    SPRING_A = 3,
    BONE_COUNT = 4
};

static Vector3 position(Matrix matrix) {
    return (Vector3){
        matrix.m12,
        matrix.m13,
        matrix.m14
    };
}

static R3D_Material colored_material(Color color) {
    R3D_Material material = R3D_GetDefaultMaterial();
    material.albedo.color = color;
    return material;
}

static void draw_segment(
    R3D_Mesh cube,
    R3D_Material material,
    Vector3 from,
    Vector3 to,
    float thickness) {
    Vector3 delta = Vector3Subtract(to, from);
    float length = Vector3Length(delta);

    if (length <= 1e-5f) return;

    Vector3 y = Vector3Scale(delta, 1.0f / length);

    // Stable segment orientation.
    // Do not derive an arbitrary roll from the two endpoints.
    Vector3 z = {0, 0, 1};

    z = Vector3Subtract(
        z,
        Vector3Scale(y, Vector3DotProduct(z, y))
    );

    if (Vector3LengthSqr(z) < 1e-8f) {
        z = (Vector3){1, 0, 0};

        z = Vector3Subtract(
            z,
            Vector3Scale(y, Vector3DotProduct(z, y))
        );
    }

    z = Vector3Normalize(z);

    Vector3 x = Vector3Normalize(Vector3CrossProduct(y, z));
    z = Vector3CrossProduct(x, y);

    Vector3 center = Vector3Scale(
        Vector3Add(from, to),
        0.5f
    );

    Matrix transform = {
        x.x * thickness, y.x * length, z.x * thickness, center.x,
        x.y * thickness, y.y * length, z.y * thickness, center.y,
        x.z * thickness, y.z * length, z.z * thickness, center.z,
        0, 0, 0, 1
    };

    R3D_DrawMeshPro(cube, material, transform);
}

static void draw_chain(
    R3D_Mesh cube,
    R3D_Mesh sphere,
    R3D_Material firstMaterial,
    R3D_Material secondMaterial,
    Vector3 first,
    Vector3 second,
    Vector3 tip,
    float thickness) {
    draw_segment(
        cube, firstMaterial,
        first, second, thickness
    );

    draw_segment(
        cube, secondMaterial,
        second, tip, thickness * 0.8f
    );

    R3D_DrawMesh(sphere, firstMaterial, first, thickness);
    R3D_DrawMesh(sphere, secondMaterial, second, thickness);
    R3D_DrawMesh(sphere, secondMaterial, tip, thickness * 0.8f);
}

int main(void) {
    InitWindow(1152, 648, "[r3d] - Spring Bone Visual");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());
    R3D_SetAntiAliasingMode(R3D_ANTI_ALIASING_MODE_FXAA);

    R3D_ENVIRONMENT_SET(
        ambient.color,
        (Color){85, 85, 85, 255}
    );

    R3D_Light light = R3D_CreateDirLight(
        (Vector3){-1, -2, -1},
        WHITE,
        1.5f
    );

    R3D_Mesh cube = R3D_GenMeshCube(1, 1, 1);
    R3D_Mesh sphere = R3D_GenMeshSphere(1, 20, 20);
    R3D_Mesh ground = R3D_GenMeshPlane(10, 10, 1, 1);

    R3D_Material firstMaterial = colored_material(
        (Color){40, 165, 245, 255}
    );

    R3D_Material secondMaterial = colored_material(
        (Color){45, 220, 140, 255}
    );

    R3D_Material authoredMaterial = colored_material(
        (Color){95, 95, 105, 255}
    );

    R3D_Material rootMaterial = colored_material(
        (Color){210, 125, 50, 255}
    );

    R3D_Material floorMaterial = colored_material(
        (Color){65, 70, 78, 255}
    );

    // ========================================
    // SKELETON
    // ========================================
    //
    // Root (2)
    //   SpringA (3)
    //     SpringB (1)
    //       Tip (0)
    //
    // Deliberately unordered indices.

    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "Tip", .parent = SPRING_B},
        {.name = "SpringB", .parent = SPRING_A},
        {.name = "Root", .parent = -1},
        {.name = "SpringA", .parent = ROOT}
    };

    Matrix localBind[BONE_COUNT] = {
        MatrixTranslate(0, 0.70f, 0),
        MatrixTranslate(0, 0.90f, 0),
        MatrixTranslate(0, 0.55f, 0),
        MatrixTranslate(0, 0.65f, 0)
    };

    Matrix modelBind[BONE_COUNT] = {
        MatrixTranslate(0, 2.80f, 0),
        MatrixTranslate(0, 2.10f, 0),
        MatrixTranslate(0, 0.55f, 0),
        MatrixTranslate(0, 1.20f, 0)
    };

    Matrix localPose[BONE_COUNT] = {0};
    Matrix modelPose[BONE_COUNT] = {0};

    // ========================================
    // REAL ANIMATION TREE CLIP
    // ========================================
    //
    // The root moves from left to right while rotating.
    // SpringA and SpringB follow through secondary motion.

    const float times[] = {
        0.0f, 0.4f, 0.8f, 1.2f, 1.6f
    };

    const Vector3 translations[] = {
        {0.00f, 0.55f, 0},
        {0.70f, 0.55f, 0},
        {0.00f, 0.55f, 0},
        {-0.70f, 0.55f, 0},
        {0.00f, 0.55f, 0}
    };

    Quaternion rotations[5] = {
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, 0.00f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, -0.32f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, 0.00f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, 0.32f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, 0.00f)
    };

    R3D_AnimationChannel channel = {
        .boneIndex = ROOT,
        .translation = {
            .times = times,
            .values = translations,
            .count = 5
        },
        .rotation = {
            .times = times,
            .values = rotations,
            .count = 5
        }
    };

    R3D_Animation animation = {
        .name = "spring_motion",
        .channels = &channel,
        .channelCount = 1,
        .boneCount = BONE_COUNT,
        .duration = 1.6f,
        .ticksPerSecond = 1.0f
    };

    R3D_AnimationLib library = {
        .animations = &animation,
        .count = 1
    };

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = BONE_COUNT,
            .localBind = localBind,
            .modelBind = modelBind,
            .rootBind = MatrixIdentity()
        },
        .animLib = library,
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    R3D_AnimationTree tree = R3D_LoadAnimationTree(player, 1);

    R3D_AnimationNodeParams nodeParams = {
        .name = "spring_motion",
        .state = {
            .play = true,
            .loop = true,
            .speed = 1.0f
        }
    };

    R3D_AnimationTreeNode *node = R3D_CreateAnimationNode(
        &tree, nodeParams
    );

    if (!node) {
        TraceLog(LOG_ERROR, "Failed to create spring animation node");

        R3D_UnloadAnimationTree(tree);
        R3D_UnloadMesh(cube);
        R3D_UnloadMesh(sphere);
        R3D_UnloadMesh(ground);
        R3D_Close();
        CloseWindow();

        return 1;
    }

    R3D_AddRootAnimationNode(&tree, node);

    // ========================================
    // SPRING CONFIGURATION
    // ========================================

    R3D_SpringBoneState firstState = {0};
    R3D_SpringBoneState secondState = {0};

    float frequency = 2.0f;
    float damping = 0.40f;
    float weight = 1.0f;

    float horizontalLimit = 35.0f * DEG2RAD;
    float verticalLimit = 15.0f * DEG2RAD;
    bool limitsEnabled = true;

    bool firstEnabled = true;
    bool secondEnabled = true;
    bool animationPaused = false;
    bool showAuthored = false;

    float teleportOffset = 0.0f;

    // ========================================
    // CAMERA
    // ========================================

    float cameraAngle = 0.35f;

    Camera3D camera = {
        .position = {1.8f, 2.8f, 5.0f},
        .target = {0, 1.5f, 0},
        .up = {0, 1, 0},
        .fovy = 60,
        .projection = CAMERA_PERSPECTIVE
    };

    // ========================================
    // MAIN LOOP
    // ========================================

    while (!WindowShouldClose()) {
        float dt = Clamp(GetFrameTime(), 0.0f, 1.0f / 30.0f);

        // ----------------------------------------
        // CONTROLS
        // ----------------------------------------

        if (IsKeyPressed(KEY_ONE)) {
            firstEnabled = !firstEnabled;
            R3D_ResetAnimationSpringBone(&firstState);
        }

        if (IsKeyPressed(KEY_TWO)) {
            secondEnabled = !secondEnabled;
            R3D_ResetAnimationSpringBone(&secondState);
        }

        if (IsKeyPressed(KEY_P)) {
            animationPaused = !animationPaused;
        }

        if (IsKeyPressed(KEY_H)) {
            showAuthored = !showAuthored;
        }

        if (IsKeyPressed(KEY_L)) {
            limitsEnabled = !limitsEnabled;

            R3D_ResetAnimationSpringBone(&firstState);
            R3D_ResetAnimationSpringBone(&secondState);
        }

        // Independent horizontal / vertical angle limits.
        if (IsKeyDown(KEY_J)) horizontalLimit -= dt * 0.6f;
        if (IsKeyDown(KEY_K)) horizontalLimit += dt * 0.6f;

        if (IsKeyDown(KEY_N)) verticalLimit -= dt * 0.6f;
        if (IsKeyDown(KEY_M)) verticalLimit += dt * 0.6f;

        horizontalLimit = Clamp(
            horizontalLimit, 0.0f, 80.0f * DEG2RAD
        );

        verticalLimit = Clamp(
            verticalLimit, 0.0f, 80.0f * DEG2RAD
        );

        if (IsKeyDown(KEY_UP)) frequency += dt * 1.5f;
        if (IsKeyDown(KEY_DOWN)) frequency -= dt * 1.5f;

        if (IsKeyDown(KEY_RIGHT)) damping += dt * 0.5f;
        if (IsKeyDown(KEY_LEFT)) damping -= dt * 0.5f;

        if (IsKeyDown(KEY_EQUAL)) weight += dt * 0.6f;
        if (IsKeyDown(KEY_MINUS)) weight -= dt * 0.6f;

        if (IsKeyDown(KEY_Z)) cameraAngle -= dt;
        if (IsKeyDown(KEY_C)) cameraAngle += dt;

        frequency = Clamp(frequency, 0.3f, 8.0f);
        damping = Clamp(damping, 0.0f, 1.0f);
        weight = Clamp(weight, 0.0f, 1.0f);

        if (IsKeyPressed(KEY_T)) {
            // Teleport the complete skeleton in model space.
            // Reset both springs so they don't stretch across
            // the discontinuous jump.
            teleportOffset = teleportOffset == 0.0f
                                 ? 2.0f
                                 : 0.0f;

            R3D_ResetAnimationSpringBone(&firstState);
            R3D_ResetAnimationSpringBone(&secondState);
        }

        if (IsKeyPressed(KEY_R)) {
            frequency = 2.0f;
            damping = 0.40f;
            weight = 1.0f;

            teleportOffset = 0.0f;

            firstEnabled = true;
            secondEnabled = true;
            animationPaused = false;
            showAuthored = false;

            R3D_ResetAnimationSpringBone(&firstState);
            R3D_ResetAnimationSpringBone(&secondState);
        }

        camera.position = (Vector3){
            sinf(cameraAngle) * 5.4f,
            2.8f,
            cosf(cameraAngle) * 5.4f
        };

        // ========================================
        // STAGE 1: ANIMATION TREE
        // ========================================

        bool poseValid =
                R3D_AdvanceAnimationTree(
                    &tree,
                    animationPaused ? 0.0f : dt
                ) &&
                R3D_ComputeAnimationTreeLocalPose(&tree);

        if (poseValid) {
            // Applied after animation sampling,
            // before accumulating the model hierarchy.
            localPose[ROOT].m12 += teleportOffset;

            poseValid = R3D_ComputeAnimationTreeModelPose(&tree);
        }

        Vector3 authoredFirst = {0};
        Vector3 authoredSecond = {0};
        Vector3 authoredTip = {0};

        if (poseValid) {
            authoredFirst = position(modelPose[SPRING_A]);
            authoredSecond = position(modelPose[SPRING_B]);
            authoredTip = position(modelPose[TIP]);
        }

        // ========================================
        // STAGE 2: FIRST SPRING BONE
        // ========================================

        R3D_SpringBoneParams firstParams = {
            .localAxis = {0, 1, 0},
            .length = 0.90f,
            .frequencyHz = frequency,
            .dampingRatio = damping,
            .weight = weight,

            .limits = {
                .enabled = limitsEnabled,
                .localReferenceAxis = {1, 0, 0},

                .minHorizontalRadians = -horizontalLimit,
                .maxHorizontalRadians = horizontalLimit,

                .minVerticalRadians = -verticalLimit,
                .maxVerticalRadians = verticalLimit
            }
        };

        bool firstSuccess = true;

        if (poseValid && firstEnabled) {
            firstSuccess =
                    R3D_ApplyAnimationSpringBone(
                        &tree.player,
                        SPRING_A,
                        &firstState,
                        firstParams,
                        dt
                    ) &&
                    R3D_RecomputeAnimationModelPoseFromBone(
                        &tree.player,
                        SPRING_A
                    );
        }

        // ========================================
        // STAGE 3: SECOND SPRING BONE
        // ========================================
        //
        // This spring must use the UPDATED hierarchy.
        // Its parent may already have moved because of
        // the first spring.

        R3D_SpringBoneParams secondParams = {
            .localAxis = {0, 1, 0},
            .length = 0.70f,
            .frequencyHz = frequency * 1.35f,
            .dampingRatio = damping,
            .weight = weight,

            .limits = {
                .enabled = limitsEnabled,
                .localReferenceAxis = {1, 0, 0},

                .minHorizontalRadians = -horizontalLimit,
                .maxHorizontalRadians = horizontalLimit,

                .minVerticalRadians = -verticalLimit,
                .maxVerticalRadians = verticalLimit
            }
        };

        bool secondSuccess = true;

        if (poseValid && firstSuccess && secondEnabled) {
            secondSuccess =
                    R3D_ApplyAnimationSpringBone(
                        &tree.player,
                        SPRING_B,
                        &secondState,
                        secondParams,
                        dt
                    ) &&
                    R3D_RecomputeAnimationModelPoseFromBone(
                        &tree.player,
                        SPRING_B
                    );
        }

        // ========================================
        // STAGE 4: FINAL POSE
        // ========================================

        Vector3 root = poseValid
                           ? position(modelPose[ROOT])
                           : Vector3Zero();

        Vector3 first = poseValid
                            ? position(modelPose[SPRING_A])
                            : Vector3Zero();

        Vector3 second = poseValid
                             ? position(modelPose[SPRING_B])
                             : Vector3Zero();

        Vector3 tip = poseValid
                          ? position(modelPose[TIP])
                          : Vector3Zero();

        float firstLength = Vector3Distance(first, second);
        float secondLength = Vector3Distance(second, tip);

        // ========================================
        // STAGE 5: RENDERING
        // ========================================

        BeginDrawing();
        ClearBackground(BLACK);

        R3D_Begin(camera);
        R3D_PushLight(light);

        R3D_DrawMesh(
            ground,
            floorMaterial,
            (Vector3){0, -0.05f, 0},
            1.0f
        );

        if (poseValid) {
            // Small base underneath the chain.
            draw_segment(
                cube,
                rootMaterial,
                root,
                first,
                0.38f
            );

            if (showAuthored) {
                draw_chain(
                    cube, sphere,
                    authoredMaterial,
                    authoredMaterial,
                    authoredFirst,
                    authoredSecond,
                    authoredTip,
                    0.055f
                );
            }

            draw_chain(
                cube, sphere,
                firstMaterial,
                secondMaterial,
                first,
                second,
                tip,
                0.16f
            );
        }
        R3D_End();

        DrawRectangle(
            8, 8, 615, 270,
            Fade(BLACK, 0.72f)
        );

        DrawText(
            "SPRING BONES / SECONDARY MOTION",
            18, 16, 22, RAYWHITE
        );

        DrawText(
            TextFormat(
                "Spring A: %s  |  Spring B: %s  |  Animation: %s",
                firstEnabled ? "ON" : "OFF",
                secondEnabled ? "ON" : "OFF",
                animationPaused ? "PAUSED" : "PLAYING"
            ),
            18, 47, 16, LIGHTGRAY
        );

        DrawText(
            TextFormat(
                "Frequency: %.2f Hz  |  Damping: %.2f  |  Weight: %.2f",
                frequency, damping, weight
            ),
            18, 72, 16, LIGHTGRAY
        );

        DrawText(
            TextFormat(
                "Segment lengths: %.3f / %.3f",
                firstLength, secondLength
            ),
            18, 96, 16,
            fabsf(firstLength - 0.90f) < 0.003f &&
            fabsf(secondLength - 0.70f) < 0.003f
                ? GREEN
                : ORANGE
        );

        DrawText(
            "UP/DOWN Frequency    LEFT/RIGHT Damping    -/= Weight",
            18, 123, 16, LIGHTGRAY
        );

        DrawText(
            "1/2 Toggle springs    P Pause animation    H Authored pose",
            18, 147, 16, LIGHTGRAY
        );

        DrawText(
    "T Teleport + reset    Z/C Rotate camera    R Reset",
    18, 171, 16, LIGHTGRAY
);

        DrawText(
            TextFormat(
                "Angle limits: %s  |  Horizontal: +/-%.1f  |  Vertical: +/-%.1f",
                limitsEnabled ? "ON" : "OFF",
                horizontalLimit * RAD2DEG,
                verticalLimit * RAD2DEG
            ),
            18, 194, 16, LIGHTGRAY
        );

        DrawText(
            "L Toggle limits    J/K Horizontal    N/M Vertical",
            18, 218, 16, LIGHTGRAY
        );

        if (!poseValid || !firstSuccess || !secondSuccess)
        {
            DrawText(
                "SPRING / POSE ERROR",
                18, 242, 16, RED
            );
        }

        DrawFPS(GetScreenWidth() - 100, 12);
        EndDrawing();
    }

    R3D_UnloadAnimationTree(tree);

    R3D_UnloadMesh(cube);
    R3D_UnloadMesh(sphere);
    R3D_UnloadMesh(ground);

    R3D_Close();
    CloseWindow();

    return 0;
}
