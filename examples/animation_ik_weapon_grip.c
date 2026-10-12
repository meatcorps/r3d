#include <r3d/r3d.h>
#include <r3d/r3d_animation_tree.h>
#include <r3d/r3d_animation_ik.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

enum {
    RIGHT_HAND = 0,
    LEFT_HAND = 1,
    RIGHT_ELBOW = 2,
    RIGHT_SHOULDER = 3,
    LEFT_ELBOW = 4,
    LEFT_SHOULDER = 5,
    ROOT = 6,
    BONE_COUNT = 7
};

static Vector3 position(Matrix matrix)
{
    return (Vector3){matrix.m12, matrix.m13, matrix.m14};
}

static R3D_Material colored_material(Color color)
{
    R3D_Material material = R3D_GetDefaultMaterial();
    material.albedo.color = color;
    return material;
}

static Quaternion segment_rotation(Vector3 vector)
{
    Vector3 up = {0, 1, 0};
    vector = Vector3Normalize(vector);

    float dot = Vector3DotProduct(up, vector);

    if (dot > 0.999999f) return QuaternionIdentity();
    if (dot < -0.999999f) return QuaternionFromAxisAngle((Vector3){1, 0, 0}, PI);

    return QuaternionFromVector3ToVector3(up, vector);
}

static void draw_segment(R3D_Mesh cube, R3D_Material material, Vector3 from, Vector3 to, float thickness)
{
    Vector3 delta = Vector3Subtract(to, from);
    float length = Vector3Length(delta);

    if (length < 1e-5f) return;

    R3D_DrawMeshEx(
        cube,
        material,
        Vector3Scale(Vector3Add(from, to), 0.5f),
        segment_rotation(delta),
        (Vector3){thickness, length, thickness}
    );
}

static void draw_arm(R3D_Mesh cube, R3D_Mesh sphere, R3D_Material material,
                     Vector3 shoulder, Vector3 elbow, Vector3 hand, float thickness)
{
    draw_segment(cube, material, shoulder, elbow, thickness);
    draw_segment(cube, material, elbow, hand, thickness);

    R3D_DrawMesh(sphere, material, shoulder, thickness * 0.8f);
    R3D_DrawMesh(sphere, material, elbow, thickness * 0.8f);
    R3D_DrawMesh(sphere, material, hand, thickness * 0.8f);
}

static Vector3 bone_world(const R3D_AnimationPlayer* player, int boneIndex, Matrix characterWorld)
{
    return Vector3Transform(position(player->modelPose[boneIndex]), characterWorld);
}

int main(void)
{
    InitWindow(1152, 648, "[r3d] - Two Handed Weapon Grip IK");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());
    R3D_SetAntiAliasingMode(R3D_ANTI_ALIASING_MODE_FXAA);
    R3D_ENVIRONMENT_SET(ambient.color, (Color){85, 85, 85, 255});

    R3D_Light light = R3D_CreateDirLight((Vector3){-1, -2, -1}, WHITE, 1.6f);

    R3D_Mesh cube = R3D_GenMeshCube(1, 1, 1);
    R3D_Mesh sphere = R3D_GenMeshSphere(1, 20, 20);
    R3D_Mesh ground = R3D_GenMeshPlane(9, 9, 1, 1);

    R3D_Material rightMaterial = colored_material((Color){35, 160, 250, 255});
    R3D_Material leftMaterial = colored_material((Color){55, 220, 155, 255});
    R3D_Material authoredMaterial = colored_material((Color){100, 100, 110, 255});
    R3D_Material bodyMaterial = colored_material((Color){75, 90, 125, 255});
    R3D_Material weaponMaterial = colored_material((Color){165, 175, 185, 255});
    R3D_Material primaryMaterial = colored_material((Color){255, 215, 45, 255});
    R3D_Material supportMaterial = colored_material((Color){255, 125, 50, 255});
    R3D_Material poleMaterial = colored_material((Color){230, 60, 170, 255});
    R3D_Material floorMaterial = colored_material((Color){60, 66, 72, 255});

    // Deliberately unordered:
    //
    // Root (6)
    //   RightShoulder (3)
    //     RightElbow (2)
    //       RightHand (0)
    //   LeftShoulder (5)
    //     LeftElbow (4)
    //       LeftHand (1)

    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "RightHand", .parent = RIGHT_ELBOW},
        {.name = "LeftHand", .parent = LEFT_ELBOW},
        {.name = "RightElbow", .parent = RIGHT_SHOULDER},
        {.name = "RightShoulder", .parent = ROOT},
        {.name = "LeftElbow", .parent = LEFT_SHOULDER},
        {.name = "LeftShoulder", .parent = ROOT},
        {.name = "Root", .parent = -1}
    };

    Matrix localBind[BONE_COUNT] = {
        MatrixTranslate(0.70f, -0.12f, 0),
        MatrixTranslate(-0.70f, -0.12f, 0),
        MatrixTranslate(0.70f, -0.12f, 0),
        MatrixTranslate(0.38f, 1.32f, 0),
        MatrixTranslate(-0.70f, -0.12f, 0),
        MatrixTranslate(-0.38f, 1.32f, 0),
        MatrixIdentity()
    };

    Matrix modelBind[BONE_COUNT] = {
        MatrixTranslate(1.78f, 1.08f, 0),
        MatrixTranslate(-1.78f, 1.08f, 0),
        MatrixTranslate(1.08f, 1.20f, 0),
        MatrixTranslate(0.38f, 1.32f, 0),
        MatrixTranslate(-1.08f, 1.20f, 0),
        MatrixTranslate(-0.38f, 1.32f, 0),
        MatrixIdentity()
    };

    Matrix localPose[BONE_COUNT] = {0};
    Matrix modelPose[BONE_COUNT] = {0};

    // This is a real AnimationTree clip containing three channels:
    // body sway plus independently animated shoulder rotations.
    const float times[] = {0, 1, 2};

    const Vector3 rootPositions[] = {
        {0, 0, 0},
        {0.10f, 0, 0},
        {0, 0, 0}
    };

    const Quaternion rightRotations[] = {
        {0, 0, 0, 1},
        {0, 0, 0, 1},
        {0, 0, 0, 1}
    };

    const Quaternion leftRotations[] = {
        {0, 0, 0, 1},
        {0, 0, 0, 1},
        {0, 0, 0, 1}
    };

    // Build genuine keyframe rotations while keeping all storage
    // alive for the lifetime of the animation tree.
    Quaternion rightKeys[3] = {
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, -0.16f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, 0.16f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, -0.16f)
    };

    Quaternion leftKeys[3] = {
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, 0.16f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, -0.16f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, 0.16f)
    };

    R3D_AnimationChannel channels[3] = {
        {
            .boneIndex = ROOT,
            .translation = {.times = times, .values = rootPositions, .count = 3}
        },
        {
            .boneIndex = RIGHT_SHOULDER,
            .rotation = {.times = times, .values = rightKeys, .count = 3}
        },
        {
            .boneIndex = LEFT_SHOULDER,
            .rotation = {.times = times, .values = leftKeys, .count = 3}
        }
    };

    R3D_Animation animation = {
        .name = "weapon_idle",
        .channels = channels,
        .channelCount = 3,
        .boneCount = BONE_COUNT,
        .duration = 2.0f,
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
        .name = "weapon_idle",
        .state = {
            .play = true,
            .loop = true,
            .speed = 1.0f
        }
    };

    R3D_AnimationTreeNode* node = R3D_CreateAnimationNode(&tree, nodeParams);

    if (!node)
    {
        TraceLog(LOG_ERROR, "Failed to create weapon animation node");
        R3D_UnloadAnimationTree(tree);
        R3D_UnloadMesh(cube);
        R3D_UnloadMesh(sphere);
        R3D_UnloadMesh(ground);
        R3D_Close();
        CloseWindow();
        return 1;
    }

    R3D_AddRootAnimationNode(&tree, node);

    Camera3D camera = {
        .position = {0.2f, 2.1f, 5.5f},
        .target = {0, 1.0f, 0},
        .up = {0, 1, 0},
        .fovy = 60,
        .projection = CAMERA_PERSPECTIVE
    };

    Matrix characterWorld = MatrixTranslate(-0.15f, 0, 0);
    Matrix inverseCharacter = MatrixInvert(characterWorld);

    Vector3 weaponPosition = {0, 1.15f, 0.70f};

    // Local weapon-space grip positions.
    const Vector3 primaryGrip = {0.35f, -0.12f, 0};
    const Vector3 supportGrip = {-0.35f, -0.08f, 0};

    float weaponTime = 0;
    float manualYaw = 0;
    float manualPitch = 0;
    float weight = 1;

    bool automaticAim = true;
    bool animationPaused = false;
    bool rightEnabled = true;
    bool leftEnabled = true;
    bool showAuthored = true;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        float movement = dt * (IsKeyDown(KEY_LEFT_SHIFT) ? 2.5f : 1.0f);

        if (IsKeyPressed(KEY_SPACE)) automaticAim = !automaticAim;
        if (IsKeyPressed(KEY_P)) animationPaused = !animationPaused;
        if (IsKeyPressed(KEY_ONE)) rightEnabled = !rightEnabled;
        if (IsKeyPressed(KEY_TWO)) leftEnabled = !leftEnabled;
        if (IsKeyPressed(KEY_H)) showAuthored = !showAuthored;

        if (IsKeyDown(KEY_A)) weaponPosition.x -= movement;
        if (IsKeyDown(KEY_D)) weaponPosition.x += movement;
        if (IsKeyDown(KEY_Q)) weaponPosition.y += movement;
        if (IsKeyDown(KEY_E)) weaponPosition.y -= movement;
        if (IsKeyDown(KEY_W)) weaponPosition.z -= movement;
        if (IsKeyDown(KEY_S)) weaponPosition.z += movement;

        if (IsKeyDown(KEY_LEFT)) manualYaw -= dt;
        if (IsKeyDown(KEY_RIGHT)) manualYaw += dt;
        if (IsKeyDown(KEY_UP)) manualPitch += dt;
        if (IsKeyDown(KEY_DOWN)) manualPitch -= dt;

        manualYaw = Clamp(manualYaw, -1.2f, 1.2f);
        manualPitch = Clamp(manualPitch, -0.8f, 0.8f);

        if (IsKeyDown(KEY_MINUS)) weight -= dt;
        if (IsKeyDown(KEY_EQUAL)) weight += dt;

        weight = Clamp(weight, 0, 1);

        if (IsKeyPressed(KEY_R))
        {
            weaponPosition = (Vector3){0, 1.15f, 0.70f};
            weaponTime = 0;
            manualYaw = 0;
            manualPitch = 0;
            weight = 1;
            automaticAim = true;
            animationPaused = false;
            rightEnabled = true;
            leftEnabled = true;
            showAuthored = true;
        }

        if (automaticAim) weaponTime += dt;

        float yaw = automaticAim ? sinf(weaponTime * 0.8f) * 0.42f : manualYaw;
        float pitch = automaticAim ? sinf(weaponTime * 0.6f) * 0.16f : manualPitch;

        Matrix weaponWorld = MatrixMultiply(
            MatrixMultiply(MatrixRotateZ(pitch), MatrixRotateY(yaw)),
            MatrixTranslate(weaponPosition.x, weaponPosition.y, weaponPosition.z)
        );

        // A world-space weapon owns both grip targets.
        Vector3 primaryGripWorld = Vector3Transform(primaryGrip, weaponWorld);
        Vector3 supportGripWorld = Vector3Transform(supportGrip, weaponWorld);

        Vector3 primaryTargetModel = Vector3Transform(primaryGripWorld, inverseCharacter);
        Vector3 supportTargetModel = Vector3Transform(supportGripWorld, inverseCharacter);

        // ========================================
        // STAGE 1: SAMPLE ANIMATION TREE
        // ========================================

        bool poseValid =
            R3D_AdvanceAnimationTree(&tree, animationPaused ? 0 : dt) &&
            R3D_ComputeAnimationTreeLocalPose(&tree) &&
            R3D_ComputeAnimationTreeModelPose(&tree);

        Vector3 authoredRight[3] = {0};
        Vector3 authoredLeft[3] = {0};

        if (poseValid)
        {
            authoredRight[0] = bone_world(&tree.player, RIGHT_SHOULDER, characterWorld);
            authoredRight[1] = bone_world(&tree.player, RIGHT_ELBOW, characterWorld);
            authoredRight[2] = bone_world(&tree.player, RIGHT_HAND, characterWorld);

            authoredLeft[0] = bone_world(&tree.player, LEFT_SHOULDER, characterWorld);
            authoredLeft[1] = bone_world(&tree.player, LEFT_ELBOW, characterWorld);
            authoredLeft[2] = bone_world(&tree.player, LEFT_HAND, characterWorld);
        }

        // Model-space pole targets are placed outward from each shoulder.
        Vector3 rightPoleModel = {0};
        Vector3 leftPoleModel = {0};

        if (poseValid)
        {
            rightPoleModel = Vector3Add(
                position(modelPose[RIGHT_SHOULDER]),
                (Vector3){1.0f, 0, 1.25f}
            );

            leftPoleModel = Vector3Add(
                position(modelPose[LEFT_SHOULDER]),
                (Vector3){-1.0f, 0, 1.25f}
            );
        }

        // ========================================
        // STAGE 2: PRIMARY HAND IK
        // ========================================

        bool rightSuccess = false;

        if (poseValid && rightEnabled)
        {
            R3D_TwoBoneIKParams params = {
                .targetModel = primaryTargetModel,
                .poleModel = rightPoleModel,
                .weight = weight
            };

            rightSuccess =
                R3D_ApplyAnimationTwoBoneIK(
                    &tree.player, RIGHT_SHOULDER, RIGHT_ELBOW, RIGHT_HAND, params
                ) &&
                R3D_RecomputeAnimationModelPoseFromBone(&tree.player, RIGHT_SHOULDER);
        }

        // ========================================
        // STAGE 3: SUPPORTING HAND IK
        // ========================================
        //
        // Separate subtree, same animation player.
        // Solving this hand must not overwrite the primary arm.

        bool leftSuccess = false;

        if (poseValid && leftEnabled)
        {
            R3D_TwoBoneIKParams params = {
                .targetModel = supportTargetModel,
                .poleModel = leftPoleModel,
                .weight = weight
            };

            leftSuccess =
                R3D_ApplyAnimationTwoBoneIK(
                    &tree.player, LEFT_SHOULDER, LEFT_ELBOW, LEFT_HAND, params
                ) &&
                R3D_RecomputeAnimationModelPoseFromBone(&tree.player, LEFT_SHOULDER);
        }

        // ========================================
        // STAGE 4: FINAL POSE
        // ========================================

        Vector3 rightShoulder = poseValid ? bone_world(&tree.player, RIGHT_SHOULDER, characterWorld) : Vector3Zero();
        Vector3 rightElbow = poseValid ? bone_world(&tree.player, RIGHT_ELBOW, characterWorld) : Vector3Zero();
        Vector3 rightHand = poseValid ? bone_world(&tree.player, RIGHT_HAND, characterWorld) : Vector3Zero();

        Vector3 leftShoulder = poseValid ? bone_world(&tree.player, LEFT_SHOULDER, characterWorld) : Vector3Zero();
        Vector3 leftElbow = poseValid ? bone_world(&tree.player, LEFT_ELBOW, characterWorld) : Vector3Zero();
        Vector3 leftHand = poseValid ? bone_world(&tree.player, LEFT_HAND, characterWorld) : Vector3Zero();

        float rightError = Vector3Distance(rightHand, primaryGripWorld);
        float leftError = Vector3Distance(leftHand, supportGripWorld);

        // Weapon geometry in its own world transform.
        Vector3 stock = Vector3Transform((Vector3){-0.65f, 0, 0}, weaponWorld);
        Vector3 muzzle = Vector3Transform((Vector3){1.10f, 0, 0}, weaponWorld);

        Vector3 primaryGripEnd = Vector3Transform(
            Vector3Add(primaryGrip, (Vector3){0, -0.25f, 0}), weaponWorld
        );

        Vector3 supportGripEnd = Vector3Transform(
            Vector3Add(supportGrip, (Vector3){0, -0.25f, 0}), weaponWorld
        );

        Vector3 rightPoleWorld = Vector3Transform(rightPoleModel, characterWorld);
        Vector3 leftPoleWorld = Vector3Transform(leftPoleModel, characterWorld);

        Vector3 bodyOrigin = poseValid
            ? bone_world(&tree.player, ROOT, characterWorld)
            : Vector3Zero();

        BeginDrawing();
            ClearBackground(BLACK);

            R3D_Begin(camera);
                R3D_PushLight(light);
                R3D_DrawMesh(ground, floorMaterial, (Vector3){0, -0.45f, 0}, 1);

                if (poseValid)
                {
                    // Body and head: purely visual geometry.
                    draw_segment(
                        cube, bodyMaterial,
                        Vector3Add(bodyOrigin, (Vector3){0, 0.35f, 0}),
                        Vector3Add(bodyOrigin, (Vector3){0, 1.40f, 0}),
                        0.55f
                    );

                    R3D_DrawMesh(
                        sphere, bodyMaterial,
                        Vector3Add(bodyOrigin, (Vector3){0, 1.68f, 0}), 0.25f
                    );

                    if (showAuthored)
                    {
                        draw_arm(cube, sphere, authoredMaterial,
                                 authoredRight[0], authoredRight[1], authoredRight[2], 0.055f);

                        draw_arm(cube, sphere, authoredMaterial,
                                 authoredLeft[0], authoredLeft[1], authoredLeft[2], 0.055f);
                    }

                    draw_arm(cube, sphere, rightMaterial,
                             rightShoulder, rightElbow, rightHand, 0.13f);

                    draw_arm(cube, sphere, leftMaterial,
                             leftShoulder, leftElbow, leftHand, 0.13f);
                }

                // The weapon is driven independently of the skeleton.
                draw_segment(cube, weaponMaterial, stock, muzzle, 0.12f);
                draw_segment(cube, primaryMaterial, primaryGripWorld, primaryGripEnd, 0.09f);
                draw_segment(cube, supportMaterial, supportGripWorld, supportGripEnd, 0.09f);

                R3D_DrawMesh(sphere, primaryMaterial, primaryGripWorld, 0.105f);
                R3D_DrawMesh(sphere, supportMaterial, supportGripWorld, 0.105f);

                if (poseValid)
                {
                    R3D_DrawMesh(sphere, poleMaterial, rightPoleWorld, 0.075f);
                    R3D_DrawMesh(sphere, poleMaterial, leftPoleWorld, 0.075f);
                }
            R3D_End();

            DrawRectangle(8, 8, 610, 206, Fade(BLACK, 0.72f));

            DrawText("IK - TWO HANDED WEAPON GRIP", 18, 15, 22, RAYWHITE);

            DrawText(
                TextFormat("Primary hand: %s | Error: %.4f",
                           rightEnabled ? (rightSuccess ? "IK" : "ERROR") : "ANIMATED",
                           rightError),
                18, 45, 17, rightError < 0.01f ? GREEN : ORANGE
            );

            DrawText(
                TextFormat("Support hand: %s | Error: %.4f",
                           leftEnabled ? (leftSuccess ? "IK" : "ERROR") : "ANIMATED",
                           leftError),
                18, 68, 17, leftError < 0.01f ? GREEN : ORANGE
            );

            DrawText(TextFormat("Weight: %.2f | Weapon: %s | Animation: %s",
                               weight, automaticAim ? "AUTO" : "MANUAL",
                               animationPaused ? "PAUSED" : "PLAYING"),
                     18, 92, 16, LIGHTGRAY);

            DrawText("WASD / Q E  Move weapon    ARROWS  Yaw / Pitch", 18, 117, 16, LIGHTGRAY);
            DrawText("SPACE Auto aim    1/2 Toggle hands    -/= Weight", 18, 140, 16, LIGHTGRAY);
            DrawText("P Pause animation    H Show authored pose    R Reset", 18, 163, 16, LIGHTGRAY);

            if (!poseValid)
            {
                DrawText("ANIMATION TREE ERROR", 18, 186, 16, RED);
            }
            else if ((rightEnabled && !rightSuccess) || (leftEnabled && !leftSuccess))
            {
                DrawText("IK SOLVER ERROR", 18, 186, 16, RED);
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