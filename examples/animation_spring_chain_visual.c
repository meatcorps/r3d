#include <r3d/r3d.h>
#include <r3d/r3d_animation_spring_chain.h>
#include <r3d/r3d_animation_tree.h>
#include <raymath.h>
#include <math.h>
#include <stdio.h>

#define SEGMENT_COUNT 8
#define BONE_COUNT (SEGMENT_COUNT + 2)

enum {
    TIP = 0,
    ROOT = 1
};

// Root -> 8 spring bones -> Tip.
// Deliberately unordered bone indices.
static const int path[SEGMENT_COUNT] = {
    8, 3, 9, 4, 6, 2, 7, 5
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

static void draw_segment(
    R3D_Mesh cube,
    R3D_Material material,
    Vector3 from,
    Vector3 to,
    float thickness)
{
    Vector3 delta = Vector3Subtract(to, from);
    float length = Vector3Length(delta);

    if (length < 1e-5f) return;

    Vector3 y = Vector3Scale(delta, 1.0f / length);

    // Stable segment orientation, as in our IK handle demo.
    Vector3 z = {0, 0, 1};

    z = Vector3Subtract(
        z, Vector3Scale(y, Vector3DotProduct(z, y))
    );

    if (Vector3LengthSqr(z) < 1e-8f)
    {
        z = (Vector3){1, 0, 0};

        z = Vector3Subtract(
            z, Vector3Scale(y, Vector3DotProduct(z, y))
        );
    }

    z = Vector3Normalize(z);

    Vector3 x = Vector3Normalize(Vector3CrossProduct(y, z));
    z = Vector3CrossProduct(x, y);

    Vector3 center = Vector3Scale(
        Vector3Add(from, to), 0.5f
    );

    Matrix transform = {
        x.x * thickness, y.x * length, z.x * thickness, center.x,
        x.y * thickness, y.y * length, z.y * thickness, center.y,
        x.z * thickness, y.z * length, z.z * thickness, center.z,
        0, 0, 0, 1
    };

    R3D_DrawMeshPro(cube, material, transform);
}

static void draw_tail(
    R3D_Mesh cube,
    R3D_Mesh sphere,
    R3D_Material firstMaterial,
    R3D_Material secondMaterial,
    const Vector3* joints,
    float thickness)
{
    for (int i = 0; i < SEGMENT_COUNT; i++)
    {
        R3D_Material material =
            i % 2 == 0 ? firstMaterial : secondMaterial;

        float taper = 1.0f - 0.65f * (
            (float)i / (float)SEGMENT_COUNT
        );

        draw_segment(
            cube,
            material,
            joints[i],
            joints[i + 1],
            thickness * taper
        );

        R3D_DrawMesh(
            sphere,
            material,
            joints[i],
            thickness * taper * 0.7f
        );
    }

    R3D_DrawMesh(
        sphere,
        secondMaterial,
        joints[SEGMENT_COUNT],
        thickness * 0.25f
    );
}

int main(void)
{
    InitWindow(1152, 648, "[r3d] - Spring Chain Visual");
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
    R3D_Mesh sphere = R3D_GenMeshSphere(1, 16, 16);
    R3D_Mesh ground = R3D_GenMeshPlane(10, 10, 1, 1);

    R3D_Material blue = colored_material(
        (Color){40, 165, 245, 255}
    );

    R3D_Material green = colored_material(
        (Color){45, 220, 140, 255}
    );

    R3D_Material authored = colored_material(
        (Color){95, 95, 105, 255}
    );

    R3D_Material base = colored_material(
        (Color){230, 130, 45, 255}
    );

    R3D_Material floorMaterial = colored_material(
        (Color){65, 70, 78, 255}
    );

    // ========================================
    // SKELETON
    // ========================================

    R3D_BoneInfo bones[BONE_COUNT] = {0};

    Matrix localBind[BONE_COUNT];
    Matrix modelBind[BONE_COUNT];

    Matrix localPose[BONE_COUNT] = {0};
    Matrix modelPose[BONE_COUNT] = {0};

    for (int i = 0; i < BONE_COUNT; i++)
    {
        localBind[i] = MatrixIdentity();
        modelBind[i] = MatrixIdentity();
    }

    snprintf(bones[ROOT].name, sizeof(bones[ROOT].name), "Root");
    bones[ROOT].parent = -1;

    localBind[ROOT] = MatrixTranslate(0, 0.65f, 0);
    modelBind[ROOT] = localBind[ROOT];

    float height = 0.65f;

    for (int i = 0; i < SEGMENT_COUNT; i++)
    {
        const int boneIndex = path[i];
        const int parentIndex = i == 0 ? ROOT : path[i - 1];

        snprintf(
            bones[boneIndex].name,
            sizeof(bones[boneIndex].name),
            "Tail_%02d",
            i
        );

        bones[boneIndex].parent = parentIndex;

        const float offset = i == 0 ? 0.25f : 0.30f;

        localBind[boneIndex] = MatrixTranslate(0, offset, 0);

        height += offset;
        modelBind[boneIndex] = MatrixTranslate(0, height, 0);
    }

    snprintf(bones[TIP].name, sizeof(bones[TIP].name), "Tip");
    bones[TIP].parent = path[SEGMENT_COUNT - 1];

    localBind[TIP] = MatrixTranslate(0, 0.30f, 0);
    modelBind[TIP] = MatrixTranslate(0, height + 0.30f, 0);

    // ========================================
    // ANIMATION TREE
    // ========================================

    const float times[] = {
        0.0f, 0.4f, 0.8f, 1.2f, 1.6f
    };

    const Vector3 translations[] = {
        { 0.00f, 0.65f, 0},
        { 0.65f, 0.65f, 0},
        { 0.00f, 0.65f, 0},
        {-0.65f, 0.65f, 0},
        { 0.00f, 0.65f, 0}
    };

    Quaternion rotations[5] = {
        QuaternionFromAxisAngle((Vector3){0, 0, 1},  0.00f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1}, -0.40f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1},  0.00f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1},  0.40f),
        QuaternionFromAxisAngle((Vector3){0, 0, 1},  0.00f)
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
        .name = "tail_motion",
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
        .name = "tail_motion",
        .state = {
            .play = true,
            .loop = true,
            .speed = 1.0f
        }
    };

    R3D_AnimationTreeNode* node = R3D_CreateAnimationNode(
        &tree, nodeParams
    );

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

    // ========================================
    // SPRING CHAIN
    // ========================================

    R3D_SpringChainBoneDesc descriptors[SEGMENT_COUNT];

    // Deliberately submit the descriptors in reverse order.
    for (int i = 0; i < SEGMENT_COUNT; i++)
    {
        const int segment = SEGMENT_COUNT - 1 - i;

        float t = (float)segment / (float)(SEGMENT_COUNT - 1);

        float horizontalLimit =
            (18.0f + 25.0f * t) * DEG2RAD;

        float verticalLimit =
            (10.0f + 20.0f * t) * DEG2RAD;

        descriptors[i] = (R3D_SpringChainBoneDesc){
            .boneIndex = path[segment],
            .params = {
                .localAxis = {0, 1, 0},
                .length = 0.30f,

                // Stiffer near the base, softer at the tip.
                .frequencyHz = 5.0f - 3.2f * t,
                .dampingRatio = 0.80f - 0.45f * t,
                .weight = 1.0f,

                .limits = {
                    .enabled = true,
                    .localReferenceAxis = {1, 0, 0},

                    .minHorizontalRadians = -horizontalLimit,
                    .maxHorizontalRadians = horizontalLimit,

                    .minVerticalRadians = -verticalLimit,
                    .maxVerticalRadians = verticalLimit
                }
            }
        };
    }

    R3D_SpringChain chain = R3D_LoadSpringChain(
        &tree.player,
        descriptors,
        SEGMENT_COUNT
    );

    if (!R3D_IsSpringChainValid(&chain))
    {
        TraceLog(LOG_ERROR, "Failed to create spring chain");

        R3D_UnloadAnimationTree(tree);
        R3D_UnloadMesh(cube);
        R3D_UnloadMesh(sphere);
        R3D_UnloadMesh(ground);
        R3D_Close();
        CloseWindow();
        return 1;
    }

    // ========================================
    // CONTROLS
    // ========================================

    float frequencyScale = 1.0f;
    float dampingAdjust = 0.0f;

    bool animationPaused = false;
    bool springEnabled = true;
    bool limitsEnabled = true;
    bool showAuthored = false;

    float teleportOffset = 0.0f;
    float cameraAngle = 0.4f;

    Camera3D camera = {
        .position = {2, 3, 6},
        .target = {0, 2.1f, 0},
        .up = {0, 1, 0},
        .fovy = 60,
        .projection = CAMERA_PERSPECTIVE
    };

    while (!WindowShouldClose())
    {
        float dt = Clamp(GetFrameTime(), 0.0f, 1.0f / 30.0f);

        if (IsKeyPressed(KEY_P))
            animationPaused = !animationPaused;

        if (IsKeyPressed(KEY_SPACE))
        {
            springEnabled = !springEnabled;
            R3D_ResetSpringChain(&chain);
        }

        if (IsKeyPressed(KEY_H))
            showAuthored = !showAuthored;

        if (IsKeyPressed(KEY_L))
        {
            limitsEnabled = !limitsEnabled;
            R3D_ResetSpringChain(&chain);
        }

        if (IsKeyPressed(KEY_T))
        {
            teleportOffset = teleportOffset == 0.0f
                ? 1.5f : 0.0f;

            R3D_ResetSpringChain(&chain);
        }

        if (IsKeyDown(KEY_UP))
            frequencyScale += dt;

        if (IsKeyDown(KEY_DOWN))
            frequencyScale -= dt;

        if (IsKeyDown(KEY_RIGHT))
            dampingAdjust += dt * 0.4f;

        if (IsKeyDown(KEY_LEFT))
            dampingAdjust -= dt * 0.4f;

        if (IsKeyDown(KEY_Z))
            cameraAngle -= dt;

        if (IsKeyDown(KEY_C))
            cameraAngle += dt;

        frequencyScale = Clamp(frequencyScale, 0.2f, 3.0f);
        dampingAdjust = Clamp(dampingAdjust, -0.35f, 0.2f);

        if (IsKeyPressed(KEY_R))
        {
            frequencyScale = 1.0f;
            dampingAdjust = 0.0f;
            teleportOffset = 0.0f;

            animationPaused = false;
            springEnabled = true;
            limitsEnabled = true;
            showAuthored = false;

            R3D_ResetSpringChain(&chain);
        }

        camera.position = (Vector3){
            sinf(cameraAngle) * 6.0f,
            3.0f,
            cosf(cameraAngle) * 6.0f
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

        if (poseValid)
        {
            localPose[ROOT].m12 += teleportOffset;

            poseValid = R3D_ComputeAnimationTreeModelPose(&tree);
        }

        Vector3 authoredJoints[SEGMENT_COUNT + 1] = {0};
        Vector3 finalJoints[SEGMENT_COUNT + 1] = {0};

        if (poseValid)
        {
            for (int i = 0; i < SEGMENT_COUNT; i++)
            {
                authoredJoints[i] = position(modelPose[path[i]]);
            }

            authoredJoints[SEGMENT_COUNT] =
                position(modelPose[TIP]);
        }

        // ========================================
        // STAGE 2: SPRING CHAIN
        // ========================================

        // The sorted chain corresponds to path[0..7].
        // Each segment has independent parameters.
        for (int i = 0; i < chain.boneCount; i++)
        {
            float t = (float)i / (float)(SEGMENT_COUNT - 1);

            chain.bones[i].params.frequencyHz =
                (5.0f - 3.2f * t) * frequencyScale;

            chain.bones[i].params.dampingRatio = Clamp(
                0.80f - 0.45f * t + dampingAdjust,
                0.0f,
                1.0f
            );

            chain.bones[i].params.limits.enabled =
                limitsEnabled;
        }

        bool springSuccess = true;

        if (poseValid && springEnabled)
        {
            springSuccess = R3D_UpdateSpringChain(
                &chain,
                &tree.player,
                dt
            );
        }

        if (poseValid)
        {
            for (int i = 0; i < SEGMENT_COUNT; i++)
            {
                finalJoints[i] = position(modelPose[path[i]]);
            }

            finalJoints[SEGMENT_COUNT] =
                position(modelPose[TIP]);
        }

        // ========================================
        // RENDER
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

                if (poseValid)
                {
                    draw_segment(
                        cube,
                        base,
                        position(modelPose[ROOT]),
                        finalJoints[0],
                        0.35f
                    );

                    if (showAuthored)
                    {
                        draw_tail(
                            cube, sphere,
                            authored, authored,
                            authoredJoints,
                            0.06f
                        );
                    }

                    draw_tail(
                        cube, sphere,
                        blue, green,
                        finalJoints,
                        0.18f
                    );
                }
            R3D_End();

            DrawRectangle(8, 8, 650, 188, Fade(BLACK, 0.75f));

            DrawText(
                "SPRING CHAIN / 8 SEGMENTS",
                18, 16, 22, RAYWHITE
            );

            DrawText(
                TextFormat(
                    "Chain: %s  |  Animation: %s  |  Limits: %s",
                    springEnabled ? "ON" : "OFF",
                    animationPaused ? "PAUSED" : "PLAYING",
                    limitsEnabled ? "ON" : "OFF"
                ),
                18, 48, 16, LIGHTGRAY
            );

            DrawText(
                TextFormat(
                    "Frequency scale: %.2f  |  Damping adjust: %.2f",
                    frequencyScale, dampingAdjust
                ),
                18, 74, 16, LIGHTGRAY
            );

            DrawText(
                "UP/DOWN Frequency    LEFT/RIGHT Damping",
                18, 99, 16, LIGHTGRAY
            );

            DrawText(
                "SPACE Chain    P Pause    L Limits    H Authored",
                18, 123, 16, LIGHTGRAY
            );

            DrawText(
                "T Teleport/reset    Z/C Camera    R Reset",
                18, 147, 16, LIGHTGRAY
            );

            if (!poseValid || !springSuccess)
            {
                DrawText(
                    "SPRING CHAIN ERROR",
                    18, 169, 16, RED
                );
            }

            DrawFPS(GetScreenWidth() - 100, 12);
        EndDrawing();
    }

    R3D_UnloadSpringChain(chain);
    R3D_UnloadAnimationTree(tree);

    R3D_UnloadMesh(cube);
    R3D_UnloadMesh(sphere);
    R3D_UnloadMesh(ground);

    R3D_Close();
    CloseWindow();

    return 0;
}