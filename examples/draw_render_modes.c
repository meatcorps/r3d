#include <r3d/r3d.h>
#include <raymath.h>
#include <math.h>
#include <stdlib.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

typedef enum {
    MODE_DEFAULT,
    MODE_UNLIT,
    MODE_XRAY,
    MODE_OCCLUDED,
    MODE_WIREFRAME,
    MODE_WIREFRAME_OVERLAY,
    MODE_SILHOUETTE,
    MODE_ALPHA,
    MODE_ADDITIVE,
    MODE_MULTIPLY,
    MODE_SCREEN,
    MODE_DOUBLE_SIDED,
    MODE_OVERLAY,
    MODE_DEPTH_ONLY,
    MODE_DITHERED,
    MODE_STENCIL,
    MODE_SHADOW_ONLY,
    MODE_OUTLINE,
    MODE_COUNT
} DemoMode;

static const char* modeNames[MODE_COUNT] = {
    "DEFAULT",
    "UNLIT",
    "X-RAY",
    "OCCLUDED ONLY",
    "WIREFRAME",
    "WIREFRAME OVERLAY",
    "SILHOUETTE",
    "ALPHA BLEND",
    "ADDITIVE",
    "MULTIPLY",
    "SCREEN",
    "DOUBLE-SIDED",
    "OVERLAY",
    "DEPTH ONLY",
    "DITHERED TRANSPARENCY",
    "STENCIL / MASK",
    "SHADOW ONLY",
    "OUTLINE"
};

static const char* modeDescriptions[MODE_COUNT] = {
    "Original PBR materials, lighting and shadows.",
    "The original materials without PBR illumination.",
    "The model remains visible through the foreground obstruction.",
    "Only the portions hidden behind geometry are displayed.",
    "Mesh topology rendered as lines.",
    "Original PBR surface with wireframe lines overlaid.",
    "Solid, unlit surface color.",
    "Standard alpha transparency over the studio background.",
    "Light contributions added to the existing image.",
    "Source and destination colors multiplied together.",
    "Screen blending produces a brighter composite.",
    "Render the back side of a single-sided surface.",
    "Render the subject over existing scene geometry.",
    "The invisible subject writes depth, occluding geometry behind it.",
    "Opaque rendering with a 4x4 screen-space coverage pattern.",
    "The subject is only visible inside the invisible mask.",
    "The invisible subject continues to cast shadows.",
    "A crisp screen-space selection outline with constant pixel thickness."
};

static void setup_environment(void)
{
    R3D_SetAntiAliasingMode(R3D_ANTI_ALIASING_MODE_FXAA);
    R3D_SetAntiAliasingPreset(R3D_ANTI_ALIASING_PRESET_HIGH);

    R3D_ENVIRONMENT_SET(background.color, (Color){9, 14, 24, 255});
    R3D_ENVIRONMENT_SET(background.energy, 1.0f);

    R3D_ENVIRONMENT_SET(ambient.color, (Color){35, 43, 65, 255});
    R3D_ENVIRONMENT_SET(ambient.energy, 0.75f);

    R3D_ENVIRONMENT_SET(ssao.enabled, true);
    R3D_ENVIRONMENT_SET(ssao.sampleCount, 24);
    R3D_ENVIRONMENT_SET(ssao.radius, 0.9f);
    R3D_ENVIRONMENT_SET(ssao.intensity, 0.85f);

    R3D_ENVIRONMENT_SET(bloom.mode, R3D_BLOOM_MIX);
    R3D_ENVIRONMENT_SET(bloom.intensity, 0.055f);
    R3D_ENVIRONMENT_SET(bloom.threshold, 1.0f);
    R3D_ENVIRONMENT_SET(bloom.softThreshold, 0.6f);
    R3D_ENVIRONMENT_SET(bloom.levels, 1);

    R3D_ENVIRONMENT_SET(tonemap.mode, R3D_TONEMAP_ACES);
    R3D_ENVIRONMENT_SET(tonemap.exposure, 0.85f);
    R3D_ENVIRONMENT_SET(tonemap.white, 4.0f);

    R3D_ENVIRONMENT_SET(color.contrast, 1.06f);
    R3D_ENVIRONMENT_SET(color.saturation, 1.08f);

    R3D_ENVIRONMENT_SET(volumetricFog.enabled, true);
    R3D_ENVIRONMENT_SET(volumetricFog.scatteringDensity, 0.08f);
    R3D_ENVIRONMENT_SET(volumetricFog.absortionDensity, 0.03f);
    R3D_ENVIRONMENT_SET(volumetricFog.anisotropy, 0.7f);
    R3D_ENVIRONMENT_SET(volumetricFog.stepSize, 0.2f);
}

static void setup_modes(R3D_BuiltRenderMode modes[MODE_COUNT])
{
    modes[MODE_DEFAULT] = R3D_RenderModeBuildDefault();
    modes[MODE_UNLIT] = R3D_RenderModeBuildUnlit();

    modes[MODE_XRAY] =
        R3D_RenderModeBuildXRay((Color){70, 215, 255, 255}, 0.85f);

    modes[MODE_OCCLUDED] =
        R3D_RenderModeBuildOccludedOnly((Color){255, 110, 170, 255}, 0.95f);

    modes[MODE_WIREFRAME] =
        R3D_RenderModeBuildWireframe((Color){70, 230, 255, 255});

    modes[MODE_WIREFRAME_OVERLAY] =
        R3D_RenderModeBuildWireframeOverlay((Color){255, 180, 70, 255});

    modes[MODE_SILHOUETTE] =
        R3D_RenderModeBuildSilhouette((Color){255, 75, 125, 255});

    modes[MODE_ALPHA] = R3D_RenderModeBuildAlphaBlend();
    modes[MODE_ADDITIVE] = R3D_RenderModeBuildAdditive();
    modes[MODE_MULTIPLY] = R3D_RenderModeBuildMultiply();
    modes[MODE_SCREEN] = R3D_RenderModeBuildScreen();
    modes[MODE_DOUBLE_SIDED] = R3D_RenderModeBuildDoubleSided();
    modes[MODE_OVERLAY] = R3D_RenderModeBuildOverlay();
    modes[MODE_DEPTH_ONLY] = R3D_RenderModeBuildDepthOnly();

    modes[MODE_DITHERED] =
        R3D_RenderModeBuildDitheredTransparency(0.45f);

    modes[MODE_STENCIL] = R3D_RenderModeBuildStencilTest(0x01);
    modes[MODE_SHADOW_ONLY] = R3D_RenderModeBuildShadowOnly();

    modes[MODE_OUTLINE] = R3D_RenderModeBuildOutline(
        (Color){65, 220, 255, 255},
        4.0f,
        false
    );
}

static void draw_studio(R3D_Mesh floorMesh, R3D_Mesh pedestalMesh,
                        R3D_Mesh trimMesh, R3D_Mesh backdropMesh,
                        R3D_Material floorMat, R3D_Material pedestalMat,
                        R3D_Material cyanMat, R3D_Material orangeMat,
                        R3D_Material backdropMat)
{
    R3D_DrawMesh(floorMesh, floorMat,
                 (Vector3){0.0f, -1.52f, 0.0f}, 1.0f);

    R3D_DrawMesh(backdropMesh, backdropMat,
                 (Vector3){0.0f, 0.1f, -3.6f}, 1.0f);

    R3D_DrawMesh(pedestalMesh, pedestalMat,
                 (Vector3){0.0f, -1.31f, 0.0f}, 1.0f);

    // Small illuminated architectural accents.
    R3D_DrawMesh(trimMesh, cyanMat,
                 (Vector3){-3.25f, 0.2f, -3.25f}, 1.0f);

    R3D_DrawMesh(trimMesh, orangeMat,
                 (Vector3){3.25f, 0.2f, -3.25f}, 1.0f);
}

static void draw_hud(int index, bool rotating, bool blockerEnabled)
{
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();

    DrawRectangle(0, 0, width, 122, (Color){5, 9, 18, 225});
    DrawRectangle(0, 122, width, 2, (Color){35, 160, 205, 210});

    DrawText("R3D / BUILT-IN RENDER MODES", 35, 19, 20,
             (Color){105, 210, 240, 255});

    DrawText(modeNames[index], 35, 49, 34, RAYWHITE);

    DrawText(modeDescriptions[index], 36, 96, 17,
             (Color){185, 197, 210, 255});

    DrawText(TextFormat("%02d / %02d", index + 1, MODE_COUNT),
             width - 175, 42, 25, (Color){110, 215, 240, 255});

    DrawRectangle(0, height - 58, width, 58,
                  (Color){5, 9, 18, 230});

    DrawText("LEFT / RIGHT  MODE", 35, height - 38, 17, RAYWHITE);

    DrawText(rotating ? "SPACE  PAUSE ROTATION" : "SPACE  RESUME ROTATION",
             290, height - 38, 17, LIGHTGRAY);

    DrawText(blockerEnabled ? "B  OBSTRUCTION ON" : "B  OBSTRUCTION OFF",
             620, height - 38, 17, LIGHTGRAY);

    DrawFPS(width - 110, height - 38);
}

int main(void)
{
    const int screenWidth = 1440;
    const int screenHeight = 810;

    InitWindow(screenWidth, screenHeight,
               "[r3d] Cinematic Built-in Render Modes");

    SetTargetFPS(60);

    if (!R3D_Init(screenWidth, screenHeight))
    {
        CloseWindow();
        return 1;
    }

    setup_environment();
    R3D_SetTextureFilter(TEXTURE_FILTER_ANISOTROPIC_4X);

    // HDR panorama used for reflections, not as the visible backdrop.
    R3D_Cubemap hdr = R3D_LoadCubemap(
        RESOURCES_PATH "panorama/indoor.hdr",
        R3D_CUBEMAP_LAYOUT_AUTO_DETECT
    );

    R3D_AmbientMap ambientMap = R3D_GenAmbientMap(
        hdr,
        R3D_AMBIENT_ILLUMINATION | R3D_AMBIENT_REFLECTION
    );

    R3D_ENVIRONMENT_SET(ambient.map, ambientMap);

    // Main PBR subject.
    R3D_Model helmet =
        R3D_LoadModel(RESOURCES_PATH "models/DamagedHelmet.glb");

    if (!helmet.meshes || !helmet.materials || helmet.materialCount <= 0)
    {
        TraceLog(LOG_ERROR, "Failed to load DamagedHelmet.glb");
        R3D_UnloadAmbientMap(ambientMap);
        R3D_UnloadCubemap(hdr);
        R3D_Close();
        CloseWindow();
        return 1;
    }

    // A second material array for alpha-based demonstrations.
    // The mesh and textures remain owned by the original helmet.
    R3D_Material* fadedMaterials = malloc(
        (size_t)helmet.materialCount * sizeof(R3D_Material)
    );

    if (!fadedMaterials)
    {
        TraceLog(LOG_ERROR, "Failed to allocate demonstration materials");
        R3D_UnloadModel(helmet, true);
        R3D_UnloadAmbientMap(ambientMap);
        R3D_UnloadCubemap(hdr);
        R3D_Close();
        CloseWindow();
        return 1;
    }

    for (int i = 0; i < helmet.materialCount; i++)
    {
        fadedMaterials[i] = helmet.materials[i];
        fadedMaterials[i].albedo.color.a = 155;
    }

    R3D_Model fadedHelmet = helmet;
    fadedHelmet.materials = fadedMaterials;

    // Studio geometry.
    R3D_Mesh floorMesh = R3D_GenMeshPlane(20.0f, 20.0f, 1, 1);
    R3D_Mesh pedestal = R3D_GenMeshCube(2.65f, 0.40f, 2.65f);
    R3D_Mesh backdrop = R3D_GenMeshCube(8.0f, 5.0f, 0.2f);
    R3D_Mesh trim = R3D_GenMeshCube(0.08f, 3.25f, 0.08f);
    R3D_Mesh blocker = R3D_GenMeshCube(0.85f, 2.55f, 0.18f);

    // Facing away from the camera: suitable for Double-Sided testing.
    R3D_Mesh backFacingPanel = R3D_GenMeshQuad(
        2.0f, 2.0f, 1, 1, (Vector3){0.0f, 0.0f, -1.0f}
    );

    R3D_Mesh stencilSphere = R3D_GenMeshSphere(0.95f, 32, 32);

    R3D_Material floorMat = R3D_GetDefaultMaterial();
    floorMat.albedo.color = (Color){48, 54, 66, 255};
    floorMat.orm.roughness = 0.72f;
    floorMat.orm.metalness = 0.12f;

    R3D_Material pedestalMat = R3D_GetDefaultMaterial();
    pedestalMat.albedo.color = (Color){85, 92, 109, 255};
    pedestalMat.orm.roughness = 0.28f;
    pedestalMat.orm.metalness = 0.80f;

    R3D_Material backdropMat = R3D_GetDefaultMaterial();
    backdropMat.albedo.color = (Color){17, 25, 40, 255};
    backdropMat.orm.roughness = 0.92f;

    R3D_Material cyanMat = R3D_GetDefaultMaterial();
    cyanMat.albedo.color = (Color){20, 80, 110, 255};
    cyanMat.emission.color = (Color){40, 180, 255, 255};
    cyanMat.emission.energy = 8.7f;
    cyanMat.orm.roughness = 0.25f;

    R3D_Material orangeMat = R3D_GetDefaultMaterial();
    orangeMat.albedo.color = (Color){130, 65, 20, 255};
    orangeMat.emission.color = (Color){255, 125, 45, 255};
    orangeMat.emission.energy = 8.2f;

    R3D_Material blockerMat = R3D_GetDefaultMaterial();
    blockerMat.albedo.color = (Color){115, 130, 148, 255};
    blockerMat.orm.metalness = 0.75f;
    blockerMat.orm.roughness = 0.3f;

    R3D_Material panelMat = R3D_GetDefaultMaterial();
    panelMat.albedo.color = (Color){65, 180, 210, 255};
    panelMat.orm.roughness = 0.45f;
    panelMat.emission.color = (Color){35, 110, 150, 255};
    panelMat.emission.energy = 0.5f;

    R3D_Material maskMat = R3D_GetDefaultMaterial();

    // Cinematic three-point lighting.
    R3D_Light keyLight = R3D_CreateSpotLight(
        (Vector3){-3.5f, 5.6f, 4.5f},
        (Vector3){3.5f, -5.8f, -4.5f},
        18.0f,
        (Color){255, 215, 177, 255},
        2.6f
    );

    R3D_ShadowMap keyShadow = R3D_LoadShadowMap(R3D_LIGHT_SPOT);
    keyShadow.softness = 5.0f;

    R3D_Light fillLight = R3D_CreateOmniLight(
        (Vector3){3.1f, 1.6f, 1.8f},
        9.0f,
        (Color){65, 155, 255, 255},
        1.75f
    );

    R3D_Light rimLight = R3D_CreateSpotLight(
        (Vector3){1.8f, 3.5f, -2.6f},
        (Vector3){-1.8f, -3.5f, 2.6f},
        12.0f,
        (Color){255, 90, 140, 255},
        2.2f
    );

    R3D_BuiltRenderMode modes[MODE_COUNT] = {0};
    setup_modes(modes);

    // Mask writer is a separate reusable builder.
    R3D_BuiltRenderMode stencilWriter =
        R3D_RenderModeBuildStencilMask(0x01);

    Camera3D camera = {
        .position = {2.1f, 1.8f, 5.8f},
        .target = {0.0f, -0.12f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 44.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    int selected = MODE_DEFAULT;
    bool rotating = true;
    bool blockerEnabled = true;
    float rotation = 0.0f;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_RIGHT))
            selected = (selected + 1) % MODE_COUNT;

        if (IsKeyPressed(KEY_LEFT))
            selected = (selected + MODE_COUNT - 1) % MODE_COUNT;

        if (IsKeyPressed(KEY_SPACE))
            rotating = !rotating;

        if (IsKeyPressed(KEY_B))
            blockerEnabled = !blockerEnabled;

        if (rotating)
            rotation += GetFrameTime() * 0.3f;

        // Pause rotation in comparisons where a stable shape is useful.
        const float angle = selected == MODE_DOUBLE_SIDED ? 0.0f : rotation;
        Matrix subjectTransform = MatrixMultiply(
            MatrixRotateY(angle),
            MatrixTranslate(0.0f, -0.08f, 0.0f)
        );

        const bool showBlocker =
            blockerEnabled &&
            (selected == MODE_XRAY || selected == MODE_OCCLUDED);

        BeginDrawing();
        ClearBackground((Color){7, 11, 19, 255});

        R3D_Begin(camera);

        R3D_PushLightEx(keyLight, keyShadow, true);
        R3D_PushLight(fillLight);
        R3D_PushLight(rimLight);

        draw_studio(
            floorMesh, pedestal, trim, backdrop,
            floorMat, pedestalMat, cyanMat,
            orangeMat, backdropMat
        );

        if (showBlocker)
        {
            // In front of the helmet, to demonstrate depth comparisons.
            R3D_DrawMesh(
                blocker, blockerMat,
                (Vector3){0.15f, -0.1f, 1.15f},
                1.0f
            );
        }

        if (selected == MODE_STENCIL)
        {
            // Invisible stencil volume, processed before its reader.
            R3D_PushBuiltRenderMode(stencilWriter);

            R3D_DrawMesh(
                stencilSphere, maskMat,
                (Vector3){0.0f, 0.0f, 0.95f},
                1.0f
            );

            R3D_PopBuiltRenderMode();
        }

        R3D_PushBuiltRenderMode(modes[selected]);

        if (selected == MODE_DOUBLE_SIDED)
        {
            // Open, single-sided geometry viewed from its back.
            R3D_DrawMesh(
                backFacingPanel, panelMat,
                (Vector3){0.0f, 0.0f, 0.0f},
                1.0f
            );
        }
        else if (selected == MODE_ALPHA ||
                 selected == MODE_ADDITIVE ||
                 selected == MODE_MULTIPLY ||
                 selected == MODE_SCREEN)
        {
            R3D_DrawModelPro(fadedHelmet, subjectTransform);
        }
        else
        {
            R3D_DrawModelPro(helmet, subjectTransform);
        }

        R3D_PopBuiltRenderMode();

        R3D_End();

        draw_hud(selected, rotating, blockerEnabled);

        EndDrawing();
    }

    // All builder-owned resources are released here.
    for (int i = 0; i < MODE_COUNT; i++)
    {
        R3D_UnloadBuiltRenderMode(&modes[i]);
    }

    R3D_UnloadBuiltRenderMode(&stencilWriter);

    R3D_UnloadShadowMap(keyShadow);

    R3D_UnloadMesh(stencilSphere);
    R3D_UnloadMesh(backFacingPanel);
    R3D_UnloadMesh(blocker);
    R3D_UnloadMesh(trim);
    R3D_UnloadMesh(backdrop);
    R3D_UnloadMesh(pedestal);
    R3D_UnloadMesh(floorMesh);

    free(fadedMaterials);
    R3D_UnloadModel(helmet, true);

    R3D_UnloadAmbientMap(ambientMap);
    R3D_UnloadCubemap(hdr);

    R3D_Close();
    CloseWindow();

    return 0;
}