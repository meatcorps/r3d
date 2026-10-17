#include <r3d/r3d.h>
#include <raymath.h>
#include <math.h>
#include <stddef.h>

// Creates a slightly curved CRT display surface.
// The mesh has independent geometry, but uses the same
// texture-surface shader as the convenience quad.
static R3D_Mesh create_curved_screen(void)
{
    const int segmentsX = 32;
    const int segmentsY = 16;

    const int vertexCount =
        (segmentsX + 1) * (segmentsY + 1);

    const int indexCount =
        segmentsX * segmentsY * 6;

    R3D_MeshData data =
        R3D_LoadMeshData(vertexCount, indexCount);

    R3D_Mesh result = {0};

    if (!data.vertices || !data.indices)
    {
        R3D_UnloadMeshData(data);
        return result;
    }

    for (int y = 0; y <= segmentsY; y++)
    {
        float v = (float)y / (float)segmentsY;

        for (int x = 0; x <= segmentsX; x++)
        {
            float u = (float)x / (float)segmentsX;
            float nx = u * 2.0f - 1.0f;

            Vector3 position = {
                (u - 0.5f) * 3.0f,
                (0.5f - v) * 1.7f,
                -0.20f * nx * nx
            };

            Vector3 normal = Vector3Normalize(
                (Vector3){0.267f * nx, 0.0f, 1.0f}
            );

            Vector3 tangent = Vector3Normalize(
                (Vector3){1.0f, 0.0f, -0.267f * nx}
            );

            int index = y * (segmentsX + 1) + x;

            data.vertices[index] = R3D_MakeVertex(
                position,
                (Vector2){u, v},
                normal,
                (Vector4){tangent.x, tangent.y, tangent.z, 1.0f},
                WHITE
            );
        }
    }

    int index = 0;

    for (int y = 0; y < segmentsY; y++)
    {
        for (int x = 0; x < segmentsX; x++)
        {
            unsigned int a = y * (segmentsX + 1) + x;
            unsigned int b = a + 1;
            unsigned int c = a + segmentsX + 1;
            unsigned int d = c + 1;

            data.indices[index++] = a;
            data.indices[index++] = c;
            data.indices[index++] = b;

            data.indices[index++] = b;
            data.indices[index++] = c;
            data.indices[index++] = d;
        }
    }

    data.vertexCount = vertexCount;
    data.indexCount = indexCount;

    result = R3D_LoadMesh(
        R3D_PRIMITIVE_TRIANGLES, data, NULL
    );

    R3D_UnloadMeshData(data);

    return result;
}

int main(void)
{
    InitWindow(1440, 810, "[r3d] Texture Surface");
    SetTargetFPS(60);

    if (!R3D_Init(GetScreenWidth(), GetScreenHeight()))
    {
        CloseWindow();
        return 1;
    }

    // Precompile the shared quad shader and upload its mesh.
    if (!R3D_InitTexture3DResources())
    {
        R3D_Close();
        CloseWindow();
        return 1;
    }

    RenderTexture2D screen =
        LoadRenderTexture(512, 288);

    Image checkerImage = GenImageChecked(
        256, 256, 32, 32,
        (Color){210, 215, 255, 255},
        (Color){44, 65, 110, 255}
    );

    Texture2D checker =
        LoadTextureFromImage(checkerImage);

    UnloadImage(checkerImage);

    // A completely separate public shader instance.
    R3D_SurfaceShader* crtShader =
        R3D_LoadTextureSurfaceShader();

    R3D_Mesh curvedScreen =
        create_curved_screen();

    if (!crtShader ||
        !R3D_IsMeshValid(curvedScreen) ||
        !screen.id ||
        !checker.id)
    {
        if (R3D_IsMeshValid(curvedScreen))
            R3D_UnloadMesh(curvedScreen);

        if (crtShader)
            R3D_UnloadSurfaceShader(crtShader);

        if (screen.id)
            UnloadRenderTexture(screen);

        if (checker.id)
            UnloadTexture(checker);

        R3D_UnloadTexture3DResources();
        R3D_Close();
        CloseWindow();
        return 1;
    }

    R3D_Material crtMaterial =
        R3D_CreateTextureSurfaceMaterial(crtShader);

    // Reuse the same GPU render texture without readback.
    // BOTH: the texture contributes to albedo and emission.
    R3D_SetTextureSurfaceRenderTarget(
        &crtMaterial,
        screen,
        R3D_TEXTURE_SURFACE_BOTH
    );

    R3D_SetTextureSurfaceHDR(
        &crtMaterial, 2.0f
    );

    R3D_SetTextureSurfaceNormal(
        &crtMaterial,
        R3D_GetNormalTexture(),
        1.0f
    );

    crtMaterial.orm.roughness = 0.35f;
    crtMaterial.orm.metalness = 0.05f;
    crtMaterial.unlit = false;
    crtMaterial.cullMode = R3D_CULL_NONE;

    R3D_ENVIRONMENT_SET(
        background.color, (Color){9, 15, 29, 255}
    );

    R3D_ENVIRONMENT_SET(
        ambient.color, (Color){65, 70, 92, 255}
    );

    R3D_ENVIRONMENT_SET(
        bloom.mode, R3D_BLOOM_ADDITIVE
    );

    R3D_Light light = R3D_CreateDirLight(
        (Vector3){-0.5f, -0.8f, -1.0f},
        WHITE, 2.0f
    );

    Camera3D camera = {
        .position = {0.0f, 1.4f, 9.5f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 48.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    R3D_TextureDrawOptions croppedOptions =
        R3D_DefaultTextureDrawOptions();

    croppedOptions.aspectMode =
        R3D_TEXTURE_ASPECT_CONTAIN;

    croppedOptions.cullMode =
        R3D_CULL_NONE;

    while (!WindowShouldClose())
    {
        float time = (float)GetTime();

        BeginDrawing();

        // Render live 2D content to the offscreen texture.
        BeginTextureMode(screen);

            ClearBackground(
                (Color){7, 24, 37, 255}
            );

            DrawRectangleLines(
                14, 14, 484, 260,
                (Color){50, 220, 190, 255}
            );

            DrawText(
                "MEATCORPS.OS",
                30, 29, 38,
                (Color){90, 255, 195, 255}
            );

            DrawText(
                "LIVE RAYLIB RENDER TEXTURE",
                30, 92, 19, RAYWHITE
            );

            DrawCircle(
                256 + (int)(110.0f * sinf(time * 1.5f)),
                185,
                26.0f,
                (Color){255, 95, 130, 255}
            );

            DrawText(
                TextFormat("SYSTEM TIME: %.1f", time),
                30, 247, 17, LIGHTGRAY
            );

        EndTextureMode();

        // The render target is complete before R3D samples it.
        ClearBackground(
            (Color){9, 15, 29, 255}
        );

        R3D_Begin(camera);

            R3D_PushLight(light);

            // 1. Ordinary texture with pixel source cropping.
            R3D_DrawTexture3DPro(
                checker,
                (Rectangle){32, 32, 128, 128},
                (Vector3){-3.8f, 0.0f, 0.0f},
                QuaternionIdentity(),
                (Vector2){2.3f, 2.3f},
                (Vector2){0.5f, 0.5f},
                croppedOptions
            );

            // 2. Live render texture on the shared flat quad.
            R3D_DrawRenderTexture3DEx(
                screen,
                (Vector3){-0.45f, 0.0f, 0.0f},
                QuaternionIdentity(),
                (Vector2){3.0f, 1.7f},
                WHITE
            );

            // 3. The SAME render texture on custom curved geometry.
            R3D_DrawMeshPro(
                curvedScreen,
                crtMaterial,
                R3D_MatrixFromTRS(
                    (Vector3){3.25f, 0.0f, 0.0f},
                    QuaternionFromAxisAngle(
                        (Vector3){0.0f, 1.0f, 0.0f},
                        -0.18f
                    ),
                    (Vector3){1.0f, 1.0f, 1.0f}
                )
            );

        R3D_End();

        DrawText(
            "TEXTURE QUAD",
            40, 28, 28, RAYWHITE
        );

        DrawText(
            "Source crop  |  Live RenderTexture  |  Curved PBR + HDR screen",
            40, 65, 20, LIGHTGRAY
        );

        DrawFPS(1320, 28);

        EndDrawing();
    }

    // All GPU resources released before R3D_Close().
    R3D_UnloadMesh(curvedScreen);
    R3D_UnloadSurfaceShader(crtShader);
    R3D_UnloadTexture3DResources();

    UnloadRenderTexture(screen);
    UnloadTexture(checker);

    R3D_Close();
    CloseWindow();

    return 0;
}