
#include <r3d/r3d.h>
#include <raymath.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

// Same draw path for the plain box and the animated CesiumMan model.
static void draw_subject(bool drawMesh, R3D_Mesh box, R3D_Material material, R3D_Model model, R3D_AnimationPlayer player, float x)
{
    Vector3 position = {x, 0.0f, 0.0f};

    if (drawMesh)
    {
        R3D_DrawMesh(box, material, position, 1.0f);
    }
    else
    {
        R3D_DrawAnimatedModel(model, player, position, 1.0f);
    }
}

int main(void)
{
    InitWindow(1152, 648, "[r3d] - Temporary surface effects");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());

    // Keep the resources and animation setup from draw_immediate.c.
    R3D_Mesh plane = R3D_GenMeshPlane(1000, 1000, 1, 1);
    R3D_Mesh box = R3D_GenMeshCube(0.85f, 0.85f, 0.85f);
    R3D_Model model = R3D_LoadModel(RESOURCES_PATH "models/CesiumMan.glb");
    R3D_Material material = R3D_GetDefaultMaterial();

    R3D_AnimationLib modelAnims = R3D_LoadAnimationLib(RESOURCES_PATH "models/CesiumMan.glb");
    R3D_AnimationPlayer modelPlayer = R3D_LoadAnimationPlayer(model.skeleton, modelAnims);
    R3D_SetAnimationLoop(&modelPlayer, 0, true);
    R3D_PlayAnimation(&modelPlayer, 0);

    // This is loaded from the examples resources, NOT a built-in effect.
    R3D_SurfaceEffect* tint = R3D_LoadSurfaceEffect(RESOURCES_PATH "shaders/surface_effect_tint.glsl");
    if (!tint)
    {
        TraceLog(LOG_ERROR, "Failed to load test surface effect");
        R3D_UnloadAnimationPlayer(modelPlayer);
        R3D_UnloadAnimationLib(modelAnims);
        R3D_UnloadModel(model, true);
        R3D_UnloadMesh(box);
        R3D_UnloadMesh(plane);
        R3D_Close();
        CloseWindow();
        return 1;
    }

    R3D_ENVIRONMENT_SET(ambient.color, (Color){40, 40, 40, 255});

    R3D_Light light = R3D_CreateSpotLight((Vector3){0, 10, 5}, (Vector3){0, -1, -0.5f}, 50.0f, WHITE, 1.0f);
    R3D_ShadowMap shadow = R3D_LoadShadowMap(R3D_LIGHT_SPOT);
    shadow.softness = 4.0f;

    Camera3D camera = {
        .position = {0, 2.5f, 9.0f},
        .target = {0, 0, 0},
        .up = {0, 1, 0},
        .fovy = 60,
        .projection = CAMERA_PERSPECTIVE
    };

    Vector3 red = {1.0f, 0.1f, 0.1f};
    Vector3 blue = {0.15f, 0.25f, 1.0f};
    Vector3 green = {0.1f, 1.0f, 0.1f};
    float redStrength = 0.85f;
    float blueStrength = 0.55f;
    float disabled = 0.0f;
    bool drawMesh = true;

    while (!WindowShouldClose())
    {
        float delta = GetFrameTime();
        UpdateCamera(&camera, CAMERA_ORBITAL);

        if (IsKeyPressed(KEY_SPACE)) drawMesh = !drawMesh;
        R3D_UpdateAnimationPlayer(&modelPlayer, delta);

        BeginDrawing();
            ClearBackground(RAYWHITE);

            R3D_Begin(camera);
                R3D_PushLightEx(light, shadow, true);
                R3D_DrawMesh(plane, material, (Vector3){0, -0.5f, 0}, 1.0f);

                // 1. Ordinary rendering, no effect.
                draw_subject(drawMesh, box, material, model, modelPlayer, -3.0f);

                // 2. Red: parameters must be frozen by Push.
                R3D_SetSurfaceEffectUniform(tint, "tint", &red);
                R3D_SetSurfaceEffectUniform(tint, "strength", &redStrength);
                R3D_PushSurfaceEffect(tint);
                draw_subject(drawMesh, box, material, model, modelPlayer, -1.8f);

                // 3. Red + blue, with the SAME effect resource nested twice.
                // The inner blue settings must not overwrite the outer red settings.
                R3D_SetSurfaceEffectUniform(tint, "tint", &blue);
                R3D_SetSurfaceEffectUniform(tint, "strength", &blueStrength);
                R3D_PushSurfaceEffect(tint);
                draw_subject(drawMesh, box, material, model, modelPlayer, -0.6f);

                // 4. Pop blue: this must still use the original RED snapshot.
                R3D_PopSurfaceEffect();
                draw_subject(drawMesh, box, material, model, modelPlayer, 0.6f);

                // 5. Pop red, then submit blue in a NEW independent scope.
                R3D_PopSurfaceEffect();
                R3D_PushSurfaceEffect(tint);
                draw_subject(drawMesh, box, material, model, modelPlayer, 1.8f);
                R3D_PopSurfaceEffect();

                // 6. Ordinary rendering restored.
                draw_subject(drawMesh, box, material, model, modelPlayer, 3.0f);

                // Deliberately overwrite the effect resource BEFORE R3D_End().
                // Previously submitted draws must retain their captured values.
                R3D_SetSurfaceEffectUniform(tint, "tint", &green);
                R3D_SetSurfaceEffectUniform(tint, "strength", &disabled);
            R3D_End();

            DrawText("SPACE: switch box / animated CesiumMan", 10, 10, 20, BLACK);
            DrawText("DEFAULT  |  RED  |  RED + BLUE  |  RED  |  BLUE  |  DEFAULT", 10, 40, 16, BLACK);
            DrawFPS(GetScreenWidth() - 100, 10);
        EndDrawing();
    }

    R3D_UnloadSurfaceEffect(tint);
    R3D_UnloadShadowMap(shadow);
    R3D_UnloadAnimationPlayer(modelPlayer);
    R3D_UnloadAnimationLib(modelAnims);
    R3D_UnloadModel(model, true);
    R3D_UnloadMesh(box);
    R3D_UnloadMesh(plane);
    R3D_Close();
    CloseWindow();
    return 0;
}
