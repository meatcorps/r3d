#include <r3d/r3d.h>
#include <raymath.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

int main(void)
{
    InitWindow(1152, 648, "[r3d] - Effect shadow validation");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());

    R3D_Mesh ground = R3D_GenMeshPlane(20.0f, 20.0f, 1, 1);
    R3D_Mesh cube = R3D_GenMeshCube(1.5f, 1.5f, 1.5f);
    R3D_Material material = R3D_GetDefaultMaterial();

    // The effect moves the vertices beyond their original bounds.
    cube.aabb.min = (Vector3){-2, -2, -2};
    cube.aabb.max = (Vector3){ 2,  2,  2};

    R3D_SurfaceEffect* effect = R3D_LoadSurfaceEffect(
        RESOURCES_PATH "shaders/surface_effect_shadow_test.glsl"
    );

    if (!effect)
    {
        R3D_UnloadMesh(cube);
        R3D_UnloadMesh(ground);
        R3D_Close();
        CloseWindow();
        return 1;
    }

    float lift = 0.75f;
    float stripeWidth = 0.45f;

    R3D_SetSurfaceEffectUniform(effect, "lift", &lift);
    R3D_SetSurfaceEffectUniform(effect, "stripeWidth", &stripeWidth);

    R3D_Light spot = R3D_CreateSpotLight(
        (Vector3){3, 8, 4},
        (Vector3){-0.3f, -1, -0.4f},
        65.0f, WHITE, 2.0f
    );

    R3D_Light omni = R3D_CreateOmniLight(
        (Vector3){3, 5, 3},
        15.0f, WHITE, 2.0f
    );

    R3D_ShadowMap spotShadow = R3D_LoadShadowMap(R3D_LIGHT_SPOT);
    R3D_ShadowMap omniShadow = R3D_LoadShadowMap(R3D_LIGHT_OMNI);

    R3D_ENVIRONMENT_SET(ambient.color, (Color){35, 35, 35, 255});

    Camera3D camera = {
        .position = {5, 5, 9},
        .target = {0, 0, 0},
        .up = {0, 1, 0},
        .fovy = 60,
        .projection = CAMERA_PERSPECTIVE
    };

    bool useOmni = false;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_SPACE))
            useOmni = !useOmni;

        UpdateCamera(&camera, CAMERA_ORBITAL);

        BeginDrawing();
        ClearBackground(RAYWHITE);

        R3D_Begin(camera);

        if (useOmni)
            R3D_PushLightEx(omni, omniShadow, true);
        else
            R3D_PushLightEx(spot, spotShadow, true);

        R3D_DrawMesh(
            ground, material,
            (Vector3){0, -0.8f, 0},
            1.0f
        );

        // Baseline: unchanged cube and normal shadow.
        R3D_DrawMesh(
            cube, material,
            (Vector3){-1.8f, 0, 0},
            1.0f
        );

        // The visible geometry AND its shadow must be deformed.
        R3D_PushSurfaceEffect(effect);

        R3D_DrawMesh(
            cube, material,
            (Vector3){1.0f, 0, 0},
            1.0f
        );

        R3D_PopSurfaceEffect();

        R3D_End();

        DrawText(
            useOmni ? "OMNI SHADOW - SPACE to switch" :
                      "SPOT SHADOW - SPACE to switch",
            10, 10, 20, BLACK
        );

        R3D_DrawRenderStats(10, 45);

        EndDrawing();
    }

    R3D_UnloadSurfaceEffect(effect);
    R3D_UnloadShadowMap(spotShadow);
    R3D_UnloadShadowMap(omniShadow);
    R3D_UnloadMesh(cube);
    R3D_UnloadMesh(ground);

    R3D_Close();
    CloseWindow();
    return 0;
}