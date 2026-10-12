#include <r3d/r3d.h>
#include <r3d/r3d_animation_tree.h>
#include <r3d/r3d_animation_ik.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

enum {
    HAND = 0,
    SHOULDER = 1,
    ELBOW = 2,
    ROOT = 3,
    BONE_COUNT = 4
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

static Quaternion segment_rotation(Vector3 direction)
{
    Vector3 up = {0, 1, 0};
    direction = Vector3Normalize(direction);

    float dot = Vector3DotProduct(up, direction);

    if (dot > 0.999999f) return QuaternionIdentity();
    if (dot < -0.999999f) return QuaternionFromAxisAngle((Vector3){1, 0, 0}, PI);

    return QuaternionFromVector3ToVector3(up, direction);
}

static void draw_segment(R3D_Mesh cube, R3D_Material material, Vector3 from, Vector3 to, float thickness)
{
    Vector3 delta = Vector3Subtract(to, from);
    float length = Vector3Length(delta);

    if (length < 1e-5f) return;

    Vector3 y = Vector3Scale(delta, 1.0f / length);

    // Explicit reference for rotation around the segment axis.
    Vector3 z = {0.0f, 0.0f, 1.0f};

    z = Vector3Subtract(z, Vector3Scale(y, Vector3DotProduct(z, y)));

    if (Vector3LengthSqr(z) < 1e-8f)
    {
        z = (Vector3){1.0f, 0.0f, 0.0f};
        z = Vector3Subtract(z, Vector3Scale(y, Vector3DotProduct(z, y)));
    }

    z = Vector3Normalize(z);

    Vector3 x = Vector3Normalize(Vector3CrossProduct(y, z));
    z = Vector3CrossProduct(x, y);

    Vector3 center = Vector3Scale(Vector3Add(from, to), 0.5f);

    Matrix transform = {
        x.x * thickness, y.x * length, z.x * thickness, center.x,
        x.y * thickness, y.y * length, z.y * thickness, center.y,
        x.z * thickness, y.z * length, z.z * thickness, center.z,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    R3D_DrawMeshPro(cube, material, transform);
}

static void draw_arm(R3D_Mesh cube, R3D_Mesh sphere, R3D_Material material,
                     Vector3 shoulder, Vector3 elbow, Vector3 wrist, float thickness)
{
    draw_segment(cube, material, shoulder, elbow, thickness);
    draw_segment(cube, material, elbow, wrist, thickness);

    R3D_DrawMesh(sphere, material, shoulder, thickness * 0.8f);
    R3D_DrawMesh(sphere, material, elbow, thickness * 0.8f);
    R3D_DrawMesh(sphere, material, wrist, thickness * 0.8f);
}

int main(void)
{
    InitWindow(1152, 648, "[r3d] - IK Hand to Handle");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());
    R3D_SetAntiAliasingMode(R3D_ANTI_ALIASING_MODE_FXAA);
    R3D_ENVIRONMENT_SET(ambient.color, (Color){85, 85, 85, 255});

    R3D_Light light = R3D_CreateDirLight((Vector3){-1, -2, -1}, WHITE, 1.5f);

    R3D_Mesh cube = R3D_GenMeshCube(1, 1, 1);
    R3D_Mesh sphere = R3D_GenMeshSphere(1, 20, 20);
    R3D_Mesh ground = R3D_GenMeshPlane(8, 8, 1, 1);

    R3D_Material armMaterial = colored_material((Color){40, 175, 245, 255});
    R3D_Material originalMaterial = colored_material((Color){95, 95, 105, 255});
    R3D_Material leverMaterial = colored_material((Color){245, 120, 35, 255});
    R3D_Material gripMaterial = colored_material((Color){250, 215, 45, 255});
    R3D_Material poleMaterial = colored_material((Color){250, 60, 170, 255});
    R3D_Material floorMaterial = colored_material((Color){65, 70, 78, 255});

    // Unordered skeleton:
    // Root (3) -> Shoulder (1) -> Elbow (2) -> Hand (0).
    R3D_BoneInfo bones[BONE_COUNT] = {
        {.name = "Hand", .parent = ELBOW},
        {.name = "Shoulder", .parent = ROOT},
        {.name = "Elbow", .parent = SHOULDER},
        {.name = "Root", .parent = -1}
    };

    Matrix localBind[BONE_COUNT] = {
        MatrixTranslate(1, 0, 0),
        MatrixTranslate(-1.0f, 1.25f, 0),
        MatrixTranslate(1, 0, 0),
        MatrixIdentity()
    };

    Matrix modelBind[BONE_COUNT] = {
        MatrixTranslate(1.0f, 1.25f, 0),
        MatrixTranslate(-1.0f, 1.25f, 0),
        MatrixTranslate(0.0f, 1.25f, 0),
        MatrixIdentity()
    };

    Matrix localPose[BONE_COUNT] = {0};
    Matrix modelPose[BONE_COUNT] = {0};

    // A genuine AnimationTree clip: the character root sways
    // while the arm keeps its bind-pose local offsets.
    const float times[] = {0, 1, 2};
    const Vector3 sway[] = {
        {0, 0, 0},
        {0.16f, 0, 0},
        {0, 0, 0}
    };

    R3D_AnimationChannel channel = {
        .boneIndex = ROOT,
        .translation = {
            .times = times,
            .values = sway,
            .count = 3
        }
    };

    R3D_Animation animation = {
        .name = "idle_sway",
        .channels = &channel,
        .channelCount = 1,
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
        .name = "idle_sway",
        .state = {
            .play = true,
            .loop = true,
            .speed = 1.0f
        }
    };

    R3D_AnimationTreeNode* node = R3D_CreateAnimationNode(&tree, nodeParams);

    if (!node)
    {
        TraceLog(LOG_ERROR, "Failed to create animation node");
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
        .position = {1.0f, 2.2f, 5.5f},
        .target = {0, 0.85f, 0},
        .up = {0, 1, 0},
        .fovy = 60,
        .projection = CAMERA_PERSPECTIVE
    };

    // Non-identity character transform demonstrates world-to-model
    // conversion for the external handle position.
    Matrix characterWorld = MatrixTranslate(0.25f, 0, 0);
    Matrix inverseCharacter = MatrixInvert(characterWorld);

    const Vector3 leverPivot = {0.38f, 0.15f, 0};
    const float leverLength = 0.95f;

    float leverTime = 0;
    float manualAngle = 0;
    float weight = 1;
    float poleZ = 1.5f;

    bool automaticLever = true;
    bool animationPaused = false;
    bool ikEnabled = true;
    bool showAuthored = true;

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        if (IsKeyPressed(KEY_SPACE)) automaticLever = !automaticLever;
        if (IsKeyPressed(KEY_P)) animationPaused = !animationPaused;
        if (IsKeyPressed(KEY_G)) ikEnabled = !ikEnabled;
        if (IsKeyPressed(KEY_H)) showAuthored = !showAuthored;

        if (IsKeyDown(KEY_LEFT)) manualAngle -= dt;
        if (IsKeyDown(KEY_RIGHT)) manualAngle += dt;
        if (IsKeyDown(KEY_Q)) poleZ += dt;
        if (IsKeyDown(KEY_E)) poleZ -= dt;
        if (IsKeyDown(KEY_MINUS)) weight -= dt;
        if (IsKeyDown(KEY_EQUAL)) weight += dt;

        manualAngle = Clamp(manualAngle, -1.0f, 1.0f);
        poleZ = Clamp(poleZ, -3.0f, 3.0f);
        weight = Clamp(weight, 0.0f, 1.0f);

        if (IsKeyPressed(KEY_R))
        {
            leverTime = 0;
            manualAngle = 0;
            poleZ = 1.5f;
            weight = 1;
            automaticLever = true;
            animationPaused = false;
            ikEnabled = true;
            showAuthored = true;
        }

        if (automaticLever) leverTime += dt;

        float leverAngle = automaticLever
            ? sinf(leverTime * 0.9f) * 0.55f
            : manualAngle;

        Matrix leverWorld = MatrixMultiply(
            MatrixRotateZ(leverAngle),
            MatrixTranslate(leverPivot.x, leverPivot.y, leverPivot.z)
        );

        // The handle belongs to an independently moving object.
        Vector3 handleWorld = Vector3Transform(
            (Vector3){0, leverLength, 0}, leverWorld
        );

        // The IK API expects a model-space target.
        Vector3 targetModel = Vector3Transform(handleWorld, inverseCharacter);

        // ========================================
        // STAGE 1: ANIMATION TREE
        // ========================================

        bool poseValid =
            R3D_AdvanceAnimationTree(&tree, animationPaused ? 0 : dt) &&
            R3D_ComputeAnimationTreeLocalPose(&tree) &&
            R3D_ComputeAnimationTreeModelPose(&tree);

        Vector3 authoredShoulder = {0};
        Vector3 authoredElbow = {0};
        Vector3 authoredHand = {0};

        if (poseValid)
        {
            authoredShoulder = Vector3Transform(position(modelPose[SHOULDER]), characterWorld);
            authoredElbow = Vector3Transform(position(modelPose[ELBOW]), characterWorld);
            authoredHand = Vector3Transform(position(modelPose[HAND]), characterWorld);
        }

        // ========================================
        // STAGE 2: HANDLE IK
        // ========================================

        Vector3 poleModel = poseValid
            ? Vector3Add(position(modelPose[SHOULDER]), (Vector3){0, 0, poleZ})
            : Vector3Zero();

        bool ikSuccess = false;

        if (poseValid && ikEnabled)
        {
            R3D_TwoBoneIKParams params = {
                .targetModel = targetModel,
                .poleModel = poleModel,
                .weight = weight
            };

            ikSuccess =
                R3D_ApplyAnimationTwoBoneIK(&tree.player, SHOULDER, ELBOW, HAND, params) &&
                R3D_RecomputeAnimationModelPoseFromBone(&tree.player, SHOULDER);
        }

        // ========================================
        // STAGE 3: FINAL POSE
        // ========================================

        Vector3 shoulder = poseValid ? Vector3Transform(position(modelPose[SHOULDER]), characterWorld) : Vector3Zero();
        Vector3 elbow = poseValid ? Vector3Transform(position(modelPose[ELBOW]), characterWorld) : Vector3Zero();
        Vector3 hand = poseValid ? Vector3Transform(position(modelPose[HAND]), characterWorld) : Vector3Zero();
        Vector3 poleWorld = Vector3Transform(poleModel, characterWorld);

        float error = Vector3Distance(hand, handleWorld);
        float reach = Vector3Distance(shoulder, elbow) + Vector3Distance(elbow, hand);

        BeginDrawing();
            ClearBackground(BLACK);

            R3D_Begin(camera);
                R3D_PushLight(light);
                R3D_DrawMesh(ground, floorMaterial, (Vector3){0, -0.45f, 0}, 1);

                // The lever and its handle are not skeleton bones.
                R3D_DrawMeshEx(cube, leverMaterial, (Vector3){leverPivot.x, -0.02f, leverPivot.z},
                               QuaternionIdentity(), (Vector3){0.45f, 0.3f, 0.4f});

                draw_segment(cube, leverMaterial, leverPivot, handleWorld, 0.09f);
                R3D_DrawMesh(sphere, gripMaterial, handleWorld, 0.13f);

                if (poseValid)
                {
                    if (showAuthored)
                    {
                        draw_arm(cube, sphere, originalMaterial,
                                 authoredShoulder, authoredElbow, authoredHand, 0.065f);
                    }

                    draw_arm(cube, sphere, armMaterial,
                             shoulder, elbow, hand, 0.14f);
                }

                R3D_DrawMesh(sphere, poleMaterial, poleWorld, 0.10f);
            R3D_End();

            DrawRectangle(8, 8, 560, 184, Fade(BLACK, 0.7f));
            DrawText("IK - HAND TO HANDLE", 18, 15, 22, RAYWHITE);

            DrawText(TextFormat("IK: %s | Weight: %.2f | Animation: %s",
                               ikEnabled ? "ON" : "OFF", weight,
                               animationPaused ? "PAUSED" : "PLAYING"),
                     18, 46, 17, LIGHTGRAY);

            DrawText(TextFormat("Hand to handle error: %.4f | Reach: %.2f", error, reach),
                     18, 72, 17, error < 0.01f ? GREEN : ORANGE);

            DrawText("SPACE Auto lever   LEFT/RIGHT Manual angle   P Pause animation", 18, 100, 15, LIGHTGRAY);
            DrawText("G IK toggle   H Authored pose   Q/E Pole   -/= Weight", 18, 125, 15, LIGHTGRAY);
            DrawText("R Reset", 18, 150, 15, LIGHTGRAY);

            if (!poseValid || (ikEnabled && !ikSuccess))
            {
                DrawText("IK / POSE ERROR", 18, 170, 16, RED);
            }
            else if (Vector3Distance(shoulder, handleWorld) > reach + 0.001f)
            {
                DrawText("HANDLE OUT OF REACH", 18, 170, 16, YELLOW);
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