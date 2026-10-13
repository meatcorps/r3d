
#include <r3d/r3d.h>
#include <raymath.h>

#include <math.h>
#include <stdio.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

int main(void)
{
    const char* filePath =
        RESOURCES_PATH "models/AnimatedMorphCube.glb";

    InitWindow(
        1152, 648,
        "[r3d] Imported Morph Animation"
    );

    SetTargetFPS(60);

    if (!R3D_Init(GetScreenWidth(), GetScreenHeight()))
    {
        CloseWindow();
        return 1;
    }

    R3D_Model model = R3D_LoadModel(filePath);
    R3D_AnimationLib animations =
        R3D_LoadAnimationLib(filePath);

    R3D_ShapeKeyGpu gpu = {0};
    R3D_ShapeKeyInstance instance = {0};

    int result = 1;
    int meshIndex = -1;
    int animationIndex = -1;

    // Find any compatible mesh + morph animation.
    for (int a = 0; a < animations.count; a++)
    {
        const R3D_Animation* animation =
            &animations.animations[a];

        TraceLog(
            LOG_INFO,
            "Animation %d: %s (%d morph channels)",
            a,
            animation->name,
            animation->morphChannelCount
        );

        for (int m = 0; m < model.meshCount; m++)
        {
            if (!model.shapeKeySets)
                continue;

            const R3D_ShapeKeySet* set =
                &model.shapeKeySets[m];

            const R3D_MorphAnimationChannel* channel =
                R3D_GetMorphAnimationChannel(animation, m);

            if (channel &&
                set->keyCount > 0 &&
                channel->targetCount == set->keyCount)
            {
                meshIndex = m;
                animationIndex = a;
                break;
            }
        }

        if (meshIndex >= 0)
            break;
    }

    if (meshIndex < 0 || animationIndex < 0)
    {
        TraceLog(
            LOG_ERROR,
            "No compatible imported morph animation found"
        );
        goto cleanup;
    }

    const R3D_Animation* animation =
        &animations.animations[animationIndex];

    const R3D_ShapeKeySet* source =
        &model.shapeKeySets[meshIndex];

    const R3D_MorphAnimationChannel* channel =
        R3D_GetMorphAnimationChannel(
            animation, meshIndex
        );

    if (!channel ||
        animation->ticksPerSecond <= 0.0f ||
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

    TraceLog(
        LOG_INFO,
        "Playing '%s': mesh %d, %d targets, %d keyframes",
        animation->name,
        meshIndex,
        channel->targetCount,
        channel->keyCount
    );

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

    float radius = fmaxf(
        0.1f,
        fmaxf(size.x, fmaxf(size.y, size.z))
    );

    Vector3 center = Vector3Scale(
        Vector3Add(mesh.aabb.min, mesh.aabb.max),
        0.5f
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

    const float durationSeconds =
        animation->duration / animation->ticksPerSecond;

    float timeSeconds = 0.0f;
    bool paused = false;

    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_ORBITAL);

        if (IsKeyPressed(KEY_SPACE))
            paused = !paused;

        if (IsKeyPressed(KEY_R))
            timeSeconds = 0.0f;

        if (!paused)
            timeSeconds += GetFrameTime();

        // Loop using the clip's imported duration.
        float currentSeconds =
            fmodf(timeSeconds, durationSeconds);

        float currentTicks =
            currentSeconds * animation->ticksPerSecond;

        // No procedural sine waves!
        if (!R3D_EvaluateMorphAnimation(
            animation,
            meshIndex,
            currentTicks,
            &instance))
        {
            TraceLog(
                LOG_ERROR,
                "Morph animation evaluation failed"
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
            TextFormat("Imported animation: %s", animation->name),
            20, 20, 20, RAYWHITE
        );

        DrawText(
            TextFormat(
                "Time: %.2f / %.2f seconds",
                currentSeconds,
                durationSeconds
            ),
            20, 50, 20, SKYBLUE
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
                20, 85 + i * 25, 20, GREEN
            );
        }

        DrawText(
            "SPACE: Pause | R: Restart",
            20, 205, 20, RAYWHITE
        );

        if (!drawOK)
        {
            DrawText(
                "GPU DRAW FAILED",
                20, 240, 20, RED
            );
        }

        DrawFPS(GetScreenWidth() - 100, 20);
        EndDrawing();
    }

    result = 0;

cleanup:
    R3D_UnloadShapeKeyInstance(instance);
    R3D_UnloadShapeKeyGpu(gpu);

    R3D_UnloadAnimationLib(animations);
    R3D_UnloadModel(model, true);

    R3D_Close();
    CloseWindow();

    return result;
}
