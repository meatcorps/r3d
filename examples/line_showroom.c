/*
 * Meatcorps R3D - LINE ATLAS
 * Persistent world-space and screen-space paths, cinematic geometry and live updates.
 *
 * LEFT/RIGHT : change scene       SPACE : pause animation
 * O          : orbital camera     B     : bloom on/off
 * G          : built-in outer glow (world-space ribbons)
 * N          : path resolution    UP/DOWN : screen width scale
 * P          : projected Raylib 2D overlay (optional)
 * T          : temporary arc (create, draw, release after frame)
 */

#include <r3d/r3d.h>
#include <raymath.h>
#include <math.h>
#include <stdbool.h>

#define VIEW_W 1600
#define VIEW_H 900
#define MAX_STREAM_POINTS 512
#define ELLIPSE_POINTS 48
#define TAU 6.28318530718f

typedef enum LineScene {
    LINE_SCENE_ORBITS,
    LINE_SCENE_FLOW,
    LINE_SCENE_SHAPES,
    LINE_SCENE_COUNT
} LineScene;

static const char* SCENE_NAMES[LINE_SCENE_COUNT] = {
    "ORBITAL GLYPHS", "FLOW FIELD", "SHAPE LAB"
};

static const char* SCENE_DETAILS[LINE_SCENE_COUNT] = {
    "3D orbital rings / pixel-constant lines / depth",
    "Textured world ribbons / animated gradients / HDR",
    "Rectangles / circles / ellipses / arcs / live updates"
};

static const Color SCENE_ACCENTS[LINE_SCENE_COUNT] = {
    {55, 214, 248, 255}, {248, 99, 180, 255}, {255, 188, 78, 255}
};

static void set_gradient(R3D_LinePoint* points, int count, float offset)
{
    for (int i = 0; i < count; i++)
    {
        float t = (float)i / (float)count;
        points[i].color = ColorFromHSV(fmodf(offset + t * 320.0f, 360.0f), 0.85f, 1.0f);
    }
}

static void animate_stream(R3D_LinePoint* points, int count, float time, bool screenMode)
{
    for (int i = 0; i < count; i++)
    {
        float t = (float)i / (float)(count - 1);
        float wave = sinf(t * 19.0f - time * 2.1f);
        float detail = sinf(t * 37.0f + time * 1.2f);
        float envelope = 0.60f + 0.40f * sinf(t * PI);
        float variation = 0.5f + 0.5f * sinf(t * 13.0f + time * 1.3f);

        points[i].position = (Vector3){
            -4.15f + t * 8.30f,
            (wave * 0.38f + detail * 0.14f) * envelope,
            sinf(t * 10.0f + time * 0.8f) * 0.18f
        };
        points[i].width = screenMode ? (5.0f + variation * 15.0f) : (0.065f + variation * 0.18f);
        points[i].color = ColorFromHSV(fmodf(190.0f + t * 290.0f + time * 22.0f, 360.0f), 0.90f, 1.0f);
        points[i].arcToNext = (R3D_LineArc){0};
    }
}

static R3D_Mesh world_line_mesh(R3D_LinePoint* points, int count, bool closed, float width)
{
    R3D_LinePath path = {
        .points = points, .count = count, .closed = closed,
        .style = {.width = width, .widthMode = R3D_LINE_WIDTH_WORLD, .normal = {0, 0, 1}, .miterLimit = 4.0f, .caps = R3D_LINE_CAP_BOTH}
    };
    return R3D_GenMeshLinePath(&path);
}

static R3D_Mesh screen_line_mesh(R3D_LinePoint* points, int count, bool closed, float width)
{
    R3D_LinePath path = {
        .points = points, .count = count, .closed = closed,
        .style = {.width = width, .widthMode = R3D_LINE_WIDTH_SCREEN, .caps = R3D_LINE_CAP_BOTH}
    };
    return R3D_GenMeshScreenLinePath(&path);
}

static void draw_world_line(R3D_Mesh mesh, R3D_Material material, Matrix transform, bool glow, R3D_BuiltRenderMode outerGlow)
{
    if (glow) R3D_PushBuiltRenderMode(outerGlow);
    R3D_DrawMeshPro(mesh, material, transform);
    if (glow) R3D_PopBuiltRenderMode();
}

static void draw_hud(LineScene scene, bool paused, bool orbit, bool bloom, bool glow, bool overlay2D, int count, float scale, double updateMs, int fps)
{
    Color accent = SCENE_ACCENTS[scene];
    Color muted = {138, 158, 182, 255};
    Color soft = {213, 230, 243, 255};

    // Fixed side navigation and diagnostics, inspired by the Effects Atlas.
    DrawRectangle(0, 0, 322, VIEW_H, (Color){7, 13, 27, 245});
    DrawRectangle(321, 0, 2, VIEW_H, Fade(accent, 0.38f));
    DrawText("MEATCORPS  /  R3D", 30, 29, 21, soft);
    DrawRectangle(30, 70, 43, 3, accent);
    DrawText("LINE", 27, 106, 58, RAYWHITE);
    DrawText("ATLAS", 29, 166, 58, accent);
    DrawText("DYNAMIC GEOMETRY SHOWROOM", 30, 236, 15, muted);

    for (int i = 0; i < LINE_SCENE_COUNT; i++)
    {
        int y = 306 + i * 76;
        bool selected = i == (int)scene;
        if (selected) DrawRectangle(18, y - 12, 287, 55, Fade(accent, 0.16f));
        DrawRectangle(29, y, 3, 29, selected ? accent : (Color){52, 70, 93, 255});
        DrawText(TextFormat("0%d", i + 1), 44, y + 1, 17, selected ? accent : muted);
        DrawText(SCENE_NAMES[i], 81, y, 19, selected ? RAYWHITE : muted);
    }

    DrawText("LIVE GEOMETRY", 30, 629, 18, accent);
    DrawText(TextFormat("%d source points", count), 30, 662, 20, soft);
    DrawText(TextFormat("Update + upload: %.3f ms", updateMs), 30, 693, 17, muted);
    DrawText(TextFormat("%d FPS  /  x%.2f", fps, scale), 30, 721, 18, soft);
    DrawText(TextFormat("BLOOM %s  /  GLOW %s", bloom ? "ON" : "OFF", glow ? "ON" : "OFF"), 30, 765, 16, accent);
    DrawText("PERSISTENT GPU MESHES", 30, 828, 15, muted);

    DrawRectangle(322, 0, VIEW_W - 322, 95, (Color){7, 13, 27, 185});
    DrawText(SCENE_NAMES[scene], 375, 25, 33, RAYWHITE);
    DrawText(SCENE_DETAILS[scene], 376, 65, 17, accent);

    DrawRectangle(322, VIEW_H - 98, VIEW_W - 322, 98, (Color){7, 13, 27, 225});
    DrawRectangle(363, VIEW_H - 99, 120, 3, accent);
    DrawText("LEFT / RIGHT    SCENE      SPACE    PAUSE      O    CAMERA      B    BLOOM      G    GLOW", 367, VIEW_H - 75, 17, soft);
    DrawText("N    RESOLUTION      UP / DOWN    PIXEL SCALE      P    RAYLIB 2D      T    ARC", 367, VIEW_H - 43, 16, muted);

    DrawText(orbit ? "ORBIT ON" : "ORBIT OFF", VIEW_W - 166, 26, 16, accent);
    DrawText(paused ? "PAUSED" : "ANIMATING", VIEW_W - 170, 52, 16, muted);
    DrawText(TextFormat("BLOOM %s  /  2D %s", bloom ? "ON" : "OFF", overlay2D ? "ON" : "OFF"), VIEW_W - 280, VIEW_H - 31, 15, accent);

    for (int i = 0; i < LINE_SCENE_COUNT; i++)
    {
        DrawRectangle(VIEW_W / 2 - 32 + i * 28, VIEW_H - 110, 21, 3, i == (int)scene ? accent : (Color){48, 66, 88, 255});
    }
}

int main(void)
{
    InitWindow(VIEW_W, VIEW_H, "[r3d] Meatcorps - Line Atlas");
    SetTargetFPS(60);

    // Preallocate enough shared GPU storage for the 512-point screen path.
    // This also avoids driver-specific buffer-growth copy warnings on macOS.
    R3D_SetHint(R3D_HINT_MESH_VERTEX_BUFFER_CAPACITY, 262144);
    R3D_SetHint(R3D_HINT_MESH_INDEX_BUFFER_CAPACITY, 262144);
    if (!R3D_Init(VIEW_W, VIEW_H)) { CloseWindow(); return 1; }

    R3D_SetAntiAliasingMode(R3D_ANTI_ALIASING_MODE_FXAA);
    R3D_SetAntiAliasingPreset(R3D_ANTI_ALIASING_PRESET_HIGH);
    R3D_ENVIRONMENT_SET(background.color, (Color){5, 9, 21, 255});
    R3D_ENVIRONMENT_SET(ambient.color, (Color){35, 55, 82, 255});
    R3D_ENVIRONMENT_SET(tonemap.mode, R3D_TONEMAP_ACES);
    // Deliberately strong neon bloom; the B key toggles it independently of Outer Glow.
    R3D_ENVIRONMENT_SET(bloom.mode, R3D_BLOOM_ADDITIVE);
    R3D_ENVIRONMENT_SET(bloom.threshold, 0.70f);
    R3D_ENVIRONMENT_SET(bloom.softThreshold, 0.70f);
    R3D_ENVIRONMENT_SET(bloom.levels, 1.0f);
    R3D_ENVIRONMENT_SET(bloom.filterRadius, 1.4f);

    Camera3D camera = {
        .position = {0.7f, 2.4f, 12.5f},
        .target = {0.7f, 0.0f, 0.0f},
        .up = {0, 1, 0}, .fovy = 48.0f, .projection = CAMERA_PERSPECTIVE
    };
    R3D_Light light = R3D_CreateOmniLight((Vector3){-2.0f, 3.5f, 4.5f}, 20.0f, (Color){105, 180, 255, 255}, 2.5f);

    R3D_Material screenMat = R3D_GetDefaultScreenLineMaterial();
    screenMat.emission.color = (Color){160, 210, 255, 255};
    screenMat.emission.energy = 2.8f;
    R3D_Material worldMat = R3D_GetDefaultMaterial();
    worldMat.cullMode = R3D_CULL_NONE;
    worldMat.unlit = false;
    worldMat.emission.color = (Color){100, 155, 255, 255};
    worldMat.emission.energy = 1.7f;
    R3D_Material sphereMat = R3D_GetDefaultMaterial();
    sphereMat.albedo.color = (Color){12, 21, 43, 255};
    sphereMat.emission.color = (Color){22, 73, 130, 255};
    sphereMat.emission.energy = 1.35f;

    Image checkerImage = GenImageChecked(128, 128, 16, 16, (Color){245, 245, 255, 255}, (Color){45, 65, 105, 255});
    Texture2D checker = LoadTextureFromImage(checkerImage);
    UnloadImage(checkerImage);
    SetTextureWrap(checker, TEXTURE_WRAP_REPEAT);
    R3D_Material textureMat = worldMat;
    textureMat.albedo.texture = checker;

    // Built-in screen-space silhouette glow. Independent from HDR bloom.
    R3D_BuiltRenderMode outerGlow = R3D_RenderModeBuildOuterGlow((Color){71, 205, 255, 255}, 17.0f, 3.5f, 3.2f);
    R3D_SurfaceEffect* screenFx = R3D_LoadScreenLineEffect();
    R3D_Mesh sphere = R3D_GenMeshSphere(0.82f, 48, 48);

    R3D_LinePoint orbitPoints[4] = {0};
    R3D_SetLineCircle(orbitPoints, Vector3Zero(), 2.0f);
    set_gradient(orbitPoints, 4, 165.0f);
    orbitPoints[0].width = 8.0f;
    orbitPoints[1].width = 15.0f;
    orbitPoints[2].width = 8.0f;
    orbitPoints[3].width = 20.0f;
    R3D_Mesh orbitMesh = screen_line_mesh(orbitPoints, 4, true, 12.0f);

    R3D_LinePoint worldRingPoints[4] = {0};
    R3D_SetLineCircle(worldRingPoints, Vector3Zero(), 1.35f);
    set_gradient(worldRingPoints, 4, 5.0f);
    R3D_Mesh worldRing = world_line_mesh(worldRingPoints, 4, true, 0.12f);

    R3D_LinePoint rectanglePoints[4] = {0};
    R3D_SetLineRectangle(rectanglePoints, Vector3Zero(), (Vector2){1.55f, 1.10f});
    set_gradient(rectanglePoints, 4, 185.0f);
    R3D_Mesh rectangleMesh = world_line_mesh(rectanglePoints, 4, true, 0.12f);

    R3D_LinePoint circlePoints[4] = {0};
    R3D_SetLineCircle(circlePoints, Vector3Zero(), 0.73f);
    set_gradient(circlePoints, 4, 15.0f);
    R3D_Mesh circleMesh = screen_line_mesh(circlePoints, 4, true, 13.0f);

    R3D_LinePoint ellipsePoints[ELLIPSE_POINTS] = {0};
    R3D_SetLineEllipse(ellipsePoints, ELLIPSE_POINTS, Vector3Zero(), (Vector2){0.98f, 0.62f});
    set_gradient(ellipsePoints, ELLIPSE_POINTS, 270.0f);
    R3D_Mesh ellipseMesh = world_line_mesh(ellipsePoints, ELLIPSE_POINTS, true, 0.14f);

    R3D_LinePoint arcPoints[2] = {{.color = (Color){255, 84, 124, 255}}, {.color = (Color){59, 208, 255, 255}}};
    R3D_SetLineArc(arcPoints, (Vector3){-0.88f, -0.30f, 0}, (Vector3){0.88f, -0.30f, 0}, (Vector3){0, 0, 1}, -PI * 0.78f, 32);
    R3D_Mesh arcMesh = screen_line_mesh(arcPoints, 2, false, 17.0f);

    R3D_LinePoint screenPoints[MAX_STREAM_POINTS] = {0};
    R3D_LinePoint worldPoints[MAX_STREAM_POINTS] = {0};
    const int counts[] = {128, 256, 512};
    int countIndex = 1;
    int pointCount = counts[countIndex];
    animate_stream(screenPoints, pointCount, 0.0f, true);
    animate_stream(worldPoints, pointCount, 0.0f, false);
    R3D_LinePath screenPath = {
        .points = screenPoints, .count = pointCount,
        .style = {.width = 9.0f, .widthMode = R3D_LINE_WIDTH_SCREEN, .caps = R3D_LINE_CAP_BOTH}
    };
    R3D_LinePath worldPath = {
        .points = worldPoints, .count = pointCount,
        .style = {.width = 0.1f, .widthMode = R3D_LINE_WIDTH_WORLD, .normal = {0, 0, 1}, .miterLimit = 4.0f, .caps = R3D_LINE_CAP_BOTH}
    };
    R3D_Mesh screenStream = R3D_GenMeshScreenLinePath(&screenPath);
    R3D_Mesh worldStream = R3D_GenMeshLinePath(&worldPath);

    // A genuinely short-lived mesh: never release it until AFTER R3D_End().
    R3D_LinePoint transientPoints[2] = {
        {.color = (Color){255, 94, 144, 255}},
        {.color = (Color){80, 221, 255, 255}}
    };
    R3D_SetLineArc(transientPoints, (Vector3){-1.7f, -0.8f, 0}, (Vector3){1.7f, -0.8f, 0}, (Vector3){0, 0, 1}, -PI * 0.65f, 28);
    R3D_LinePath transientPath = {
        .points = transientPoints, .count = 2,
        .style = {.width = 0.15f, .widthMode = R3D_LINE_WIDTH_WORLD, .normal = {0, 0, 1}, .caps = R3D_LINE_CAP_BOTH}
    };
    R3D_Mesh transientMesh = {0};
    double transientUntil = 0.0;

    bool orbit = false;
    bool paused = false;
    bool bloom = true;
    bool glow = false;
    bool overlay2D = false;
    LineScene scene = LINE_SCENE_ORBITS;
    float phase = 0.0f;
    float scale = 1.0f;
    double updateMs = 0.0;

    while (!WindowShouldClose())
    {
        float delta = fminf(GetFrameTime(), 0.05f);
        if (IsKeyPressed(KEY_RIGHT)) scene = (LineScene)(((int)scene + 1) % LINE_SCENE_COUNT);
        if (IsKeyPressed(KEY_LEFT)) scene = (LineScene)(((int)scene + LINE_SCENE_COUNT - 1) % LINE_SCENE_COUNT);
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_O)) orbit = !orbit;
        if (IsKeyPressed(KEY_B)) bloom = !bloom;
        if (IsKeyPressed(KEY_G)) glow = !glow;
        if (IsKeyPressed(KEY_P)) overlay2D = !overlay2D;
        if (IsKeyPressed(KEY_T))
        {
            if (!R3D_IsMeshValid(transientMesh)) transientMesh = R3D_GenMeshLinePath(&transientPath);
            transientUntil = GetTime() + 0.75;
        }
        if (IsKeyPressed(KEY_UP)) scale = fminf(3.0f, scale + 0.25f);
        if (IsKeyPressed(KEY_DOWN)) scale = fmaxf(0.25f, scale - 0.25f);
        if (IsKeyPressed(KEY_N))
        {
            countIndex = (countIndex + 1) % 3;
            pointCount = counts[countIndex];
            screenPath.count = pointCount;
            worldPath.count = pointCount;
        }

        if (!paused) phase += delta;
        if (orbit) UpdateCamera(&camera, CAMERA_ORBITAL);
        R3D_ENVIRONMENT_SET(bloom.intensity, bloom ? 0.58f : 0.0f);

        double updateStart = GetTime();
        animate_stream(screenPoints, pointCount, phase, true);
        animate_stream(worldPoints, pointCount, phase, false);
        bool updated = true;
        if (scene != LINE_SCENE_SHAPES) updated &= R3D_UpdateScreenLineMesh(&screenStream, &screenPath);
        if (scene != LINE_SCENE_ORBITS) updated &= R3D_UpdateLineMesh(&worldStream, &worldPath);
        updateMs = (GetTime() - updateStart) * 1000.0;

        BeginDrawing();
        ClearBackground((Color){5, 9, 21, 255});
        R3D_Begin(camera);
        R3D_PushLight(light);

        Matrix stage = MatrixTranslate(0.7f, 0.0f, 0.0f);
        if (scene == LINE_SCENE_ORBITS)
        {
            R3D_DrawMesh(sphere, sphereMat, (Vector3){0.7f, 0, 0}, 1.0f);
            R3D_DrawScreenLineMesh(orbitMesh, screenMat, screenFx, scale, MatrixMultiply(MatrixRotateY(phase * 0.47f), stage));
            R3D_DrawScreenLineMesh(orbitMesh, screenMat, screenFx, scale * 0.8f, MatrixMultiply(MatrixRotateX(1.06f + phase * 0.28f), stage));
            draw_world_line(worldRing, worldMat, MatrixMultiply(MatrixRotateY(-phase * 0.67f), stage), glow, outerGlow);
            R3D_DrawScreenLineMesh(screenStream, screenMat, screenFx, scale, MatrixTranslate(0.7f, -2.05f, 0.15f));
        }
        else if (scene == LINE_SCENE_FLOW)
        {
            R3D_DrawMesh(sphere, sphereMat, (Vector3){0.7f, -0.1f, -0.35f}, 1.0f);
            draw_world_line(worldRing, worldMat, MatrixMultiply(MatrixRotateY(phase * 0.55f), stage), glow, outerGlow);
            draw_world_line(worldStream, textureMat, MatrixTranslate(0.7f, 1.35f, 0.15f), glow, outerGlow);
            R3D_DrawScreenLineMesh(screenStream, screenMat, screenFx, scale, MatrixTranslate(0.7f, -1.45f, 0.55f));
            R3D_DrawScreenLineMesh(orbitMesh, screenMat, screenFx, scale * 0.65f, MatrixMultiply(MatrixRotateY(-phase * 0.25f), stage));
        }
        else
        {
            draw_world_line(rectangleMesh, worldMat, MatrixTranslate(-2.7f, 0.55f, 0), glow, outerGlow);
            R3D_DrawScreenLineMesh(circleMesh, screenMat, screenFx, scale, MatrixTranslate(-0.55f, 0.55f, 0));
            draw_world_line(ellipseMesh, worldMat, MatrixTranslate(1.7f, 0.55f, 0), glow, outerGlow);
            R3D_DrawScreenLineMesh(arcMesh, screenMat, screenFx, scale, MatrixTranslate(3.85f, 0.55f, 0));
            R3D_DrawMesh(sphere, sphereMat, (Vector3){0.7f, -1.5f, -0.30f}, 0.65f);
            draw_world_line(worldStream, worldMat, MatrixTranslate(0.7f, -1.50f, 0), glow, outerGlow);
        }
        if (R3D_IsMeshValid(transientMesh))
        {
            draw_world_line(transientMesh, worldMat, MatrixTranslate(0.7f, 0.0f, 0.7f), glow, outerGlow);
        }
        R3D_End();

        if (R3D_IsMeshValid(transientMesh) && GetTime() >= transientUntil)
        {
            R3D_UnloadMesh(transientMesh);
            transientMesh = (R3D_Mesh){0};
        }

        // Optional projection utility demo: drawn by Raylib on top of R3D.
        // This is deliberately not depth-tested against the 3D scene.
        if (overlay2D)
        {
            R3D_View view = {.camera = R3D_CameraFromRL(camera)};
            R3D_ProjectedLinePath projected = R3D_ProjectLinePath2D(&screenPath, view);
            R3D_Draw2DProjectedLinePath(&projected, scale * 0.55f);
            R3D_UnloadProjectedLinePath(projected);
        }

        draw_hud(scene, paused, orbit, bloom, glow, overlay2D, pointCount, scale, updateMs, GetFPS());
        if (scene == LINE_SCENE_SHAPES)
        {
            const char* names[] = {"RECTANGLE", "CIRCLE", "ELLIPSE", "ARC"};
            const float positions[] = {-2.7f, -0.55f, 1.7f, 3.85f};
            for (int i = 0; i < 4; i++)
            {
                Vector2 label = GetWorldToScreen((Vector3){positions[i], -0.53f, 0}, camera);
                DrawText(names[i], (int)label.x - MeasureText(names[i], 16) / 2, (int)label.y, 16, SCENE_ACCENTS[scene]);
            }
        }
        if (!updated) DrawText("MESH UPDATE FAILED", 375, VIEW_H - 133, 22, RED);
        EndDrawing();
    }

    if (R3D_IsMeshValid(transientMesh)) R3D_UnloadMesh(transientMesh);
    R3D_UnloadMesh(orbitMesh);
    R3D_UnloadMesh(worldRing);
    R3D_UnloadMesh(rectangleMesh);
    R3D_UnloadMesh(circleMesh);
    R3D_UnloadMesh(ellipseMesh);
    R3D_UnloadMesh(arcMesh);
    R3D_UnloadMesh(screenStream);
    R3D_UnloadMesh(worldStream);
    R3D_UnloadMesh(sphere);
    R3D_UnloadBuiltRenderMode(&outerGlow);
    if (screenFx) R3D_UnloadSurfaceEffect(screenFx);
    UnloadTexture(checker);
    R3D_Close();
    CloseWindow();
    return 0;
}
