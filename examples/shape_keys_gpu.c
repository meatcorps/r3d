
#include <r3d/r3d.h>
#include <raymath.h>
#include <math.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

int main(void)
{
    InitWindow(1152, 648, "[r3d] GPU Shape Keys");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());

    R3D_Model model = R3D_LoadModel(
        RESOURCES_PATH "models/AnimatedMorphCube.glb"
    );

    int meshIndex = -1;

    for (int i = 0; i < model.meshCount; i++)
    {
        if (model.shapeKeySets &&
            R3D_GetShapeKeyCount(&model.shapeKeySets[i]) >= 2)
        {
            meshIndex = i;
            break;
        }
    }

    if (meshIndex < 0)
    {
        TraceLog(LOG_ERROR, "No mesh with two shape keys found");

        R3D_UnloadModel(model, true);
        R3D_Close();
        CloseWindow();
        return 1;
    }

    const R3D_ShapeKeySet* source =
        &model.shapeKeySets[meshIndex];

    // One shared GPU morph buffer.
    R3D_ShapeKeyGpu gpu = R3D_LoadShapeKeyGpu(source);

    // Two independent sets of weights.
    R3D_ShapeKeyInstance left =
        R3D_LoadShapeKeyInstance(source);

    R3D_ShapeKeyInstance right =
        R3D_LoadShapeKeyInstance(source);

    if (!R3D_IsShapeKeyGpuValid(&gpu) ||
        !left.weights || !right.weights)
    {
        TraceLog(LOG_ERROR, "GPU shape-key setup failed");

        R3D_UnloadShapeKeyInstance(left);
        R3D_UnloadShapeKeyInstance(right);
        R3D_UnloadShapeKeyGpu(gpu);
        R3D_UnloadModel(model, true);

        R3D_Close();
        CloseWindow();
        return 1;
    }

    R3D_Mesh mesh = model.meshes[meshIndex];

    R3D_Material material =
        model.materials[model.meshMaterials[meshIndex]];

    R3D_ENVIRONMENT_SET(
        ambient.color,
        (Color){70, 70, 70, 255}
        );

    R3D_ENVIRONMENT_SET(
        background.color,
        (Color){0, 0, 0, 255}
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

    float separation = radius * 1.2f;

    Camera3D camera = {
        .position = {
            center.x,
            center.y + radius,
            center.z + radius * 5.5f
        },
        .target = center,
        .up = {0, 1, 0},
        .fovy = 55.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_ORBITAL);

        float time = (float)GetTime();

        float weight1 =
            0.5f + 0.5f * sinf(time * 1.5f);

        float weight2 =
            0.5f + 0.5f * sinf(time * 1.5f + 1.5f);

        // Left: only shape key 1.
        R3D_SetShapeKeyWeight(&left, 0, weight1);

        // Right: only shape key 2.
        R3D_SetShapeKeyWeight(&right, 1, weight2);

        BeginDrawing();
        ClearBackground(BLACK);

        R3D_Begin(camera);

        R3D_PushLight(light);

        bool leftOK = R3D_DrawShapeKeyMeshPro(
            mesh,
            material,
            &gpu,
            &left,
            MatrixTranslate(-separation, 0, 0)
        );

        bool rightOK = R3D_DrawShapeKeyMeshPro(
            mesh,
            material,
            &gpu,
            &right,
            MatrixTranslate(separation, 0, 0)
        );

        R3D_End();

        DrawText(
            TextFormat("Shape 1: %.2f", weight1),
            20, 20, 20, GREEN
        );

        DrawText(
            TextFormat("Shape 2: %.2f", weight2),
            20, 50, 20, SKYBLUE
        );

        if (!leftOK || !rightOK)
        {
            DrawText(
                "GPU MORPH DRAW FAILED",
                20, 90, 20, RED
            );
        }

        DrawFPS(GetScreenWidth() - 100, 20);

        EndDrawing();
    }

    R3D_UnloadShapeKeyInstance(left);
    R3D_UnloadShapeKeyInstance(right);
    R3D_UnloadShapeKeyGpu(gpu);
    R3D_UnloadModel(model, true);

    R3D_Close();
    CloseWindow();

    return 0;
}
