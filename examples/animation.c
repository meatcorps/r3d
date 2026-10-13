#include <r3d/r3d.h>
#include <raymath.h>
#include <string.h>

#ifndef RESOURCES_PATH
#	define RESOURCES_PATH "./"
#endif

static bool get_attachment_world(const R3D_AnimationPlayer* player, int boneIndex, Matrix socketOffset, Matrix characterWorld, Matrix* outMatrix)
{
    if (!outMatrix) return false;

    Matrix boneModel;

    if (!R3D_GetAnimationBoneModelMatrix(player, boneIndex, &boneModel)) return false;

    Matrix socketModel = MatrixMultiply(socketOffset, boneModel);
    *outMatrix = MatrixMultiply(socketModel, characterWorld);

    return true;
}

int main(void)
{
    // Initialize window
    InitWindow(1152, 648, "[r3d] - Animation example");
    SetTargetFPS(60);

    // Initialize R3D with FXAA
    R3D_Init(GetScreenWidth(), GetScreenHeight());
    R3D_SetAntiAliasingMode(R3D_ANTI_ALIASING_MODE_FXAA);

    // Setup environment sky
    R3D_Cubemap cubemap = R3D_LoadCubemap(RESOURCES_PATH "panorama/indoor.hdr", R3D_CUBEMAP_LAYOUT_AUTO_DETECT);
    R3D_ENVIRONMENT_SET(background.skyBlur, 0.3f);
    R3D_ENVIRONMENT_SET(background.energy, 0.6f);
    R3D_ENVIRONMENT_SET(background.sky, cubemap);

    // Setup environment ambient
    R3D_AmbientMap ambientMap = R3D_GenAmbientMap(cubemap, R3D_AMBIENT_ILLUMINATION);
    R3D_ENVIRONMENT_SET(ambient.map, ambientMap);
    R3D_ENVIRONMENT_SET(ambient.energy, 0.25f);

    // Setup tonemapping
    R3D_ENVIRONMENT_SET(tonemap.mode, R3D_TONEMAP_FILMIC);
    R3D_ENVIRONMENT_SET(tonemap.exposure, 0.75f);

    // Generate a ground plane and load the animated model
    R3D_Mesh plane = R3D_GenMeshPlane(10, 10, 1, 1);
    R3D_Model model = R3D_LoadModel(RESOURCES_PATH "models/CesiumMan.glb");

    // Load animations
    R3D_AnimationLib modelAnims = R3D_LoadAnimationLib(RESOURCES_PATH "models/CesiumMan.glb");
    R3D_AnimationPlayer modelPlayer = R3D_LoadAnimationPlayer(model.skeleton, modelAnims);

    // Print the available bone names once to identify the hand bone.
    for (int i = 0; i < modelPlayer.skeleton.boneCount; i++)
    {
        TraceLog(LOG_INFO, "Bone %d: %s\n", i, modelPlayer.skeleton.bones[i].name);
    }

    // Replace the name with the actual right-hand bone from the output.
    int handBone = modelPlayer.skeleton.boneCount > 0 ? 0 : -1;

    R3D_Mesh flashlightMesh = R3D_GenMeshCube(0.4f, 0.4f, 0.4f);
    Matrix flashlightOffset = MatrixTranslate(0.5f, 0.0f, 0.0f);

    // Setup animation playing
    R3D_SetAnimationLoop(&modelPlayer, 0, true);
    R3D_PlayAnimation(&modelPlayer, 0);

    // Create model instances
    R3D_InstanceBuffer instances = R3D_LoadInstanceBuffer(4, R3D_INSTANCE_POSITION);
    Vector3* positions = R3D_MapInstances(instances, R3D_INSTANCE_POSITION, false);
    for (int z = 0; z < 2; z++)
    {
        for (int x = 0; x < 2; x++)
        {
            positions[z*2 + x] = (Vector3) {(float)x - 0.5f, 0, (float)z - 0.5f};
        }
    }
    R3D_UnmapInstances(instances, R3D_INSTANCE_POSITION);

    // Setup lights with shadows
    R3D_Light light = R3D_CreateDirLight((Vector3) {-1, -1, -1}, WHITE, 1.0f);
    light.range = 20.0f;

    R3D_ShadowMap shadow = R3D_LoadShadowMap(R3D_LIGHT_DIR);
    shadow.softness = 2.0f;

    // Setup camera
    Camera3D camera = {
        .position = {0, 1.5f, 3.0f},
        .target   = {0, 0.75f, 0.0f},
        .up       = {0, 1, 0},
        .fovy     = 60
    };

    // Main loop
    while (!WindowShouldClose())
    {
        float delta = GetFrameTime();

        if (IsKeyPressed(KEY_RIGHT))
        {
            float time = R3D_GetAnimationTimeSeconds(modelPlayer, 0);
            R3D_SetAnimationTimeSeconds(&modelPlayer, 0, time + 0.25f);
        }

        if (IsKeyPressed(KEY_LEFT))
        {
            float time = R3D_GetAnimationTimeSeconds(modelPlayer, 0);
            R3D_SetAnimationTimeSeconds(&modelPlayer, 0, time - 0.25f);
        }

        UpdateCamera(&camera, CAMERA_ORBITAL);
        bool verifyOrder = IsKeyPressed(KEY_T);
        float updateDelta = verifyOrder ? R3D_GetAnimationDurationSeconds(modelPlayer, 0) * 0.25f : delta;

        R3D_UpdateAnimationPlayer(&modelPlayer, updateDelta);

        Matrix characterWorld = MatrixScale(1.25f, 1.25f, 1.25f);

        Matrix flashlightWorld = MatrixIdentity();
        bool hasFlashlight = get_attachment_world(&modelPlayer, handBone, flashlightOffset, characterWorld, &flashlightWorld);

        if (verifyOrder)
        {
            int boneCount = modelPlayer.skeleton.boneCount;
            Matrix* sampledPose = MemAlloc(boneCount * sizeof(Matrix));

            if (sampledPose)
            {
                memcpy(sampledPose, modelPlayer.localPose, boneCount * sizeof(Matrix));

                // Evaluate again at the reported time without advancing.
                R3D_ComputeAnimationPose(&modelPlayer);

                int mismatches = 0;
                for (int i = 0; i < boneCount; i++)
                {
                    if (!R3D_MatrixEquals(sampledPose[i], modelPlayer.localPose[i])) mismatches++;
                }

                TraceLog(mismatches == 0 ? LOG_INFO : LOG_ERROR, "Animation pose/time synchronization: %s (%d mismatched bones)", mismatches == 0 ? "PASS" : "FAIL", mismatches);

                MemFree(sampledPose);
            }
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);
            R3D_Begin(camera);
                R3D_PushLightEx(light, shadow, true);
                R3D_DrawMesh(plane, R3D_MATERIAL_BASE, Vector3Zero(), 1.0f);
                R3D_DrawAnimatedModelPro(model, modelPlayer, characterWorld);
                R3D_DrawAnimatedModelInstanced(model, modelPlayer, instances, 4);
            R3D_End();

            DrawText(TextFormat("Time: %.1f / %.1f seconds",
            R3D_GetAnimationTimeSeconds(modelPlayer, 0),
            R3D_GetAnimationDurationSeconds(modelPlayer, 0)), 10, 10, 20, BLACK);

            DrawText(TextFormat("Normalized: %.3f",
            R3D_GetAnimationNormalizedTime(modelPlayer, 0)), 10, 35, 20, BLACK);

            DrawText("LEFT / RIGHT: Seek 0.25 seconds", 10, 60, 20, BLACK);
            DrawText(
                TextFormat("Bone: %d | Attachment: %s", handBone, hasFlashlight ? "YES" : "NO"),
                10, 100, 20, hasFlashlight ? GREEN : RED
            );
        EndDrawing();
    }

    // Cleanup
    R3D_UnloadAnimationPlayer(modelPlayer);
    R3D_UnloadAnimationLib(modelAnims);
    R3D_UnloadModel(model, true);
    R3D_UnloadMesh(plane);
    R3D_UnloadMesh(flashlightMesh);
    R3D_Close();

    CloseWindow();

    return 0;
}
