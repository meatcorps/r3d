
#include <r3d/r3d.h>
#include <raymath.h>
#include <math.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

#define OUTPUT_WIDTH 1920
#define OUTPUT_HEIGHT 1080

typedef enum CompareMode {
    COMPARE_NATIVE,
    COMPARE_LINEAR,
    COMPARE_LANCZOS,
    COMPARE_FSR1,
    COMPARE_MODE_COUNT
} CompareMode;

static const char* MODE_NAMES[] = {
    "Native",
    "Linear",
    "Lanczos",
    "FSR1"
};

static const float RENDER_SCALES[] = {
    0.50f,
    0.60f,
    0.67f,
    0.75f,
    0.85f,
    1.00f
};

#define SCALE_COUNT (sizeof(RENDER_SCALES) / sizeof(RENDER_SCALES[0]))

static const char* get_aa_name(R3D_AntiAliasingMode mode)
{
    switch (mode)
    {
        case R3D_ANTI_ALIASING_MODE_NONE: return "None";
        case R3D_ANTI_ALIASING_MODE_FXAA: return "FXAA";
        case R3D_ANTI_ALIASING_MODE_SMAA: return "SMAA";
        default: return "Unknown";
    }
}

static void calculate_render_size(int outputWidth, int outputHeight, CompareMode mode, int scaleIndex, int* width, int* height)
{
    if (mode == COMPARE_NATIVE)
    {
        *width = outputWidth;
        *height = outputHeight;
        return;
    }

    // Preserve a 16:9 internal aspect ratio.
    // This allows Keep/Expand to be compared when the window
    // is resized to a different aspect ratio.
    float fitWidth = (float)outputWidth;
    float fitHeight = fitWidth * 9.0f / 16.0f;

    if (fitHeight > (float)outputHeight)
    {
        fitHeight = (float)outputHeight;
        fitWidth = fitHeight * 16.0f / 9.0f;
    }

    float scale = RENDER_SCALES[scaleIndex];

    *width = (int)(fitWidth * scale + 0.5f);
    *height = (int)(fitHeight * scale + 0.5f);

    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
}

static void update_render_resolution(CompareMode mode, int scaleIndex)
{
    int outputWidth = GetRenderWidth();
    int outputHeight = GetRenderHeight();

    if (outputWidth <= 0 || outputHeight <= 0) return;

    int desiredWidth;
    int desiredHeight;

    calculate_render_size(
        outputWidth,
        outputHeight,
        mode,
        scaleIndex,
        &desiredWidth,
        &desiredHeight
    );

    int currentWidth;
    int currentHeight;

    R3D_GetResolution(&currentWidth, &currentHeight);

    if (currentWidth != desiredWidth || currentHeight != desiredHeight)
    {
        R3D_SetResolution(desiredWidth, desiredHeight);
    }
}

static void apply_upscale_mode(CompareMode mode)
{
    switch (mode)
    {
        case COMPARE_NATIVE:
            R3D_SetUpscaleMode(R3D_UPSCALE_NEAREST);
            break;

        case COMPARE_LINEAR:
            R3D_SetUpscaleMode(R3D_UPSCALE_LINEAR);
            break;

        case COMPARE_LANCZOS:
            R3D_SetUpscaleMode(R3D_UPSCALE_LANCZOS);
            break;

        case COMPARE_FSR1:
            R3D_SetUpscaleMode(R3D_UPSCALE_FSR1);
            break;

        default:
            break;
    }
}

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(OUTPUT_WIDTH, OUTPUT_HEIGHT, "[r3d] - FSR1 Comparison");

    // Uncapped FPS makes performance differences easier to see.
    SetTargetFPS(0);

    CompareMode mode = COMPARE_FSR1;
    int scaleIndex = 3; // 75%

    R3D_AspectMode aspectMode = R3D_ASPECT_KEEP;
    R3D_AntiAliasingMode aaMode = R3D_ANTI_ALIASING_MODE_FXAA;

    int initialWidth;
    int initialHeight;

    calculate_render_size(
        GetRenderWidth(),
        GetRenderHeight(),
        mode,
        scaleIndex,
        &initialWidth,
        &initialHeight
    );

    if (!R3D_Init(initialWidth, initialHeight))
    {
        CloseWindow();
        return 1;
    }

    R3D_SetAspectMode(aspectMode);
    R3D_SetAntiAliasingMode(aaMode);
    R3D_SetFsr1Sharpness(0.5f);

    apply_upscale_mode(mode);

    // ----------------------------
    // HDR environment
    // ----------------------------

    R3D_Cubemap cubemap = R3D_LoadCubemap(
        RESOURCES_PATH "panorama/indoor.hdr",
        R3D_CUBEMAP_LAYOUT_AUTO_DETECT
    );

    R3D_ENVIRONMENT_SET(background.sky, cubemap);
    R3D_ENVIRONMENT_SET(background.skyBlur, 0.3f);
    R3D_ENVIRONMENT_SET(background.energy, 0.6f);

    R3D_AmbientMap ambientMap = R3D_GenAmbientMap(
        cubemap,
        R3D_AMBIENT_ILLUMINATION
    );

    R3D_ENVIRONMENT_SET(ambient.map, ambientMap);
    R3D_ENVIRONMENT_SET(ambient.energy, 0.35f);

    R3D_ENVIRONMENT_SET(tonemap.mode, R3D_TONEMAP_FILMIC);
    R3D_ENVIRONMENT_SET(tonemap.exposure, 0.75f);

    // ----------------------------
    // Scene resources
    // ----------------------------

    R3D_Mesh plane = R3D_GenMeshPlane(14, 14, 1, 1);
    R3D_Mesh detailMesh = R3D_GenMeshCube(0.08f, 0.8f, 0.08f);

    R3D_Material floorMaterial = R3D_GetDefaultMaterial();
    floorMaterial.albedo.color = (Color){165, 175, 185, 255};
    floorMaterial.orm.roughness = 0.85f;

    R3D_Material detailMaterial = R3D_GetDefaultMaterial();
    detailMaterial.albedo.color = (Color){230, 105, 70, 255};
    detailMaterial.orm.roughness = 0.35f;
    detailMaterial.orm.metalness = 0.6f;

    R3D_Model model = R3D_LoadModel(
        RESOURCES_PATH "models/CesiumMan.glb"
    );

    R3D_AnimationLib animations = R3D_LoadAnimationLib(
        RESOURCES_PATH "models/CesiumMan.glb"
    );

    R3D_AnimationPlayer player = R3D_LoadAnimationPlayer(
        model.skeleton,
        animations
    );

    if (animations.count > 0)
    {
        R3D_SetAnimationLoop(&player, 0, true);
        R3D_PlayAnimation(&player, 0);
    }

    // ----------------------------
    // Lighting and shadows
    // ----------------------------

    R3D_Light light = R3D_CreateDirLight(
        (Vector3){-1, -1, -1},
        WHITE,
        1.5f
    );

    light.range = 30.0f;

    R3D_ShadowMap shadow = R3D_LoadShadowMap(R3D_LIGHT_DIR);
    shadow.softness = 2.0f;

    // ----------------------------
    // Camera
    // ----------------------------

    Camera3D camera = {
        .position = {0, 2.6f, 6.5f},
        .target = {0, 1.0f, 0},
        .up = {0, 1, 0},
        .fovy = 55,
        .projection = CAMERA_PERSPECTIVE
    };

    // ----------------------------
    // Frame statistics
    // ----------------------------

    float accumulatedTime = 0.0f;
    int accumulatedFrames = 0;

    float averageFps = 0.0f;
    float averageFrameMs = 0.0f;

    int previousOutputWidth = GetRenderWidth();
    int previousOutputHeight = GetRenderHeight();

    // ----------------------------
    // Main loop
    // ----------------------------

    while (!WindowShouldClose())
    {
        float delta = GetFrameTime();

        accumulatedTime += delta;
        accumulatedFrames++;

        if (accumulatedTime >= 0.5f)
        {
            averageFps = accumulatedFrames / accumulatedTime;
            averageFrameMs = accumulatedTime * 1000.0f / accumulatedFrames;

            accumulatedTime = 0.0f;
            accumulatedFrames = 0;
        }

        bool resolutionChanged = false;

        // Switch comparison mode.
        if (IsKeyPressed(KEY_RIGHT))
        {
            mode = (CompareMode)((mode + 1) % COMPARE_MODE_COUNT);
            resolutionChanged = true;
            apply_upscale_mode(mode);
        }

        if (IsKeyPressed(KEY_LEFT))
        {
            mode = (CompareMode)((mode + COMPARE_MODE_COUNT - 1) % COMPARE_MODE_COUNT);
            resolutionChanged = true;
            apply_upscale_mode(mode);
        }

        // Increase render resolution.
        if (IsKeyPressed(KEY_UP) && scaleIndex < (int)SCALE_COUNT - 1)
        {
            scaleIndex++;
            resolutionChanged = true;
        }

        // Decrease render resolution.
        if (IsKeyPressed(KEY_DOWN) && scaleIndex > 0)
        {
            scaleIndex--;
            resolutionChanged = true;
        }

        // Toggle aspect handling.
        if (IsKeyPressed(KEY_SPACE))
        {
            aspectMode = aspectMode == R3D_ASPECT_KEEP
                ? R3D_ASPECT_EXPAND
                : R3D_ASPECT_KEEP;

            R3D_SetAspectMode(aspectMode);
        }

        // Optional: cycle anti-aliasing for comparison.
        if (IsKeyPressed(KEY_A))
        {
            aaMode = (R3D_AntiAliasingMode)((aaMode + 1) % 3);
            R3D_SetAntiAliasingMode(aaMode);
        }

        // Optional: adjust RCAS sharpening.
        if (IsKeyPressed(KEY_MINUS))
        {
            R3D_SetFsr1Sharpness(
                R3D_GetFsr1Sharpness() - 0.1f
            );
        }

        if (IsKeyPressed(KEY_EQUAL))
        {
            R3D_SetFsr1Sharpness(
                R3D_GetFsr1Sharpness() + 0.1f
            );
        }

        // Handle window resizing.
        int outputWidth = GetRenderWidth();
        int outputHeight = GetRenderHeight();

        if (outputWidth != previousOutputWidth ||
            outputHeight != previousOutputHeight)
        {
            previousOutputWidth = outputWidth;
            previousOutputHeight = outputHeight;
            resolutionChanged = true;
        }

        if (resolutionChanged)
        {
            update_render_resolution(mode, scaleIndex);
        }

        // Keep the camera stationary to make image-quality
        // comparisons easier while the models animate.
        if (animations.count > 0)
        {
            R3D_UpdateAnimationPlayer(&player, delta);
        }

        // ----------------------------
        // Render
        // ----------------------------

        BeginDrawing();

            // Also clears the letterbox/pillarbox areas.
            ClearBackground(BLACK);

            R3D_Begin(camera);

                R3D_PushLightEx(light, shadow, true);

                R3D_DrawMesh(
                    plane,
                    floorMaterial,
                    Vector3Zero(),
                    1.0f
                );

                // Three animated characters.
                // Reusing the same animation keeps this example simple.
                for (int i = -1; i <= 1; i++)
                {
                    Matrix scale = MatrixScale(1.15f, 1.15f, 1.15f);
                    Matrix translation = MatrixTranslate(
                        (float)i * 1.8f, 0.0f, 0.0f
                    );

                    Matrix transform = MatrixMultiply(scale, translation);

                    R3D_DrawAnimatedModelPro(
                        model,
                        player,
                        transform
                    );
                }

                // Thin contrasting details to help inspect
                // edge reconstruction and sharpening.
                for (int i = 0; i < 13; i++)
                {
                    float x = ((float)i - 6.0f) * 0.45f;

                    R3D_DrawMesh(
                        detailMesh,
                        detailMaterial,
                        (Vector3){x, 0.4f, -2.2f},
                        1.0f
                    );
                }

            R3D_End();

            // ----------------------------
            // Overlay: drawn after R3D
            // ----------------------------

            int renderWidth;
            int renderHeight;

            R3D_GetResolution(&renderWidth, &renderHeight);

            DrawRectangle(
                8, 8, 630, 166,
                Fade(BLACK, 0.78f)
            );

            DrawText(
                TextFormat(
                    "Mode: %s   |   Scale preset: %.0f%%",
                    MODE_NAMES[mode],
                    RENDER_SCALES[scaleIndex] * 100.0f
                ),
                18, 16, 20, WHITE
            );

            DrawText(
                TextFormat(
                    "Internal: %dx%d   |   Output: %dx%d",
                    renderWidth, renderHeight,
                    outputWidth, outputHeight
                ),
                18, 42, 18, RAYWHITE
            );

            DrawText(
                TextFormat(
                    "FPS: %.1f   |   Frame: %.2f ms",
                    averageFps,
                    averageFrameMs
                ),
                18, 66, 18, YELLOW
            );

            DrawText(
                TextFormat(
                    "Aspect: %s   |   AA: %s   |   RCAS: %.2f",
                    aspectMode == R3D_ASPECT_KEEP ? "Keep" : "Expand",
                    get_aa_name(aaMode),
                    R3D_GetFsr1Sharpness()
                ),
                18, 90, 18, RAYWHITE
            );

            DrawText(
                "LEFT/RIGHT: Mode     UP/DOWN: Scale     SPACE: Aspect",
                18, 116, 16, LIGHTGRAY
            );

            DrawText(
                "A: AA     - / =: RCAS sharpness",
                18, 140, 16, LIGHTGRAY
            );

            if (mode == COMPARE_NATIVE)
            {
                DrawText(
                    "Native mode: internal resolution matches output",
                    18, 183, 18, GREEN
                );
            }
            else if (mode == COMPARE_FSR1)
            {
                DrawText(
                    "FSR1: EASU + RCAS when upscaling is required",
                    18, 183, 18, GREEN
                );
            }

        EndDrawing();
    }

    // ----------------------------
    // Cleanup
    // ----------------------------

    R3D_UnloadAnimationPlayer(player);
    R3D_UnloadAnimationLib(animations);
    R3D_UnloadModel(model, true);

    R3D_UnloadMesh(detailMesh);
    R3D_UnloadMesh(plane);

    R3D_UnloadShadowMap(shadow);
    R3D_UnloadAmbientMap(ambientMap);
    R3D_UnloadCubemap(cubemap);

    R3D_Close();
    CloseWindow();

    return 0;
}
