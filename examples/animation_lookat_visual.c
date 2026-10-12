/* animation_lookat_visual.c -- Interactive skeletal LookAt example.
 *
 * Standalone example; does not modify examples/animation.c.
 */

#include <r3d/r3d.h>
#include <r3d/r3d_animation_lookat.h>
#include <raymath.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

static const Vector3 AXES[6] = {
    { 1.0f, 0.0f, 0.0f},
    {-1.0f, 0.0f, 0.0f},
    { 0.0f, 1.0f, 0.0f},
    { 0.0f,-1.0f, 0.0f},
    { 0.0f, 0.0f, 1.0f},
    { 0.0f, 0.0f,-1.0f}
};

static const char* AXIS_NAMES[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

static bool contains_ignore_case(const char* name, const char* needle)
{
    if (!name || !needle) return false;

    for (const char* start = name; *start; start++)
    {
        const char* a = start;
        const char* b = needle;

        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b))
        {
            a++;
            b++;
        }

        if (*b == '\0') return true;
    }

    return false;
}

static int find_initial_bone(R3D_Skeleton skeleton)
{
    const char* preferences[] = {"head", "neck", "hand", "arm", "spine"};

    for (int p = 0; p < (int)(sizeof(preferences) / sizeof(preferences[0])); p++)
    {
        for (int i = 0; i < skeleton.boneCount; i++)
        {
            if (contains_ignore_case(skeleton.bones[i].name, preferences[p])) return i;
        }
    }

    return skeleton.boneCount > 0 ? 0 : -1;
}

static int next_nonparallel_axis(int current, int other)
{
    for (int i = 1; i <= 6; i++)
    {
        int candidate = (current + i) % 6;
        if (fabsf(Vector3DotProduct(AXES[candidate], AXES[other])) < 0.99f) return candidate;
    }

    return current;
}

static Vector3 matrix_origin(Matrix matrix)
{
    return Vector3Transform(Vector3Zero(), matrix);
}

// Select the local signed axis that best matches a known direction in model space.
// This provides useful rig-specific starting values, while F/U remain manual overrides.
static int closest_local_axis(Matrix boneModel, Vector3 modelDirection, int excludeAxis)
{
    Vector3 origin = matrix_origin(boneModel);
    float bestScore = -2.0f;
    int bestAxis = -1;

    for (int i = 0; i < 6; i++)
    {
        if (excludeAxis >= 0 && fabsf(Vector3DotProduct(AXES[i], AXES[excludeAxis])) > 0.99f) continue;

        Vector3 axis = Vector3Subtract(Vector3Transform(AXES[i], boneModel), origin);
        float lengthSq = Vector3LengthSqr(axis);
        if (lengthSq < 1e-12f) continue;

        float score = Vector3DotProduct(axis, modelDirection) / sqrtf(lengthSq);
        if (score > bestScore)
        {
            bestScore = score;
            bestAxis = i;
        }
    }

    return bestAxis;
}

static void pick_lookat_axes(const R3D_AnimationPlayer* player, int boneIndex, int* forwardAxis, int* upAxis)
{
    Matrix boneModel;
    if (!R3D_GetAnimationBoneModelMatrix(player, boneIndex, &boneModel)) return;

    // CesiumMan faces roughly +Z in model space, and +Y is model up.
    // These are character-facing conventions, not assumptions about bone-local axes.
    int forward = closest_local_axis(boneModel, (Vector3){0.0f, 0.0f, 1.0f}, -1);
    if (forward < 0) return;

    int up = closest_local_axis(boneModel, (Vector3){0.0f, 1.0f, 0.0f}, forward);
    if (up < 0) return;

    *forwardAxis = forward;
    *upAxis = up;

    TraceLog(LOG_INFO, "Auto-calibrated LookAt bone %d: forward %s, up %s",
        boneIndex, AXIS_NAMES[forward], AXIS_NAMES[up]);
}

int main(void)
{
    InitWindow(1152, 648, "[r3d] - Animation bone LookAt visual test");
    SetTargetFPS(60);

    if (!R3D_Init(GetScreenWidth(), GetScreenHeight()))
    {
        CloseWindow();
        return 1;
    }

    R3D_SetAntiAliasingMode(R3D_ANTI_ALIASING_MODE_FXAA);

    R3D_Cubemap cubemap = R3D_LoadCubemap(RESOURCES_PATH "panorama/indoor.hdr", R3D_CUBEMAP_LAYOUT_AUTO_DETECT);
    R3D_ENVIRONMENT_SET(background.skyBlur, 0.3f);
    R3D_ENVIRONMENT_SET(background.energy, 0.6f);
    R3D_ENVIRONMENT_SET(background.sky, cubemap);

    R3D_AmbientMap ambientMap = R3D_GenAmbientMap(cubemap, R3D_AMBIENT_ILLUMINATION);
    R3D_ENVIRONMENT_SET(ambient.map, ambientMap);
    R3D_ENVIRONMENT_SET(ambient.energy, 0.25f);
    R3D_ENVIRONMENT_SET(tonemap.mode, R3D_TONEMAP_FILMIC);
    R3D_ENVIRONMENT_SET(tonemap.exposure, 0.75f);

    R3D_Mesh plane = R3D_GenMeshPlane(10.0f, 10.0f, 1, 1);
    R3D_Mesh markerMesh = R3D_GenMeshSphere(0.09f, 12, 12);
    R3D_Mesh targetMesh = R3D_GenMeshSphere(0.15f, 16, 12);

    R3D_Material targetMaterial = R3D_MATERIAL_BASE;
    targetMaterial.albedo.color = YELLOW;
    targetMaterial.unlit = true;

    R3D_Material originMaterial = R3D_MATERIAL_BASE;
    originMaterial.albedo.color = LIME;
    originMaterial.unlit = true;

    R3D_Material tipMaterial = R3D_MATERIAL_BASE;
    tipMaterial.albedo.color = SKYBLUE;
    tipMaterial.unlit = true;

    R3D_Model model = R3D_LoadModel(RESOURCES_PATH "models/CesiumMan.glb");
    R3D_AnimationLib animations = R3D_LoadAnimationLib(RESOURCES_PATH "models/CesiumMan.glb");
    R3D_AnimationPlayer player = R3D_LoadAnimationPlayer(model.skeleton, animations);

    for (int i = 0; i < player.skeleton.boneCount; i++)
    {
        TraceLog(LOG_INFO, "LookAt bone %d: %s", i, player.skeleton.bones[i].name);
    }

    int boneIndex = find_initial_bone(player.skeleton);
    int forwardAxis = 4;  // +Z
    int upAxis = 2;       // +Y
    float weight = 0.65f;
    float maxAngleDegrees = 45.0f;
    bool useAngleLimit = true;
    bool lookAtEnabled = true;
    bool autoAxes = true;
    bool paused = false;
    Vector3 targetModel = {0.0f, 1.7f, 2.0f};

    if (animations.count > 0)
    {
        R3D_SetAnimationLoop(&player, 0, true);
        R3D_PlayAnimation(&player, 0);
    }

    R3D_Light light = R3D_CreateDirLight((Vector3){-1.0f, -1.0f, -1.0f}, WHITE, 1.0f);
    light.range = 20.0f;
    R3D_ShadowMap shadow = R3D_LoadShadowMap(R3D_LIGHT_DIR);
    shadow.softness = 2.0f;

    Camera3D camera = {
        .position = {3.0f, 2.5f, 4.0f},
        .target = {0.0f, 1.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 60.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    // Use this exact transform for both rendering and LookAt debug points.
    Matrix characterWorld = MatrixMultiply(MatrixRotateY(0.35f), MatrixScale(1.25f, 1.25f, 1.25f));

    while (!WindowShouldClose())
    {
        float dt = GetFrameTime();
        float moveSpeed = (IsKeyDown(KEY_LEFT_SHIFT) ? 3.0f : 1.25f) * dt;

        if (IsKeyDown(KEY_A)) targetModel.x -= moveSpeed;
        if (IsKeyDown(KEY_D)) targetModel.x += moveSpeed;
        if (IsKeyDown(KEY_Q)) targetModel.y -= moveSpeed;
        if (IsKeyDown(KEY_E)) targetModel.y += moveSpeed;
        if (IsKeyDown(KEY_W)) targetModel.z -= moveSpeed;
        if (IsKeyDown(KEY_S)) targetModel.z += moveSpeed;

        if (IsKeyPressed(KEY_SPACE)) lookAtEnabled = !lookAtEnabled;
        if (IsKeyPressed(KEY_P)) paused = !paused;
        if (IsKeyPressed(KEY_L)) useAngleLimit = !useAngleLimit;
        if (IsKeyPressed(KEY_MINUS)) weight = fmaxf(0.0f, weight - 0.1f);
        if (IsKeyPressed(KEY_EQUAL)) weight = fminf(1.0f, weight + 0.1f);
        if (IsKeyPressed(KEY_LEFT_BRACKET)) maxAngleDegrees = fmaxf(5.0f, maxAngleDegrees - 5.0f);
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) maxAngleDegrees = fminf(180.0f, maxAngleDegrees + 5.0f);

        if (IsKeyPressed(KEY_TAB) && player.skeleton.boneCount > 0)
        {
            boneIndex = (boneIndex + 1) % player.skeleton.boneCount;
            autoAxes = true;
        }

        if (IsKeyPressed(KEY_F))
        {
            forwardAxis = (forwardAxis + 1) % 6;
            if (fabsf(Vector3DotProduct(AXES[forwardAxis], AXES[upAxis])) > 0.99f)
            {
                upAxis = next_nonparallel_axis(upAxis, forwardAxis);
            }
        }

        if (IsKeyPressed(KEY_U)) upAxis = next_nonparallel_axis(upAxis, forwardAxis);

        if (IsKeyPressed(KEY_R))
        {
            boneIndex = find_initial_bone(player.skeleton);
            forwardAxis = 4;
            upAxis = 2;
            autoAxes = true;
            weight = 0.65f;
            maxAngleDegrees = 45.0f;
            useAngleLimit = true;
            lookAtEnabled = true;
            targetModel = (Vector3){0.0f, 1.7f, 2.0f};
        }

        R3D_AdvanceAnimationTime(&player, paused ? 0.0f : dt);
        R3D_ComputeAnimationLocalPose(&player);
        bool poseReady = R3D_RecomputeAnimationModelPose(&player);
        if (poseReady && autoAxes && boneIndex >= 0)
        {
            // Calibrate using the authored animation BEFORE applying LookAt.
            pick_lookat_axes(&player, boneIndex, &forwardAxis, &upAxis);
            autoAxes = false;
        }
        bool lookAtSucceeded = true;

        R3D_BoneLookAtParams params = {
            .localForward = AXES[forwardAxis],
            .localUp = AXES[upAxis],
            .modelUp = {0.0f, 1.0f, 0.0f},
            .weight = weight,
            .maxAngleRadians = useAngleLimit ? maxAngleDegrees * DEG2RAD : 0.0f
        };

        if (poseReady && lookAtEnabled && boneIndex >= 0)
        {
            lookAtSucceeded = R3D_ApplyAnimationBoneLookAt(&player, boneIndex, targetModel, params);
            if (lookAtSucceeded) poseReady = R3D_RecomputeAnimationModelPoseFromBone(&player, boneIndex);
        }

        if (poseReady) R3D_UploadAnimationPose(&player);

        // Only read modelPose after recomputing the hierarchy.
        Matrix boneModel = MatrixIdentity();
        bool hasBone = poseReady && R3D_GetAnimationBoneModelMatrix(&player, boneIndex, &boneModel);
        Vector3 targetWorld = Vector3Transform(targetModel, characterWorld);
        Vector3 originWorld = {0};
        Vector3 tipWorld = {0};
        Vector3 upTipWorld = {0};
        float aimError = 0.0f;

        if (hasBone)
        {
            Matrix boneWorld = MatrixMultiply(boneModel, characterWorld);
            originWorld = matrix_origin(boneWorld);
            tipWorld = Vector3Transform(Vector3Scale(AXES[forwardAxis], 0.5f), boneWorld);
            upTipWorld = Vector3Transform(Vector3Scale(AXES[upAxis], 0.45f), boneWorld);

            Vector3 currentDirection = Vector3Subtract(tipWorld, originWorld);
            Vector3 desiredDirection = Vector3Subtract(targetWorld, originWorld);

            if (Vector3LengthSqr(currentDirection) > 1e-10f && Vector3LengthSqr(desiredDirection) > 1e-10f)
            {
                float dot = Vector3DotProduct(Vector3Normalize(currentDirection), Vector3Normalize(desiredDirection));
                aimError = acosf(Clamp(dot, -1.0f, 1.0f)) * RAD2DEG;
            }
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);
            R3D_Begin(camera);
                R3D_PushLightEx(light, shadow, true);
                R3D_DrawMesh(plane, R3D_MATERIAL_BASE, Vector3Zero(), 1.0f);
                R3D_DrawAnimatedModelPro(model, player, characterWorld);
                R3D_DrawMesh(targetMesh, targetMaterial, targetWorld, 1.0f);

                if (hasBone)
                {
                    R3D_DrawMesh(markerMesh, originMaterial, originWorld, 1.0f);
                    R3D_DrawMesh(markerMesh, tipMaterial, tipWorld, 0.75f);
                }
            R3D_End();

            if (hasBone)
            {
                Vector2 originScreen = GetWorldToScreen(originWorld, camera);
                Vector2 tipScreen = GetWorldToScreen(tipWorld, camera);
                Vector2 upScreen = GetWorldToScreen(upTipWorld, camera);
                Vector2 targetScreen = GetWorldToScreen(targetWorld, camera);

                DrawLineEx(originScreen, tipScreen, 3.0f, SKYBLUE);
                DrawLineEx(originScreen, upScreen, 3.0f, RED);
                DrawLineEx(originScreen, targetScreen, 1.0f, Fade(YELLOW, 0.65f));
            }

            DrawRectangle(8, 8, 730, 187, Fade(BLACK, 0.76f));
            DrawText("BONE LOOKAT - BLUE: FORWARD / RED: UP", 18, 15, 20, RAYWHITE);
            DrawText(TextFormat("Bone [%d/%d]: %s | LookAt: %s | Animation: %s", boneIndex, player.skeleton.boneCount,
                boneIndex >= 0 ? player.skeleton.bones[boneIndex].name : "NONE",
                lookAtEnabled ? "ON" : "OFF", paused ? "PAUSED" : "PLAYING"), 18, 42, 16, RAYWHITE);
            DrawText(TextFormat("Local forward: %s   Local up: %s   Weight: %.1f", AXIS_NAMES[forwardAxis], AXIS_NAMES[upAxis], weight),
                18, 65, 16, RAYWHITE);
            DrawText(TextFormat("Angle limit: %s (%.0f deg)   Current aim error: %.1f deg", useAngleLimit ? "ON" : "OFF", maxAngleDegrees, aimError),
                18, 88, 16, RAYWHITE);
            DrawText(TextFormat("Target model-space: (%.2f, %.2f, %.2f) | Solver: %s", targetModel.x, targetModel.y, targetModel.z,
                !poseReady ? "POSE ERROR" : !lookAtSucceeded ? "FAILED" : "OK"), 18, 111, 16, lookAtSucceeded && poseReady ? LIME : RED);
            DrawText("WASD: move target X/Z   Q/E: down/up   SHIFT: fast   SPACE: toggle   P: pause", 18, 139, 14, RAYWHITE);
            DrawText("TAB: bone   F/U: forward/up axes   -/=: weight   L: angle limit   [ / ]: angle   R: reset", 18, 164, 14, RAYWHITE);
        EndDrawing();
    }

    R3D_UnloadAnimationPlayer(player);
    R3D_UnloadAnimationLib(animations);
    R3D_UnloadModel(model, true);
    R3D_UnloadMesh(targetMesh);
    R3D_UnloadMesh(markerMesh);
    R3D_UnloadMesh(plane);
    R3D_UnloadShadowMap(shadow);
    R3D_UnloadAmbientMap(ambientMap);
    R3D_UnloadCubemap(cubemap);
    R3D_Close();
    CloseWindow();

    return 0;
}
