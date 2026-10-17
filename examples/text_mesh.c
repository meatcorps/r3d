#include <r3d/r3d.h>
#include <r3d/r3d_text_mesh.h>
#include <raymath.h>

int main(void)
{
    InitWindow(1280, 720, "[r3d] Phase 4 - 3D Text Mesh");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());

    R3D_TextMeshSettings blockSettings =
        R3D_DefaultTextMeshSettings();

    blockSettings.fontSize = 0.82f;
    blockSettings.depth = 0.32f;
    blockSettings.bevelSmoothness = 0.0f;
    blockSettings.alignment = R3D_TEXT_MESH_ALIGN_RIGHT;

    R3D_TextMeshSettings roundedSettings = blockSettings;

    roundedSettings.curveTolerance = 0.0025f;
    roundedSettings.letterSpacing = 0.035f;
    roundedSettings.lineSpacing = 1.2f;
    roundedSettings.bevelSmoothness = 0.8f;
    roundedSettings.bevelSubdivisions = 2;

    const char* sampleText = "B8Oa@ 2026\nABC 012";

    // Load both reusable font resources.
    R3D_TextFont blockFont = R3D_LoadTextFontFromFile(
        RESOURCES_PATH "fonts/Darkbyte-4nly6.ttf"
    );

    R3D_TextFont roundedFont = R3D_LoadTextFontFromFile(
        RESOURCES_PATH "fonts/Scabber-q2Mn0.ttf"
    );

    R3D_MeshData blockData = {0};
    R3D_MeshData roundedData = {0};

    if (!R3D_PreloadTextFontAllGlyphs(&blockFont))
        TraceLog(LOG_WARNING, "Failed to preload Darkbyte glyphs");

    if (!R3D_PreloadTextFontAllGlyphs(&roundedFont))
        TraceLog(LOG_WARNING, "Failed to preload Scabber glyphs");

    blockData = R3D_GenMeshDataText(
            &blockFont, sampleText, blockSettings
        );

    roundedData = R3D_GenMeshDataText(
            &roundedFont, sampleText, roundedSettings
        );

    // Font resources are no longer needed.
    // Generated mesh data is completely independent.
    R3D_UnloadTextFont(blockFont);
    R3D_UnloadTextFont(roundedFont);

    R3D_Mesh block = R3D_IsMeshDataValid(blockData)
        ? R3D_LoadMesh(
            R3D_PRIMITIVE_TRIANGLES, blockData, NULL
          )
        : (R3D_Mesh){0};

    R3D_Mesh rounded = R3D_IsMeshDataValid(roundedData)
        ? R3D_LoadMesh(
            R3D_PRIMITIVE_TRIANGLES, roundedData, NULL
          )
        : (R3D_Mesh){0};

    R3D_Material blockMat = R3D_GetDefaultMaterial();
    R3D_Material roundedMat = R3D_GetDefaultMaterial();

    blockMat.albedo.color = (Color){240, 188, 90, 255};
    roundedMat.albedo.color = (Color){100, 185, 245, 255};

    R3D_ENVIRONMENT_SET(
        ambient.color, (Color){95, 95, 100, 255}
    );

    R3D_Light light = R3D_CreateDirLight(
        (Vector3){-0.5f, -0.6f, -1.0f},
        WHITE,
        2.0f
    );

    R3D_ShadowMap shadow = R3D_LoadShadowMap(
        R3D_LIGHT_DIR
    );

    Camera3D camera = {
        .position = {0.0f, 1.5f, 4.0f},
        .target = {0.0f, -0.2f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 50.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_ORBITAL);

        BeginDrawing();
        ClearBackground((Color){20, 22, 32, 255});

        R3D_Begin(camera);

            R3D_PushLightEx(light, shadow, true);

            if (R3D_IsMeshValid(block))
            {
                R3D_DrawMesh(
                    block,
                    blockMat,
                    (Vector3){0.0f, 1.7f, 0.0f},
                    1.0f
                );
            }

            if (R3D_IsMeshValid(rounded))
            {
                R3D_DrawMesh(
                    rounded,
                    roundedMat,
                    (Vector3){0.0f, -1.0f, 0.0f},
                    1.0f
                );
            }

        R3D_End();

        DrawText(
            "Darkbyte (top): sharp / Scabber (bottom): curved",
            24, 24, 20, RAYWHITE
        );

        DrawText(
            "B 8 O a @ / multiline / orbit to inspect backs and sides",
            24, 52, 18, LIGHTGRAY
        );

        if (!R3D_IsMeshValid(block) ||
            !R3D_IsMeshValid(rounded))
        {
            DrawText(
                "Text generation failed; check fonts and Manifold",
                24, 86, 18, RED
            );
        }

        EndDrawing();
    }

    if (R3D_IsMeshValid(block))
        R3D_UnloadMesh(block);

    if (R3D_IsMeshValid(rounded))
        R3D_UnloadMesh(rounded);

    R3D_UnloadMeshData(blockData);
    R3D_UnloadMeshData(roundedData);

    R3D_UnloadShadowMap(shadow);

    R3D_Close();
    CloseWindow();

    return 0;
}