
#include <r3d/r3d.h>
#include <raymath.h>

#include <math.h>
#include <stdio.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

int main(void)
{
    const char* path =
        RESOURCES_PATH "models/AnimatedMorphCube.glb";

    InitWindow(
        1152, 648,
        "[r3d] Morph AnimationTree"
    );

    SetTargetFPS(60);

    if (!R3D_Init(GetScreenWidth(), GetScreenHeight()))
    {
        CloseWindow();
        return 1;
    }

    int exitCode = 1;

    R3D_Model model = {0};
    R3D_AnimationLib animations = {0};

    R3D_ShapeKeyInstance instance = {0};
    R3D_ShapeKeyGpu gpu = {0};

    R3D_AnimationTree tree = {0};

    // ========================================
    // LOAD ASSET
    // ========================================

    model = R3D_LoadModel(path);
    animations = R3D_LoadAnimationLib(path);

    int meshIndex = -1;
    int animIndex = -1;

    for (int a = 0; a < animations.count; a++)
    {
        const R3D_Animation* anim =
            &animations.animations[a];

        for (int m = 0; m < model.meshCount; m++)
        {
            if (!model.shapeKeySets)
                continue;

            const R3D_MorphAnimationChannel* channel =
                R3D_GetMorphAnimationChannel(anim, m);

            if (channel &&
                channel->targetCount > 0 &&
                channel->targetCount ==
                    model.shapeKeySets[m].keyCount)
            {
                animIndex = a;
                meshIndex = m;
                break;
            }
        }

        if (meshIndex >= 0)
            break;
    }

    if (meshIndex < 0 || animIndex < 0)
    {
        TraceLog(
            LOG_ERROR,
            "No imported morph animation found"
        );
        goto cleanup;
    }

    const R3D_Animation* animation =
        &animations.animations[animIndex];

    const R3D_ShapeKeySet* source =
        &model.shapeKeySets[meshIndex];

    if (animation->ticksPerSecond <= 0.0f ||
        animation->duration <= 0.0f)
    {
        goto cleanup;
    }

    instance = R3D_LoadShapeKeyInstance(source);
    gpu = R3D_LoadShapeKeyGpu(source);

    if (!instance.weights ||
        !R3D_IsShapeKeyGpuValid(&gpu))
    {
        goto cleanup;
    }

    // ========================================
    // ANIMATION TREE
    // ========================================

    // The cube has no skeleton.
    //
    // AnimationTree only needs the library to
    // advance its morph animation nodes.
    // No GPU skinning player is required here.
    R3D_AnimationPlayer player = {0};
    player.animLib = animations;

    tree = R3D_LoadAnimationTree(player, 3);

    R3D_AnimationNodeParams first = {0};

    snprintf(
        first.name,
        sizeof(first.name),
        "%s",
        animation->name
    );

    first.state = (R3D_AnimationState){
        .currentTime = 0.0f,
        .speed = 1.0f,
        .play = true,
        .loop = true
    };

    R3D_AnimationNodeParams second = first;

    // Start the second leaf half a cycle later.
    const float durationSeconds =
        animation->duration / animation->ticksPerSecond;

    second.state.currentTime =
        durationSeconds * 0.5f;

    R3D_AnimationTreeNode* firstNode =
        R3D_CreateAnimationNode(&tree, first);

    R3D_AnimationTreeNode* secondNode =
        R3D_CreateAnimationNode(&tree, second);

    R3D_Blend2NodeParams blendParams = {
        .blend = 0.5f
    };

    R3D_AnimationTreeNode* blendNode =
        R3D_CreateBlend2Node(&tree, blendParams);

    if (!firstNode || !secondNode || !blendNode)
    {
        TraceLog(
            LOG_ERROR,
            "Failed to create animation tree nodes"
        );
        goto cleanup;
    }

    if (!R3D_AddAnimationNode(
            blendNode, firstNode, 0) ||
        !R3D_AddAnimationNode(
            blendNode, secondNode, 1))
    {
        TraceLog(
            LOG_ERROR,
            "Failed to connect animation tree nodes"
        );
        goto cleanup;
    }

    R3D_AddRootAnimationNode(&tree, blendNode);

    // ========================================
    // RENDER SETUP
    // ========================================

    R3D_Mesh mesh = model.meshes[meshIndex];

    R3D_Material material =
        model.materials[model.meshMaterials[meshIndex]];

    R3D_ENVIRONMENT_SET(
        background.color,
        (Color){0, 0, 0, 255}
    );

    R3D_ENVIRONMENT_SET(
        ambient.color,
        (Color){70, 70, 70, 255}
    );

    R3D_Light light = R3D_CreateDirLight(
        (Vector3){-1, -1, -1},
        WHITE,
        2.0f
    );

    Vector3 size = Vector3Subtract(
        mesh.aabb.max, mesh.aabb.min
    );

    Vector3 center = Vector3Scale(
        Vector3Add(mesh.aabb.min, mesh.aabb.max),
        0.5f
    );

    float radius = fmaxf(
        0.1f,
        fmaxf(size.x, fmaxf(size.y, size.z))
    );

    Camera3D camera = {
        .position = {
            center.x,
            center.y + radius,
            center.z + radius * 5.0f
        },
        .target = center,
        .up = {0, 1, 0},
        .fovy = 55.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    bool paused = false;

    // ========================================
    // MAIN LOOP
    // ========================================

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();

        UpdateCamera(&camera, CAMERA_ORBITAL);

        if (IsKeyPressed(KEY_SPACE))
            paused = !paused;

        // Change the tree blend, NOT morph weights.
        if (IsKeyDown(KEY_RIGHT))
            blendParams.blend += dt * 0.5f;

        if (IsKeyDown(KEY_LEFT))
            blendParams.blend -= dt * 0.5f;

        blendParams.blend =
            Clamp(blendParams.blend, 0.0f, 1.0f);

        R3D_SetBlend2NodeParams(
            blendNode,
            blendParams
        );

        // Both animation leaves advance using
        // their existing AnimationTree states.
        R3D_UpdateAnimationTree(
            &tree,
            paused ? 0.0f : dt
        );

        // Evaluate imported morph weights through
        // the tree's Blend2 root node.
        if (!R3D_EvaluateAnimationTreeMorphs(
            &tree,
            meshIndex,
            &instance))
        {
            TraceLog(
                LOG_ERROR,
                "AnimationTree morph evaluation failed"
            );
            break;
        }

        BeginDrawing();
        ClearBackground(BLACK);

        R3D_Begin(camera);
        R3D_PushLight(light);

        bool drawOK = R3D_DrawShapeKeyMeshPro(
            mesh,
            material,
            &gpu,
            &instance,
            MatrixIdentity()
        );

        R3D_End();

        DrawText(
            TextFormat(
                "Imported clip: %s",
                animation->name
            ),
            20, 20, 20, RAYWHITE
        );

        DrawText(
            TextFormat(
                "AnimationTree Blend2: %.2f",
                blendParams.blend
            ),
            20, 55, 20, SKYBLUE
        );

        for (int i = 0;
             i < source->keyCount && i < 4;
             i++)
        {
            DrawText(
                TextFormat(
                    "Morph %d: %.3f",
                    i,
                    R3D_GetShapeKeyWeight(&instance, i)
                ),
                20, 90 + i * 25, 20, GREEN
            );
        }

        DrawText(
            "LEFT/RIGHT: Blend | SPACE: Pause",
            20, 215, 20, RAYWHITE
        );

        if (!drawOK)
        {
            DrawText(
                "GPU DRAW FAILED",
                20, 250, 20, RED
            );
        }

        DrawFPS(GetScreenWidth() - 100, 20);

        EndDrawing();
    }

    exitCode = 0;

cleanup:

    // The tree borrows the animation library.
    R3D_UnloadAnimationTree(tree);

    R3D_UnloadShapeKeyInstance(instance);
    R3D_UnloadShapeKeyGpu(gpu);

    R3D_UnloadAnimationLib(animations);
    R3D_UnloadModel(model, true);

    R3D_Close();
    CloseWindow();

    return exitCode;
}
