
#include <r3d/r3d.h>
#include <raymath.h>
#include <math.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

#define BENCH_LEVEL_COUNT 5

static const int BENCH_COUNTS[BENCH_LEVEL_COUNT] = {
    128, 512, 1024, 2048, 4096
};

static const char* BENCH_MODES[] = {
    "NONE - Original rendering",
    "TINT - One shared scope",
    "GLITCH - One shared scope",
    "COMBINED - Two nested scopes",
    "MIXED - Four batched groups",
    "UNIQUE - Per-object scopes and parameters"
};

static void draw_object(R3D_Mesh mesh, R3D_Material material, int index, int columns, int rows, float spacing)
{
    int x = index % columns;
    int z = index / columns;

    Vector3 position = {
        (x - (columns - 1) * 0.5f) * spacing,
        0.0f,
        (z - (rows - 1) * 0.5f) * spacing
    };

    R3D_DrawMesh(mesh, material, position, 1.0f);
}

static void draw_range(R3D_Mesh mesh, R3D_Material material, int first, int last, int columns, int rows, float spacing)
{
    for (int i = first; i < last; i++)
        draw_object(mesh, material, i, columns, rows, spacing);
}

static void draw_unique(R3D_Mesh mesh, R3D_Material material, R3D_SurfaceEffect* tint, R3D_SurfaceEffect* glitch, int count, int columns, int rows, float spacing)
{
    const float strength = 0.90f;
    const float frequency = 9.0f;

    for (int i = 0; i < count; i++)
    {
        int mode = i % 4;

        // Unique parameters, but only four shader combinations.
        // The compiled variant cache should NOT grow with object count.
        Vector3 color = {
            0.15f + 0.85f * ((i * 3) % 17) / 16.0f,
            0.10f + 0.90f * ((i * 5) % 19) / 18.0f,
            0.15f + 0.85f * ((i * 7) % 23) / 22.0f
        };

        float amplitude = 0.30f + 0.65f * ((i * 11) % 31) / 30.0f;
        float seed = (float)i * 0.731f;

        if (mode == 1 || mode == 3)
        {
            R3D_SetSurfaceEffectUniform(tint, "tint", &color);
            R3D_SetSurfaceEffectUniform(tint, "strength", &strength);
            R3D_PushSurfaceEffect(tint);
        }

        if (mode == 2 || mode == 3)
        {
            R3D_SetSurfaceEffectUniform(glitch, "amplitude", &amplitude);
            R3D_SetSurfaceEffectUniform(glitch, "frequency", &frequency);
            R3D_SetSurfaceEffectUniform(glitch, "seed", &seed);
            R3D_PushSurfaceEffect(glitch);
        }

        draw_object(mesh, material, i, columns, rows, spacing);

        if (mode == 2 || mode == 3)
            R3D_PopSurfaceEffect();

        if (mode == 1 || mode == 3)
            R3D_PopSurfaceEffect();
    }
}

int main(void)
{
    InitWindow(1280, 720, "[r3d] - Surface Effect Benchmark");
    SetTargetFPS(0); // Uncapped benchmark

    R3D_Init(GetScreenWidth(), GetScreenHeight());

    // Enough vertices to see individual triangles explode.
    R3D_Mesh sphere = R3D_GenMeshSphere(0.46f, 24, 24);
    R3D_Mesh plane = R3D_GenMeshPlane(200.0f, 200.0f, 1, 1);

    // We deliberately deform the geometry beyond its original bounds.
    // Providing conservative bounds is the shader developer's responsibility.
    sphere.aabb.min = (Vector3){-1.8f, -1.8f, -1.8f};
    sphere.aabb.max = (Vector3){ 1.8f,  1.8f,  1.8f};

    R3D_Material material = R3D_GetDefaultMaterial();

    R3D_SurfaceEffect* tint = R3D_LoadSurfaceEffect(
        RESOURCES_PATH "shaders/surface_effect_tint.glsl"
    );

    R3D_SurfaceEffect* glitch = R3D_LoadSurfaceEffect(
        RESOURCES_PATH "shaders/surface_effect_glitch.glsl"
    );

    if (!tint || !glitch)
    {
        TraceLog(LOG_ERROR, "Could not load benchmark effects");

        R3D_UnloadSurfaceEffect(tint);
        R3D_UnloadSurfaceEffect(glitch);
        R3D_UnloadMesh(sphere);
        R3D_UnloadMesh(plane);

        R3D_Close();
        CloseWindow();
        return 1;
    }

    R3D_ENVIRONMENT_SET(ambient.color, (Color){55, 55, 55, 255});

    R3D_Light light = R3D_CreateDirLight(
        (Vector3){-0.5f, -1.0f, -0.3f},
        WHITE,
        2.0f
    );

    R3D_ShadowMap shadow = R3D_LoadShadowMap(R3D_LIGHT_DIR);
    shadow.softness = 2.0f;

    Vector3 tintColor = {1.0f, 0.15f, 0.35f};
    float tintStrength = 0.90f;

    float amplitude = 0.85f;
    float frequency = 9.0f;
    float seed = 0.0f;

    int mode = 4;
    int level = 1;
    bool shadows = false;

    float smoothFrameMs = 0.0f;
    float smoothSubmitMs = 0.0f;
    float smoothRenderMs = 0.0f;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_ONE)) mode = 0;
        if (IsKeyPressed(KEY_TWO)) mode = 1;
        if (IsKeyPressed(KEY_THREE)) mode = 2;
        if (IsKeyPressed(KEY_FOUR)) mode = 3;
        if (IsKeyPressed(KEY_FIVE)) mode = 4;
        if (IsKeyPressed(KEY_SIX)) mode = 5;

        if (IsKeyPressed(KEY_UP) && level < BENCH_LEVEL_COUNT - 1)
            level++;

        if (IsKeyPressed(KEY_DOWN) && level > 0)
            level--;

        if (IsKeyPressed(KEY_S))
            shadows = !shadows;

        int count = BENCH_COUNTS[level];
        int columns = (int)ceilf(sqrtf((float)count));
        int rows = (count + columns - 1) / columns;

        const float spacing = 1.65f;
        float span = (float)columns * spacing;

        Camera3D camera = {
            .position = {
                span * 0.26f,
                span * 0.75f,
                span * 0.90f
            },
            .target = {0, 0, 0},
            .up = {0, 1, 0},
            .fovy = 60.0f,
            .projection = CAMERA_PERSPECTIVE
        };

        float frameMs = GetFrameTime() * 1000.0f;

        // These values are identical within each batched scope.
        // A proper submission cache should capture them only once.
        R3D_SetSurfaceEffectUniform(tint, "tint", &tintColor);
        R3D_SetSurfaceEffectUniform(tint, "strength", &tintStrength);

        R3D_SetSurfaceEffectUniform(glitch, "amplitude", &amplitude);
        R3D_SetSurfaceEffectUniform(glitch, "frequency", &frequency);
        R3D_SetSurfaceEffectUniform(glitch, "seed", &seed);

        BeginDrawing();
        ClearBackground((Color){20, 22, 28, 255});

        double submitStart = GetTime();

        R3D_Begin(camera);

        if (shadows)
            R3D_PushLightEx(light, shadow, true);
        else
            R3D_PushLight(light);

        R3D_DrawMesh(
            plane,
            material,
            (Vector3){0, -0.65f, 0},
            1.0f
        );

        switch (mode)
        {
            case 0:
            {
                // Baseline: no surface effects.
                draw_range(sphere, material, 0, count, columns, rows, spacing);
            } break;

            case 1:
            {
                // One tint scope for every object.
                R3D_PushSurfaceEffect(tint);
                draw_range(sphere, material, 0, count, columns, rows, spacing);
                R3D_PopSurfaceEffect();
            } break;

            case 2:
            {
                // One glitch scope for every object.
                R3D_PushSurfaceEffect(glitch);
                draw_range(sphere, material, 0, count, columns, rows, spacing);
                R3D_PopSurfaceEffect();
            } break;

            case 3:
            {
                // Two shaders composed for every object.
                R3D_PushSurfaceEffect(tint);
                R3D_PushSurfaceEffect(glitch);

                draw_range(sphere, material, 0, count, columns, rows, spacing);

                R3D_PopSurfaceEffect();
                R3D_PopSurfaceEffect();
            } break;

            case 4:
            {
                // Four different shader combinations, grouped in batches.
                int q1 = count / 4;
                int q2 = count / 2;
                int q3 = count * 3 / 4;

                // Baseline.
                draw_range(sphere, material, 0, q1, columns, rows, spacing);

                // Tint only.
                R3D_PushSurfaceEffect(tint);
                draw_range(sphere, material, q1, q2, columns, rows, spacing);
                R3D_PopSurfaceEffect();

                // Glitch only.
                R3D_PushSurfaceEffect(glitch);
                draw_range(sphere, material, q2, q3, columns, rows, spacing);
                R3D_PopSurfaceEffect();

                // Tint + glitch.
                R3D_PushSurfaceEffect(tint);
                R3D_PushSurfaceEffect(glitch);
                draw_range(sphere, material, q3, count, columns, rows, spacing);
                R3D_PopSurfaceEffect();
                R3D_PopSurfaceEffect();
            } break;

            case 5:
            {
                // Worst-case stress test:
                // thousands of independent effect scopes and parameter values.
                draw_unique(
                    sphere, material,
                    tint, glitch,
                    count, columns, rows, spacing
                );
            } break;
        }

        double submitEnd = GetTime();

        R3D_End();

        double renderEnd = GetTime();

        float submitMs = (float)((submitEnd - submitStart) * 1000.0);
        float renderMs = (float)((renderEnd - submitEnd) * 1000.0);

        // Lightweight smoothing for a readable benchmark display.
        if (smoothFrameMs <= 0.0f)
        {
            smoothFrameMs = frameMs;
            smoothSubmitMs = submitMs;
            smoothRenderMs = renderMs;
        }
        else
        {
            smoothFrameMs += (frameMs - smoothFrameMs) * 0.08f;
            smoothSubmitMs += (submitMs - smoothSubmitMs) * 0.08f;
            smoothRenderMs += (renderMs - smoothRenderMs) * 0.08f;
        }

        DrawRectangle(0, 0, GetScreenWidth(), 136, Fade(BLACK, 0.78f));

        DrawText("SURFACE EFFECT STRESS TEST", 14, 9, 22, RAYWHITE);
        DrawText(TextFormat("Mode: %s", BENCH_MODES[mode]), 14, 38, 17, YELLOW);
        DrawText(TextFormat(
            "Objects: %d   |   Shadows: %s   |   FPS: %d   |   Frame: %.2f ms",
            count, shadows ? "ON" : "OFF", GetFPS(), smoothFrameMs
        ), 14, 63, 17, RAYWHITE);

        DrawText(TextFormat(
            "CPU submission: %.3f ms   |   R3D_End CPU: %.3f ms",
            smoothSubmitMs, smoothRenderMs
        ), 14, 86, 16, SKYBLUE);

        DrawText(
            "1-6: Mode   |   UP/DOWN: Object count   |   S: Shadows",
            14, 110, 15, LIGHTGRAY
        );

        R3D_DrawRenderStats(14, 145);

        EndDrawing();
    }

    R3D_UnloadSurfaceEffect(glitch);
    R3D_UnloadSurfaceEffect(tint);
    R3D_UnloadShadowMap(shadow);
    R3D_UnloadMesh(sphere);
    R3D_UnloadMesh(plane);

    R3D_Close();
    CloseWindow();

    return 0;
}
