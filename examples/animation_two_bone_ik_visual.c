#include <r3d/r3d.h>
#include <r3d/r3d_animation_ik.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

static Vector3 matrix_position(Matrix matrix)
{
    return (Vector3){matrix.m12, matrix.m13, matrix.m14};
}

static Quaternion segment_rotation(Vector3 direction)
{
    const Vector3 up = {0.0f, 1.0f, 0.0f};

    direction = Vector3Normalize(direction);

    float dot = Vector3DotProduct(up, direction);

    if (dot > 0.999999f) return QuaternionIdentity();

    if (dot < -0.999999f)
    {
        return QuaternionFromAxisAngle((Vector3){1, 0, 0}, PI);
    }

    return QuaternionFromVector3ToVector3(up, direction);
}

static void draw_segment(R3D_Mesh mesh, R3D_Material material, Vector3 start, Vector3 end, float thickness)
{
    Vector3 difference = Vector3Subtract(end, start);
    float length = Vector3Length(difference);

    if (length <= 0.0001f) return;

    Vector3 center = Vector3Scale(Vector3Add(start, end), 0.5f);
    Quaternion rotation = segment_rotation(difference);

    R3D_DrawMeshEx(mesh, material, center, rotation,
                   (Vector3){thickness, length, thickness});
}

static void draw_joint(R3D_Mesh mesh, R3D_Material material, Vector3 position, float radius)
{
    R3D_DrawMesh(mesh, material, position, radius);
}

static void draw_arm(R3D_Mesh cube, R3D_Mesh sphere, R3D_Material material,
                     Vector3 shoulder, Vector3 elbow, Vector3 wrist, float thickness)
{
    draw_segment(cube, material, shoulder, elbow, thickness);
    draw_segment(cube, material, elbow, wrist, thickness);

    draw_joint(sphere, material, shoulder, thickness * 0.85f);
    draw_joint(sphere, material, elbow, thickness * 0.85f);
    draw_joint(sphere, material, wrist, thickness * 0.85f);
}

static R3D_Material colored_material(Color color)
{
    R3D_Material material = R3D_GetDefaultMaterial();
    material.albedo.color = color;
    return material;
}

int main(void)
{
    InitWindow(1152, 648, "[r3d] - Two Bone IK Visual");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());
    R3D_SetAntiAliasingMode(R3D_ANTI_ALIASING_MODE_FXAA);

    R3D_ENVIRONMENT_SET(ambient.color, (Color){90, 90, 90, 255});

    R3D_Light light = R3D_CreateDirLight(
        (Vector3){-1.0f, -2.0f, -1.0f},
        WHITE,
        1.5f
    );

    R3D_Mesh cube = R3D_GenMeshCube(1.0f, 1.0f, 1.0f);
    R3D_Mesh sphere = R3D_GenMeshSphere(1.0f, 20, 20);
    R3D_Mesh ground = R3D_GenMeshPlane(8.0f, 8.0f, 1, 1);

    R3D_Material authoredMaterial = colored_material((Color){90, 90, 100, 255});
    R3D_Material ikMaterial = colored_material((Color){40, 175, 245, 255});
    R3D_Material targetMaterial = colored_material(YELLOW);
    R3D_Material poleMaterial = colored_material((Color){255, 50, 180, 255});
    R3D_Material groundMaterial = colored_material((Color){65, 70, 75, 255});

    // Deliberately unordered hierarchy:
    //
    // Root (3)
    //   Shoulder (1)
    //     Elbow (2)
    //       Wrist (0)

    R3D_BoneInfo bones[4] = {
        {.name = "Wrist", .parent = 2},
        {.name = "Shoulder", .parent = 3},
        {.name = "Elbow", .parent = 1},
        {.name = "Root", .parent = -1}
    };

    Matrix localPose[4] = {
        MatrixTranslate(1.0f, 0.0f, 0.0f),
        MatrixIdentity(),
        MatrixTranslate(1.0f, 0.0f, 0.0f),
        MatrixTranslate(-1.0f, 0.25f, 0.0f)
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
            .rootBind = MatrixIdentity()
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1
    };

    Vector3 target = {0.3f, 1.0f, 0.0f};
    Vector3 pole = {-1.0f, 1.4f, 1.2f};

    float weight = 1.0f;
    float animationTime = 0.0f;

    bool ikEnabled = true;
    bool animationPaused = false;
    bool nonuniformParent = false;
    bool showAuthored = true;

    Camera3D camera = {
        .position = {1.4f, 1.7f, 6.0f},
        .target = {0.0f, 0.7f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 60.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        float speed = IsKeyDown(KEY_LEFT_SHIFT) ? 3.0f : 1.0f;
        float movement = dt * speed;

        // Target controls.
        if (IsKeyDown(KEY_W)) target.y += movement;
        if (IsKeyDown(KEY_S)) target.y -= movement;
        if (IsKeyDown(KEY_A)) target.x -= movement;
        if (IsKeyDown(KEY_D)) target.x += movement;
        if (IsKeyDown(KEY_Q)) target.z += movement;
        if (IsKeyDown(KEY_E)) target.z -= movement;

        // Pole controls.
        if (IsKeyDown(KEY_I)) pole.y += movement;
        if (IsKeyDown(KEY_K)) pole.y -= movement;
        if (IsKeyDown(KEY_J)) pole.x -= movement;
        if (IsKeyDown(KEY_L)) pole.x += movement;
        if (IsKeyDown(KEY_U)) pole.z += movement;
        if (IsKeyDown(KEY_O)) pole.z -= movement;

        if (IsKeyPressed(KEY_SPACE)) ikEnabled = !ikEnabled;
        if (IsKeyPressed(KEY_P)) animationPaused = !animationPaused;
        if (IsKeyPressed(KEY_N)) nonuniformParent = !nonuniformParent;
        if (IsKeyPressed(KEY_H)) showAuthored = !showAuthored;

        if (IsKeyDown(KEY_MINUS)) weight -= dt;
        if (IsKeyDown(KEY_EQUAL)) weight += dt;

        weight = Clamp(weight, 0.0f, 1.0f);

        if (IsKeyPressed(KEY_R))
        {
            target = (Vector3){0.3f, 1.0f, 0.0f};
            pole = (Vector3){-1.0f, 1.4f, 1.2f};
            weight = 1.0f;
            ikEnabled = true;
            animationPaused = false;
            nonuniformParent = false;
            showAuthored = true;
            animationTime = 0.0f;
        }

        if (!animationPaused) animationTime += dt;

        // ========================================
        // STAGE 1: AUTHORED ANIMATION
        // ========================================
        //
        // Simulate a moving shoulder animation.
        // Every frame starts from a fresh authored pose.

        Transform root = {
            .translation = {-1.0f, 0.25f, 0.0f},
            .rotation = QuaternionIdentity(),
            .scale = nonuniformParent
                ? (Vector3){1.25f, 0.85f, 1.1f}
                : (Vector3){1.0f, 1.0f, 1.0f}
        };

        Transform shoulderAnimation = {
            .translation = {0.0f, 0.0f, 0.0f},
            .rotation = QuaternionFromAxisAngle(
                (Vector3){0, 0, 1},
                sinf(animationTime * 1.75f) * 0.28f
            ),
            .scale = {1.0f, 1.0f, 1.0f}
        };

        R3D_SetAnimationBoneLocalTransform(&player, 3, root);
        R3D_SetAnimationBoneLocalTransform(&player, 1, shoulderAnimation);
        R3D_SetAnimationBoneLocalMatrix(&player, 2, MatrixTranslate(1, 0, 0));
        R3D_SetAnimationBoneLocalMatrix(&player, 0, MatrixTranslate(1, 0, 0));

        // ========================================
        // STAGE 2: INITIAL MODEL POSE
        // ========================================

        bool poseValid = R3D_RecomputeAnimationModelPose(&player);

        Vector3 authoredShoulder = {0};
        Vector3 authoredElbow = {0};
        Vector3 authoredWrist = {0};

        if (poseValid)
        {
            authoredShoulder = matrix_position(modelPose[1]);
            authoredElbow = matrix_position(modelPose[2]);
            authoredWrist = matrix_position(modelPose[0]);
        }

        // ========================================
        // STAGE 3: TWO-BONE IK
        // ========================================

        bool ikSuccess = false;

        if (poseValid && ikEnabled)
        {
            R3D_TwoBoneIKParams params = {
                .targetModel = target,
                .poleModel = pole,
                .weight = weight
            };

            ikSuccess = R3D_ApplyAnimationTwoBoneIK(&player, 1, 2, 0, params);

            if (ikSuccess)
            {
                ikSuccess = R3D_RecomputeAnimationModelPoseFromBone(&player, 1);
            }
        }

        // ========================================
        // STAGE 4: FINAL POSE
        // ========================================

        Vector3 shoulder = poseValid ? matrix_position(modelPose[1]) : Vector3Zero();
        Vector3 elbow = poseValid ? matrix_position(modelPose[2]) : Vector3Zero();
        Vector3 wrist = poseValid ? matrix_position(modelPose[0]) : Vector3Zero();

        float error = Vector3Distance(wrist, target);
        float reach = Vector3Distance(shoulder, elbow) + Vector3Distance(elbow, wrist);
        float targetDistance = Vector3Distance(shoulder, target);

        BeginDrawing();
            ClearBackground(BLACK);

            R3D_Begin(camera);
                R3D_PushLight(light);

                R3D_DrawMesh(ground, groundMaterial, (Vector3){0, -1, 0}, 1.0f);

                if (poseValid)
                {
                    // Original animation shown as a narrow grey skeleton.
                    if (showAuthored)
                    {
                        draw_arm(
                            cube, sphere, authoredMaterial,
                            authoredShoulder, authoredElbow, authoredWrist,
                            0.075f
                        );
                    }

                    // Final IK result.
                    draw_arm(
                        cube, sphere, ikMaterial,
                        shoulder, elbow, wrist,
                        0.16f
                    );
                }

                // Target and pole are separate independent markers.
                draw_joint(sphere, targetMaterial, target, 0.14f);
                draw_joint(sphere, poleMaterial, pole, 0.12f);
            R3D_End();

            DrawRectangle(8, 8, 530, 183, Fade(BLACK, 0.65f));

            DrawText("TWO-BONE IK", 18, 16, 22, RAYWHITE);

            DrawText(
                TextFormat("IK: %s  |  Weight: %.2f  |  Animation: %s",
                    ikEnabled ? "ON" : "OFF",
                    weight,
                    animationPaused ? "PAUSED" : "PLAYING"),
                18, 47, 17, LIGHTGRAY
            );

            DrawText(
                TextFormat("Wrist error: %.3f  |  Reach: %.3f  |  Target distance: %.3f",
                    error, reach, targetDistance),
                18, 70, 16, ikSuccess || !ikEnabled ? GREEN : ORANGE
            );

            DrawText("WASD / Q E  Target XYZ    IJKL / U O  Pole XYZ", 18, 97, 16, LIGHTGRAY);
            DrawText("SPACE  IK toggle     - / =  Weight     SHIFT  Faster", 18, 120, 16, LIGHTGRAY);
            DrawText("P  Pause    N  Nonuniform scale    H  Authored    R  Reset", 18, 143, 16, LIGHTGRAY);

            if (!poseValid || (ikEnabled && !ikSuccess))
            {
                DrawText("IK / POSE ERROR", 18, 167, 16, RED);
            }
            else if (targetDistance > reach + 0.001f)
            {
                DrawText("TARGET OUT OF REACH", 18, 167, 16, YELLOW);
            }

            DrawFPS(GetScreenWidth() - 100, 12);
        EndDrawing();
    }

    R3D_UnloadMesh(cube);
    R3D_UnloadMesh(sphere);
    R3D_UnloadMesh(ground);

    R3D_Close();
    CloseWindow();

    return 0;
}