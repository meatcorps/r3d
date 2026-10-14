#include <r3d/r3d.h>
#include <raymath.h>

int main(void)
{
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

    R3D_SetAnimationLoop(&modelPlayer, 0, true);
    R3D_PlayAnimation(&modelPlayer, 0);

    // Setup environment
    R3D_ENVIRONMENT_SET(ambient.color, (Color){40, 40, 40, 255});

    // Create light
    R3D_Light light = R3D_CreateSpotLight((Vector3) {0, 10, 5}, (Vector3) {0, -1, -0.5f}, 50.0f, WHITE, 1.0f);
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

    // Setup camera
    Camera3D camera = {
        .position = {0, 2, 3},
        .target   = {0, 0, 0},
        .up       = {0, 1, 0},
        .fovy     = 60
    };

    bool drawMesh = true;

    // Main loop
    while (!WindowShouldClose())
    {
        float delta = GetFrameTime();

        UpdateCamera(&camera, CAMERA_ORBITAL);

        if (IsKeyPressed(KEY_SPACE))
        {
            drawMesh = !drawMesh;
        }

        R3D_UpdateAnimationPlayer(&modelPlayer, delta);

        BeginDrawing();
            ClearBackground(RAYWHITE);

            R3D_Begin(camera);
                R3D_PushLightEx(light, shadow, true);
                R3D_DrawMesh(plane, material, (Vector3) {0, -0.5f, 0}, 1.0f);

                if (drawMesh) {

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
                }
                else
                {
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
        EndDrawing();
    }

    // Cleanup
    R3D_UnloadMesh(box);
    R3D_UnloadMesh(plane);
    R3D_UnloadModel(model, true);
    R3D_UnloadAnimationLib(modelAnims);
    R3D_Close();

    CloseWindow();

    return 0;
}
