/*
 * Meatcorps R3D - PROCEDURAL GEOMETRY SHOWROOM
 *
 * Phase 5: Text mesh generation, font reuse and performance.
 *
 * LEFT/RIGHT : exhibit
 * F          : change font
 * S          : hard / smooth edges
 * U          : automatic updates
 * B          : 12-sample live update benchmark
 * R          : regenerate
 * O          : orbital camera
 */

#include <r3d/r3d.h>
#include <r3d/r3d_text_mesh.h>
#include <raymath.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define VIEW_W 1600
#define VIEW_H 900

#define FONT_COUNT 2
#define EXHIBIT_COUNT 3
#define BENCHMARK_SAMPLES 12

typedef struct ShowroomStats {
    double generationMs;
    double uploadMs;
    double peakGenerationMs;
    double peakUploadMs;

    double benchmarkGenerationTotal;
    double benchmarkUploadTotal;
    double benchmarkGenerationAverage;
    double benchmarkUploadAverage;

    int benchmarkRemaining;
    int benchmarkCompleted;

    int vertexCount;
    int triangleCount;
    unsigned int updates;
} ShowroomStats;

typedef struct ProceduralShowroom {
    R3D_TextFont fonts[FONT_COUNT];

    R3D_Mesh titleMesh;
    R3D_Mesh infoMesh;
    R3D_Mesh pedestalMesh;
    R3D_Mesh torusMesh;

    R3D_Material titleMaterial;
    R3D_Material infoMaterial;
    R3D_Material pedestalMaterial;
    R3D_Material torusMaterial;

    int fontIndex;
    int exhibitIndex;

    bool smooth;
    bool automatic;
    bool orbital;
    bool lastUpdateOK;

    float elapsed;
    float autoTimer;

    ShowroomStats stats;
} ProceduralShowroom;

static const char* FONT_NAMES[FONT_COUNT] = {
    "DARKBYTE",
    "SCABBER"
};

static const char* EXHIBIT_NAMES[EXHIBIT_COUNT] = {
    "TITLE GENERATION",
    "GLYPH TOPOLOGY",
    "INFORMATION PLAQUE"
};

static const char* EXHIBIT_TEXT[EXHIBIT_COUNT] = {
    "MEATCORPS",
    "B8Oa@ 2026\nABC 012",
    "PROCEDURAL\nGEOMETRY"
};

static const char* EXHIBIT_DETAILS[EXHIBIT_COUNT] = {
    "Extruded typography, font switching and PBR lighting",
    "Multiple contours, counters, curved and sharp outlines",
    "Runtime text replacement using reusable glyph data"
};

static const Color FONT_COLORS[FONT_COUNT] = {
    {245, 180, 67, 255},
    {57, 190, 248, 255}
};

static R3D_TextFont showroom_load_memory_font(const char* path)
{
    R3D_TextFont result = {0};

    int size = 0;
    unsigned char* bytes = LoadFileData(path, &size);

    if (!bytes)
        return result;

    if (size > 0)
    {
        result = R3D_LoadTextFontFromMemory(
            bytes, (size_t)size
        );
    }

    // The font resource owns a copy.
    UnloadFileData(bytes);

    return result;
}

static R3D_TextMeshSettings showroom_settings(
    const ProceduralShowroom* showroom,
    bool smallText)
{
    R3D_TextMeshSettings settings =
        R3D_DefaultTextMeshSettings();

    settings.fontSize = smallText ? 0.36f : 0.86f;
    settings.depth = smallText ? 0.10f : 0.28f;
    settings.curveTolerance = 0.003f;
    settings.letterSpacing = smallText ? 0.025f : 0.015f;
    settings.lineSpacing = 1.2f;
    settings.alignment = R3D_TEXT_MESH_ALIGN_CENTER;

    if (showroom->smooth)
    {
        settings.bevelSmoothness = 0.55f;
        settings.bevelSubdivisions = 2;
    }

    return settings;
}

/*
 * Regenerate both text meshes and update the existing
 * GPU meshes. No new font resource is created here.
 *
 * Timings:
 *   generationMs = CPU geometry generation
 *   uploadMs     = R3D mesh upload/update submission
 *
 * uploadMs is NOT a GPU execution-time measurement.
 */
static bool showroom_regenerate(ProceduralShowroom* showroom)
{
    R3D_TextFont* font =
        &showroom->fonts[showroom->fontIndex];

    char info[128];

    snprintf(
        info,
        sizeof(info),
        "%s\n%s",
        FONT_NAMES[showroom->fontIndex],
        showroom->smooth ? "SMOOTH EDGES" : "HARD EDGES"
    );

    R3D_TextMeshSettings titleSettings =
        showroom_settings(showroom, false);

    R3D_TextMeshSettings infoSettings =
        showroom_settings(showroom, true);

    double start = GetTime();

    R3D_MeshData titleData = R3D_GenMeshDataText(
        font,
        EXHIBIT_TEXT[showroom->exhibitIndex],
        titleSettings
    );

    R3D_MeshData infoData = R3D_GenMeshDataText(
        font,
        info,
        infoSettings
    );

    double generationMs = (GetTime() - start) * 1000.0;

    if (!R3D_IsMeshDataValid(titleData) ||
        !R3D_IsMeshDataValid(infoData))
    {
        R3D_UnloadMeshData(titleData);
        R3D_UnloadMeshData(infoData);

        showroom->lastUpdateOK = false;
        return false;
    }

    start = GetTime();

    bool titleOK;
    bool infoOK;

    if (R3D_IsMeshValid(showroom->titleMesh))
    {
        titleOK = R3D_UpdateMesh(
            &showroom->titleMesh,
            titleData,
            NULL
        );
    }
    else
    {
        showroom->titleMesh = R3D_LoadMesh(
            R3D_PRIMITIVE_TRIANGLES,
            titleData,
            NULL
        );

        titleOK = R3D_IsMeshValid(showroom->titleMesh);
    }

    if (R3D_IsMeshValid(showroom->infoMesh))
    {
        infoOK = R3D_UpdateMesh(
            &showroom->infoMesh,
            infoData,
            NULL
        );
    }
    else
    {
        showroom->infoMesh = R3D_LoadMesh(
            R3D_PRIMITIVE_TRIANGLES,
            infoData,
            NULL
        );

        infoOK = R3D_IsMeshValid(showroom->infoMesh);
    }

    double uploadMs = (GetTime() - start) * 1000.0;

    if (titleOK && infoOK)
    {
        ShowroomStats* stats = &showroom->stats;

        stats->generationMs = generationMs;
        stats->uploadMs = uploadMs;

        stats->peakGenerationMs = fmax(
            stats->peakGenerationMs, generationMs
        );

        stats->peakUploadMs = fmax(
            stats->peakUploadMs, uploadMs
        );

        stats->vertexCount =
            titleData.vertexCount + infoData.vertexCount;

        stats->triangleCount =
            (titleData.indexCount + infoData.indexCount) / 3;

        stats->updates++;

        showroom->titleMaterial.albedo.color =
            FONT_COLORS[showroom->fontIndex];
    }

    // CPU buffers are no longer needed after upload.
    R3D_UnloadMeshData(titleData);
    R3D_UnloadMeshData(infoData);

    showroom->lastUpdateOK = titleOK && infoOK;
    return showroom->lastUpdateOK;
}

static void showroom_draw_scene(
    ProceduralShowroom* showroom,
    float elapsed)
{
    // The main text occupies the center of the stage.
    if (R3D_IsMeshValid(showroom->titleMesh))
    {
        R3D_DrawMesh(
            showroom->titleMesh,
            showroom->titleMaterial,
            (Vector3){1.60f, 0.65f, 0.20f},
            1.0f
        );
    }

    // This is REAL 3D text, not a Raylib DrawText overlay.
    if (R3D_IsMeshValid(showroom->infoMesh))
    {
        R3D_DrawMesh(
            showroom->infoMesh,
            showroom->infoMaterial,
            (Vector3){1.60f, -1.05f, 0.35f},
            1.0f
        );
    }

    // Permanent geometric stage.
    R3D_DrawMesh(
        showroom->pedestalMesh,
        showroom->pedestalMaterial,
        (Vector3){1.60f, -2.12f, -0.75f},
        1.0f
    );

    // A separate animated procedural shape, proving that
    // changing text geometry does not affect other meshes.
    Matrix torusTransform = MatrixMultiply(
        MatrixRotateY(elapsed * 0.60f),
        MatrixTranslate(5.10f, 0.0f, -0.70f)
    );

    R3D_DrawMeshPro(
        showroom->torusMesh,
        showroom->torusMaterial,
        torusTransform
    );
}

static void showroom_draw_hud(
    const ProceduralShowroom* showroom)
{
    const ShowroomStats* stats = &showroom->stats;

    Color accent = FONT_COLORS[showroom->fontIndex];
    Color pale = {220, 232, 244, 255};
    Color muted = {136, 155, 180, 255};

    DrawRectangle(
        0, 0, 350, VIEW_H,
        (Color){9, 16, 29, 245}
    );

    DrawRectangle(349, 0, 2, VIEW_H, accent);

    DrawText("MEATCORPS / R3D", 27, 31, 22, pale);

    DrawText("PROCEDURAL", 27, 105, 36, RAYWHITE);
    DrawText("SHOWROOM", 27, 150, 43, accent);

    DrawText("PHASE 05 / GEOMETRY", 28, 214, 17, muted);

    for (int i = 0; i < EXHIBIT_COUNT; i++)
    {
        int y = 276 + i * 67;

        bool active = i == showroom->exhibitIndex;

        if (active)
        {
            DrawRectangle(
                18, y - 12, 315, 54,
                Fade(accent, 0.16f)
            );
        }

        DrawText(
            TextFormat("0%d", i + 1),
            29, y, 18,
            active ? accent : muted
        );

        DrawText(
            EXHIBIT_NAMES[i],
            69, y, 18,
            active ? RAYWHITE : muted
        );
    }

    DrawText("LIVE GEOMETRY", 28, 509, 21, accent);

    DrawText(
        TextFormat(
            "Vertices: %d",
            stats->vertexCount
        ),
        28, 549, 19, pale
    );

    DrawText(
        TextFormat(
            "Triangles: %d",
            stats->triangleCount
        ),
        28, 579, 19, pale
    );

    DrawText(
        TextFormat(
            "Generate: %.3f ms",
            stats->generationMs
        ),
        28, 626, 18, pale
    );

    DrawText(
        TextFormat(
            "Upload: %.3f ms",
            stats->uploadMs
        ),
        28, 656, 18, pale
    );

    DrawText(
        TextFormat(
            "Peak: %.2f / %.2f ms",
            stats->peakGenerationMs,
            stats->peakUploadMs
        ),
        28, 688, 16, muted
    );

    DrawText(
        TextFormat(
            "Updates: %u",
            stats->updates
        ),
        28, 725, 18, pale
    );

    DrawText(
        TextFormat(
            "FPS: %d",
            GetFPS()
        ),
        28, 760, 18, accent
    );

    DrawText(
        showroom->lastUpdateOK
            ? "RESOURCE STATE: OK"
            : "RESOURCE STATE: FAILED",
        28, 813, 16,
        showroom->lastUpdateOK ? GREEN : RED
    );

    // Top title bar.
    DrawRectangle(
        350, 0, VIEW_W - 350, 112,
        (Color){8, 15, 27, 225}
    );

    DrawText(
        EXHIBIT_NAMES[showroom->exhibitIndex],
        390, 25, 34, RAYWHITE
    );

    DrawText(
        EXHIBIT_DETAILS[showroom->exhibitIndex],
        391, 73, 18, accent
    );

    // Bottom controls.
    DrawRectangle(
        350, VIEW_H - 132,
        VIEW_W - 350, 132,
        (Color){8, 15, 27, 235}
    );

    DrawText(
        "LEFT/RIGHT  EXHIBIT    F  FONT    S  SMOOTH    U  AUTO    R  REBUILD",
        387, VIEW_H - 107, 18, pale
    );

    DrawText(
        "B  BENCHMARK    O  ORBIT    ESC  EXIT",
        387, VIEW_H - 72, 18, muted
    );

    DrawText(
        TextFormat(
            "FONT: %s   |   EDGE: %s   |   AUTO: %s",
            FONT_NAMES[showroom->fontIndex],
            showroom->smooth ? "SMOOTH" : "HARD",
            showroom->automatic ? "ON" : "OFF"
        ),
        389, VIEW_H - 36, 17, accent
    );

    if (stats->benchmarkRemaining > 0)
    {
        DrawText(
            TextFormat(
                "BENCHMARK: %d / %d",
                stats->benchmarkCompleted,
                BENCHMARK_SAMPLES
            ),
            VIEW_W - 275, 33, 18, accent
        );
    }
    else if (stats->benchmarkCompleted == BENCHMARK_SAMPLES)
    {
        DrawText(
            TextFormat(
                "AVG GEN: %.2f ms  |  UPDATE: %.2f ms",
                stats->benchmarkGenerationAverage,
                stats->benchmarkUploadAverage
            ),
            VIEW_W - 435, 33, 17, accent
        );
    }
}

int main(void)
{
    InitWindow(
        VIEW_W, VIEW_H,
        "[r3d] Meatcorps - Procedural Geometry Showroom"
    );

    SetTargetFPS(60);

    // Reserve GPU storage to reduce macOS buffer growth.
    R3D_SetHint(
        R3D_HINT_MESH_VERTEX_BUFFER_CAPACITY, 262144
    );

    R3D_SetHint(
        R3D_HINT_MESH_INDEX_BUFFER_CAPACITY, 262144
    );

    if (!R3D_Init(VIEW_W, VIEW_H))
    {
        CloseWindow();
        return 1;
    }

    ProceduralShowroom showroom = {0};

    // Demonstrate both supported font loading paths.
    showroom.fonts[0] = R3D_LoadTextFontFromFile(
        RESOURCES_PATH "fonts/Darkbyte-4nly6.ttf"
    );

    showroom.fonts[1] = showroom_load_memory_font(
        RESOURCES_PATH "fonts/Scabber-q2Mn0.ttf"
    );

    if (!R3D_IsTextFontValid(showroom.fonts[0]) ||
        !R3D_IsTextFontValid(showroom.fonts[1]))
    {
        TraceLog(LOG_ERROR, "Showroom font loading failed");

        R3D_UnloadTextFont(showroom.fonts[0]);
        R3D_UnloadTextFont(showroom.fonts[1]);

        R3D_Close();
        CloseWindow();
        return 1;
    }

    // Explicit glyph preloading keeps font switching cheap
    // relative to re-reading and re-parsing font files.
    for (int i = 0; i < FONT_COUNT; i++)
    {
        if (!R3D_PreloadTextFontAllGlyphs(&showroom.fonts[i]))
        {
            TraceLog(
                LOG_WARNING,
                "Could not fully preload font %s",
                FONT_NAMES[i]
            );
        }
    }

    showroom.titleMaterial = R3D_GetDefaultMaterial();
    showroom.infoMaterial = R3D_GetDefaultMaterial();
    showroom.pedestalMaterial = R3D_GetDefaultMaterial();
    showroom.torusMaterial = R3D_GetDefaultMaterial();

    showroom.titleMaterial.albedo.color = FONT_COLORS[0];
    showroom.infoMaterial.albedo.color =
        (Color){225, 233, 245, 255};

    showroom.pedestalMaterial.albedo.color =
        (Color){36, 49, 69, 255};

    showroom.torusMaterial.albedo.color =
        (Color){135, 110, 240, 255};

    showroom.pedestalMesh = R3D_GenMeshCube(
        8.0f, 0.24f, 2.0f
    );

    showroom.torusMesh = R3D_GenMeshTorus(
        0.78f, 0.22f, 36, 24
    );

    R3D_ENVIRONMENT_SET(
        background.color, (Color){8, 14, 26, 255}
    );

    R3D_ENVIRONMENT_SET(
        ambient.color, (Color){82, 91, 109, 255}
    );

    R3D_Light light = R3D_CreateDirLight(
        (Vector3){-0.6f, -0.7f, -1.0f},
        WHITE,
        2.0f
    );

    R3D_ShadowMap shadow = R3D_LoadShadowMap(
        R3D_LIGHT_DIR
    );

    Camera3D camera = {
        .position = {1.60f, 1.40f, 11.5f},
        .target = {1.60f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 48.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    showroom.lastUpdateOK = showroom_regenerate(&showroom);

    while (!WindowShouldClose())
    {
        bool regenerate = false;
        float delta = fminf(GetFrameTime(), 0.1f);

        showroom.elapsed += delta;

        if (IsKeyPressed(KEY_RIGHT))
        {
            showroom.exhibitIndex =
                (showroom.exhibitIndex + 1) % EXHIBIT_COUNT;

            regenerate = true;
        }

        if (IsKeyPressed(KEY_LEFT))
        {
            showroom.exhibitIndex =
                (showroom.exhibitIndex + EXHIBIT_COUNT - 1)
                % EXHIBIT_COUNT;

            regenerate = true;
        }

        if (IsKeyPressed(KEY_F))
        {
            showroom.fontIndex =
                (showroom.fontIndex + 1) % FONT_COUNT;

            regenerate = true;
        }

        if (IsKeyPressed(KEY_S))
        {
            showroom.smooth = !showroom.smooth;
            regenerate = true;
        }

        if (IsKeyPressed(KEY_R))
            regenerate = true;

        if (IsKeyPressed(KEY_U))
        {
            showroom.automatic = !showroom.automatic;
            showroom.autoTimer = 0.0f;
        }

        if (IsKeyPressed(KEY_O))
            showroom.orbital = !showroom.orbital;

        if (showroom.orbital)
            UpdateCamera(&camera, CAMERA_ORBITAL);

        if (IsKeyPressed(KEY_B))
        {
            showroom.stats.benchmarkRemaining =
                BENCHMARK_SAMPLES;

            showroom.stats.benchmarkCompleted = 0;
            showroom.stats.benchmarkGenerationTotal = 0.0;
            showroom.stats.benchmarkUploadTotal = 0.0;
            showroom.stats.benchmarkGenerationAverage = 0.0;
            showroom.stats.benchmarkUploadAverage = 0.0;
        }

        // Demonstrate long-running dynamic content.
        if (showroom.automatic &&
            showroom.stats.benchmarkRemaining == 0)
        {
            showroom.autoTimer += delta;

            if (showroom.autoTimer >= 4.0f)
            {
                showroom.autoTimer = 0.0f;

                showroom.exhibitIndex =
                    (showroom.exhibitIndex + 1)
                    % EXHIBIT_COUNT;

                if (showroom.exhibitIndex == 0)
                {
                    showroom.fontIndex =
                        (showroom.fontIndex + 1)
                        % FONT_COUNT;
                }

                regenerate = true;
            }
        }

        // On-demand benchmark:
        // one sample per frame rather than twelve back-to-back.
        // Each sample switches fonts and geometry.
        bool benchmarkSample =
            showroom.stats.benchmarkRemaining > 0;

        if (benchmarkSample)
        {
            showroom.exhibitIndex =
                (showroom.exhibitIndex + 1)
                % EXHIBIT_COUNT;

            showroom.fontIndex =
                (showroom.fontIndex + 1)
                % FONT_COUNT;

            regenerate = true;
        }

        if (regenerate)
        {
            bool ok = showroom_regenerate(&showroom);

            if (!ok)
                TraceLog(LOG_WARNING, "Showroom regeneration failed");

            if (benchmarkSample)
            {
                if (ok)
                {
                    showroom.stats.benchmarkGenerationTotal +=
                        showroom.stats.generationMs;

                    showroom.stats.benchmarkUploadTotal +=
                        showroom.stats.uploadMs;

                    showroom.stats.benchmarkCompleted++;
                }

                showroom.stats.benchmarkRemaining--;

                if (showroom.stats.benchmarkRemaining == 0)
                {
                    int count = showroom.stats.benchmarkCompleted;

                    if (count > 0)
                    {
                        showroom.stats.benchmarkGenerationAverage =
                            showroom.stats.benchmarkGenerationTotal / count;

                        showroom.stats.benchmarkUploadAverage =
                            showroom.stats.benchmarkUploadTotal / count;
                    }

                    TraceLog(
                        LOG_INFO,
                        "Text update benchmark: %d successful samples, "
                        "generation %.3f ms, upload %.3f ms",
                        count,
                        showroom.stats.benchmarkGenerationAverage,
                        showroom.stats.benchmarkUploadAverage
                    );
                }
            }
        }

        BeginDrawing();

        ClearBackground((Color){8, 14, 26, 255});

        R3D_Begin(camera);

            R3D_PushLightEx(light, shadow, true);

            showroom_draw_scene(
                &showroom, showroom.elapsed
            );

        R3D_End();

        showroom_draw_hud(&showroom);

        EndDrawing();
    }

    // GPU meshes are owned independently of font data.
    if (R3D_IsMeshValid(showroom.titleMesh))
        R3D_UnloadMesh(showroom.titleMesh);

    if (R3D_IsMeshValid(showroom.infoMesh))
        R3D_UnloadMesh(showroom.infoMesh);

    if (R3D_IsMeshValid(showroom.pedestalMesh))
        R3D_UnloadMesh(showroom.pedestalMesh);

    if (R3D_IsMeshValid(showroom.torusMesh))
        R3D_UnloadMesh(showroom.torusMesh);

    R3D_UnloadTextFont(showroom.fonts[0]);
    R3D_UnloadTextFont(showroom.fonts[1]);

    R3D_UnloadShadowMap(shadow);

    R3D_Close();
    CloseWindow();

    return 0;
}