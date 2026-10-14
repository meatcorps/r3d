#include <r3d/r3d.h>
#include <raymath.h>

#include "r3d/r3d_texture.h"

int main(void) {
    // Initialize window
    InitWindow(1152, 648, "[r3d] - Basic example");
    SetTargetFPS(60);

    // Initialize R3D
    R3D_Init(GetScreenWidth(), GetScreenHeight());

    // Create meshes
    R3D_Mesh plane = R3D_GenMeshPlane(1000, 1000, 1, 1);
    R3D_Mesh box = R3D_GenMeshCube(0.5f, 0.5f, 0.5f);

    R3D_Model model = R3D_LoadModel(RESOURCES_PATH "models/CesiumMan.glb");
    R3D_Material material = R3D_GetDefaultMaterial();

    R3D_AnimationLib modelAnims = R3D_LoadAnimationLib(RESOURCES_PATH "models/CesiumMan.glb");
    R3D_AnimationPlayer modelPlayer = R3D_LoadAnimationPlayer(model.skeleton, modelAnims);

    R3D_RenderMode unlit = {0};
    unlit.overrideMask = R3D_RENDER_MODE_OVERRIDE_UNLIT;
    unlit.unlit = true;

    R3D_SetAnimationLoop(&modelPlayer, 0, true);
    R3D_PlayAnimation(&modelPlayer, 0);

    // Setup environment
    R3D_ENVIRONMENT_SET(ambient.color, (Color){40, 40, 40, 255});

    // Create light
    R3D_Light light = R3D_CreateSpotLight((Vector3){0, 10, 5}, (Vector3){0, -1, -0.5f}, 50.0f, WHITE, 1.0f);
    R3D_ShadowMap shadow = R3D_LoadShadowMap(R3D_LIGHT_SPOT);
    shadow.softness = 4.0f;

    R3D_RenderMode additive = {0};
    additive.overrideMask =
            R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
            R3D_RENDER_MODE_OVERRIDE_BLEND;

    additive.transparencyMode = R3D_TRANSPARENCY_BLEND;
    additive.blendMode = R3D_BLEND_ADDITIVE;

    R3D_RenderMode doubleSided = {0};
    doubleSided.overrideMask = R3D_RENDER_MODE_OVERRIDE_CULL;
    doubleSided.cullMode = R3D_CULL_NONE;

    R3D_SurfaceShader *materialShader =
            R3D_LoadSurfaceShader(RESOURCES_PATH "shaders/surface_effect_compat_a.glsl");

    material.shader = materialShader;

    R3D_SurfaceEffect *effectA =
            R3D_LoadSurfaceEffect(RESOURCES_PATH "shaders/surface_effect_compat_a.glsl");

    R3D_SurfaceEffect *effectB =
            R3D_LoadSurfaceEffect(RESOURCES_PATH "shaders/surface_effect_compat_b.glsl");

    Image whiteImage = GenImageColor(1, 1, WHITE);
    Image redImage = GenImageColor(1, 1, RED);
    Image blueImage = GenImageColor(1, 1, BLUE);

    Texture2D whiteTexture = LoadTextureFromImage(whiteImage);
    Texture2D redTexture = LoadTextureFromImage(redImage);
    Texture2D blueTexture = LoadTextureFromImage(blueImage);

    UnloadImage(whiteImage);
    UnloadImage(redImage);
    UnloadImage(blueImage);

    R3D_SetSurfaceShaderSampler(materialShader, "noiseTexture", whiteTexture);
    R3D_SetSurfaceEffectSampler(effectA, "noiseTexture", redTexture);
    R3D_SetSurfaceEffectSampler(effectB, "noiseTexture", blueTexture);

    float effectStrength = 0.5f;
    R3D_SetSurfaceEffectUniform(effectA, "strength", &effectStrength);
    R3D_SetSurfaceEffectUniform(effectB, "strength", &effectStrength);

    // Setup camera
    Camera3D camera = {
        .position = {0, 2, 3},
        .target = {0, 0, 0},
        .up = {0, 1, 0},
        .fovy = 60
    };

    bool drawMesh = true;
    bool useCompatibleShader = false;

    // Main loop
    while (!WindowShouldClose()) {
        float delta = GetFrameTime();

        UpdateCamera(&camera, CAMERA_ORBITAL);

        if (IsKeyPressed(KEY_SPACE)) {
            drawMesh = !drawMesh;
        }

        if (IsKeyPressed(KEY_A)) {
            useCompatibleShader = !useCompatibleShader;
        }

        R3D_UpdateAnimationPlayer(&modelPlayer, delta);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        R3D_Begin(camera);
        R3D_PushLightEx(light, shadow, true);
        R3D_DrawMesh(plane, material, (Vector3){0, -0.5f, 0}, 1.0f);

        if (useCompatibleShader) {
            R3D_PushRenderMode(unlit);
            // 1. Original material, no temporary effects.
            R3D_DrawMesh(box, material, (Vector3){-2, 0, 0}, 1.0f);

            // 2. First occurrence: RED texture.
            R3D_SetSurfaceEffectSampler(effectA, "noiseTexture", redTexture);
            R3D_PushSurfaceEffect(effectA);
            R3D_DrawMesh(box, material, (Vector3){-1, 0, 0}, 1.0f);

            // 3. Push the SAME effect again, now using BLUE.
            // The outer RED snapshot must remain unchanged.
            R3D_SetSurfaceEffectSampler(effectA, "noiseTexture", blueTexture);
            R3D_PushSurfaceEffect(effectA);
            R3D_DrawMesh(box, material, (Vector3){0, 0, 0}, 1.0f);

            // 4. Pop BLUE. The original RED must be restored.
            R3D_PopSurfaceEffect();
            R3D_DrawMesh(box, material, (Vector3){1, 0, 0}, 1.0f);

            // 5. Pop RED. Return to the original material.
            R3D_PopSurfaceEffect();
            R3D_DrawMesh(box, material, (Vector3){2, 0, 0}, 1.0f);

            // Deliberately modify the effect again BEFORE R3D_End().
            // Already submitted objects must retain their textures.
            R3D_SetSurfaceEffectSampler(effectA, "noiseTexture", whiteTexture);
            R3D_PopRenderMode();
        } else if (drawMesh) {
            // Original appearance.
            R3D_DrawMesh(box, material, (Vector3){0, 0, 0}, 1.0f);

            // Additive.
            R3D_PushRenderMode(additive);
            R3D_DrawMesh(box, material, (Vector3){-1, 0, 0}, 1.0f);

            // Additive + double-sided.
            R3D_PushRenderMode(doubleSided);
            R3D_DrawMesh(box, material, (Vector3){1, 0, 0}, 1.0f);

            // Back to additive.
            R3D_PopRenderMode();
            R3D_DrawMesh(box, material, (Vector3){2, 0, 0}, 1.0f);

            // Back to original.
            R3D_PopRenderMode();
            R3D_DrawMesh(box, material, (Vector3){3, 0, 0}, 1.0f);
        } else {
            // Original appearance.
            R3D_DrawAnimatedModel(model, modelPlayer, (Vector3){0, 0, 0}, 1.0f);

            // Additive.
            R3D_PushRenderMode(additive);
            R3D_DrawAnimatedModel(model, modelPlayer, (Vector3){-1, 0, 0}, 1.0f);

            // Additive + double-sided.
            R3D_PushRenderMode(doubleSided);
            R3D_DrawAnimatedModel(model, modelPlayer, (Vector3){1, 0, 0}, 1.0f);

            // Back to additive.
            R3D_PopRenderMode();
            R3D_DrawAnimatedModel(model, modelPlayer, (Vector3){2, 0, 0}, 1.0f);

            // Back to original.
            R3D_PopRenderMode();
            R3D_DrawAnimatedModel(model, modelPlayer, (Vector3){3, 0, 0}, 1.0f);
        }

        R3D_End();

        DrawText("Press SPACE to toggle mesh / model drawing", 10, 10, 20, BLACK);
        DrawText(useCompatibleShader ? "Effects: ON" : "Effects: OFF", 10, 40, 20, BLACK);
        EndDrawing();
    }

    // Cleanup
    R3D_UnloadMesh(box);
    R3D_UnloadMesh(plane);
    R3D_UnloadModel(model, true);
    R3D_UnloadAnimationLib(modelAnims);
    R3D_UnloadSurfaceShader(materialShader);
    R3D_UnloadSurfaceEffect(effectA);
    R3D_UnloadSurfaceEffect(effectB);
    R3D_UnloadShadowMap(shadow);
    UnloadTexture(whiteTexture);
    UnloadTexture(redTexture);
    UnloadTexture(blueTexture);
    R3D_Close();

    CloseWindow();

    return 0;
}
