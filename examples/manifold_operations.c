#include <r3d/r3d.h>
#include <r3d/r3d_manifold.h>
#include <raymath.h>

int main(void)
{
    InitWindow(1152, 648, "[r3d] Manifold Operations");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());

    R3D_MeshData cubeData = R3D_GenMeshDataCube(2.0f, 2.0f, 2.0f);
    // Permanent reset reference; Boolean operations never modify this copy.
    R3D_MeshData originalData = R3D_CopyMeshData(cubeData);
    R3D_MeshData cutterData = R3D_GenMeshDataCube(1.45f, 1.45f, 1.45f);

    R3D_TranslateMeshData(
        &cutterData, (Vector3){0.65f, 0.65f, 0.65f}
    );

    Vector2 outline[] = {
        {-0.8f, -0.8f},
        { 0.8f, -0.8f},
        { 0.8f, -0.3f},
        { 0.3f, -0.3f},
        { 0.3f,  0.8f},
        {-0.8f,  0.8f}
    };

    R3D_Mesh mesh = R3D_LoadMesh(
    R3D_PRIMITIVE_TRIANGLES, cubeData, NULL
    );

    R3D_Mesh cutter = R3D_LoadMesh(
        R3D_PRIMITIVE_TRIANGLES, cutterData, NULL
    );

    R3D_LinkMeshData(&mesh, &cubeData);
    R3D_LinkMeshData(&cutter, &cutterData);

    R3D_Mesh ground = R3D_GenMeshPlane(12.0f, 12.0f, 1, 1);

    Image checkerImage = GenImageChecked(
        256, 256, 32, 32, RAYWHITE, DARKGRAY
    );

    Texture2D checker = LoadTextureFromImage(checkerImage);
    UnloadImage(checkerImage);

    R3D_Material material = R3D_GetDefaultMaterial();
    material.albedo.texture = checker;
    //material.cullMode = R3D_CULL_NONE;

    R3D_Material groundMaterial = R3D_GetDefaultMaterial();
    groundMaterial.albedo.color = LIGHTGRAY;

    R3D_ENVIRONMENT_SET(
        ambient.color, (Color){80, 80, 80, 255}
    );

    R3D_Light light = R3D_CreateDirLight(
        (Vector3){-0.5f, -1.0f, -0.7f}, WHITE, 2.0f
    );

    R3D_ShadowMap shadow = R3D_LoadShadowMap(R3D_LIGHT_DIR);

    Camera3D camera = {
        .position = {4.0f, 3.0f, 4.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 45.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    R3D_ManifoldResult result = {0};
    int selected = 0;

    const char* names[] = {
        "Original cube",
        "Union",
        "Subtraction",
        "Intersection",
        "Plane slice",
        "Extrusion"
    };

    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_ORBITAL);

        int requested = -1;

        if (IsKeyPressed(KEY_SPACE)) requested = 0;
        if (IsKeyPressed(KEY_ONE)) requested = 1;
        if (IsKeyPressed(KEY_TWO)) requested = 2;
        if (IsKeyPressed(KEY_THREE)) requested = 3;
        if (IsKeyPressed(KEY_FOUR)) requested = 4;
        if (IsKeyPressed(KEY_FIVE)) requested = 5;

        if (requested >= 0)
        {
            R3D_UnloadManifoldResult(&result);

            if (requested == 0)
            {
                R3D_MeshData restored = R3D_CopyMeshData(originalData);

                if (R3D_IsMeshDataValid(restored) &&
                    R3D_UpdateMesh(&mesh, restored, NULL))
                {
                    R3D_UnloadMeshData(cubeData);
                    cubeData = restored;
                    selected = 0;
                }
                else
                {
                    R3D_UnloadMeshData(restored);
                    TraceLog(LOG_WARNING, "Failed to reset Manifold mesh");
                }
            }
            else
            {
                switch (requested)
                {
                    case 1:
                        result = R3D_ManifoldUnion(&mesh, &cutter);
                        break;

                    case 2:
                        result = R3D_ManifoldSubtract(&mesh, &cutter);
                        break;

                    case 3:
                        result = R3D_ManifoldIntersect(&mesh, &cutter);
                        break;

                    case 4:
                        result = R3D_ManifoldSlicePlane(
                            &mesh, (Vector3){0.0f, 1.0f, 0.0f}, 0.0f
                        );
                        break;

                    case 5:
                        result = R3D_ManifoldExtrude(
                            &mesh, outline, 6, 1.5f
                        );
                        break;
                }

                if (result.status == R3D_MANIFOLD_OK)
                {
                    selected = requested;
                }
                else
                {
                    TraceLog(
                        LOG_WARNING,
                        "Manifold operation failed: status=%d native=%d",
                        (int)result.status,
                        (int)result.manifoldError
                    );
                }
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        R3D_Begin(camera);

            R3D_PushLightEx(light, shadow, true);

            R3D_DrawMesh(
                ground, groundMaterial,
                (Vector3){0.0f, -2.0f, 0.0f}, 1.0f
            );

            R3D_DrawMesh(
                mesh, material, Vector3Zero(), 1.0f
            );

            // Display additional disconnected solids.
            for (int i = 0; i < result.extraCount; i++)
            {
                R3D_DrawMesh(
                    result.extraMeshes[i],
                    material,
                    (Vector3){
                        0.25f * (i + 1),
                        -0.35f,
                        0.25f * (i + 1)
                    },
                    1.0f
                );
            }

        R3D_End();

        DrawText(
            "1 Union | 2 Subtract | 3 Intersect | 4 Slice | 5 Extrude | SPACE Reset",
            20, 20, 18, BLACK
        );

        DrawText(names[selected], 20, 50, 20, BLACK);

        DrawText(
            TextFormat("Additional pieces: %d", result.extraCount),
            20, 80, 18, DARKGRAY
        );

        EndDrawing();
    }

    R3D_UnloadManifoldResult(&result);

    R3D_UnloadMesh(mesh);
    R3D_UnloadMesh(cutter);
    R3D_UnloadMesh(ground);

    R3D_UnloadMeshData(cubeData);
    R3D_UnloadMeshData(cutterData);
    R3D_UnloadMeshData(originalData);

    R3D_UnloadShadowMap(shadow);
    UnloadTexture(checker);

    R3D_Close();
    CloseWindow();

    return 0;
}