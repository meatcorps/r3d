
/* Phase 6: Synthetic depth-camera point clouds and topology wireframes. */

#include <r3d/r3d.h>
#include <raymath.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>

#define CLOUD_W 160
#define CLOUD_H 120
#define CLOUD_CAPACITY (CLOUD_W * CLOUD_H)

static int fill_depth_cloud(R3D_PointCloudPoint* output, float seconds, int shapeMode)
{
    int count = 0;

    for (int y = 0; y < CLOUD_H; y++)
    {
        float v = 2.0f * (float)y / (CLOUD_H - 1) - 1.0f;

        for (int x = 0; x < CLOUD_W; x++)
        {
            float u = 2.0f * (float)x / (CLOUD_W - 1) - 1.0f;

            // Synthetic head and torso silhouette.
            float head =
                (u * u) / (0.28f * 0.28f) +
                ((v - 0.49f) * (v - 0.49f)) / (0.30f * 0.30f);

            float torso =
                (u * u) / (0.66f * 0.66f) +
                ((v + 0.49f) * (v + 0.49f)) / (0.60f * 0.60f);

            // Ignore empty depth pixels and introduce small gaps.
            if ((head >= 1.0f && torso >= 1.0f) ||
                (x * 17 + y * 37) % 43 == 0)
            {
                continue;
            }

            float surface =
                0.25f * sinf(u * 5.5f + seconds * 0.4f) +
                0.13f * cosf(v * 9.0f - seconds * 0.7f);

            float depth =
                0.35f + surface +
                0.15f * expf(
                    -u * u * 45.0f -
                    (v - 0.48f) * (v - 0.48f) * 35.0f
                );

            float hue = 185.0f + 115.0f * (
                0.5f + 0.5f *
                sinf(u * 3.0f + depth * 1.8f + seconds * 0.2f)
            );

            R3D_PointShape shape = shapeMode == 0
                ? (R3D_PointShape)(
                    (x / 12 + y / 12) % R3D_POINT_SHAPE_COUNT
                )
                : (R3D_PointShape)(shapeMode - 1);

            output[count++] = (R3D_PointCloudPoint){
                .position = {
                    -2.65f + u * 1.65f,
                    v * 1.75f,
                    depth
                },
                .size = 0.028f + 0.018f * (0.5f + 0.5f * surface),
                .color = ColorFromHSV(hue, 0.80f, 1.0f),
                .shape = shape
            };
        }
    }

    return count;
}

int main(void)
{
    InitWindow(1280, 720, "[r3d] - Point Clouds & Wireframes");
    SetTargetFPS(60);

    // Preallocate shared GPU buffers to avoid the
    // macOS buffer-growth warning we discovered on the M4.
    R3D_SetHint(R3D_HINT_MESH_VERTEX_BUFFER_CAPACITY, 262144);
    R3D_SetHint(R3D_HINT_MESH_INDEX_BUFFER_CAPACITY, 262144);

    if (!R3D_Init(GetScreenWidth(), GetScreenHeight()))
    {
        CloseWindow();
        return 1;
    }

    R3D_ENVIRONMENT_SET(
        background.color, (Color){5, 11, 26, 255}
    );

    R3D_ENVIRONMENT_SET(tonemap.mode, R3D_TONEMAP_ACES);
    R3D_ENVIRONMENT_SET(bloom.mode, R3D_BLOOM_ADDITIVE);
    R3D_ENVIRONMENT_SET(bloom.intensity, 0.43f);
    R3D_ENVIRONMENT_SET(bloom.threshold, 0.22f);
    R3D_ENVIRONMENT_SET(bloom.softThreshold, 0.7f);
    R3D_ENVIRONMENT_SET(bloom.levels, 0.85f);

    Camera3D camera = {
        .position = {0.0f, 1.0f, 11.5f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 50.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    // ---------------------------------------
    // Point cloud
    // ---------------------------------------

    R3D_PointCloud cloud = R3D_LoadPointCloud(CLOUD_CAPACITY);

    R3D_PointCloudPoint* points = calloc(
        CLOUD_CAPACITY, sizeof(*points)
    );

    R3D_Material pointMaterial =
        R3D_GetDefaultPointCloudMaterial();

    // ---------------------------------------
    // Topology wireframe
    // ---------------------------------------

    // A spherical mesh with actual connected triangle topology.
    // In a reconstructed point-cloud mesh, these connections
    // would come from the reconstruction algorithm instead.
    R3D_MeshData source = R3D_GenMeshDataSphere(
        1.35f, 18, 24
    );

    // Vertex colors vary with their actual 3D positions.
    // Duplicate vertices at UV seams receive identical colors.
    for (int i = 0; i < source.vertexCount; i++)
    {
        Vector3 p = source.vertices[i].position;

        float hue =
            165.0f +
            (p.y + 1.35f) * 52.0f +
            (p.x + 1.35f) * 18.0f;

        source.vertices[i].color = ColorFromHSV(
            hue, 0.90f, 1.0f
        );
    }

    R3D_LineStyle wireStyle = {
        .width = 3.0f,
        .widthMode = R3D_LINE_WIDTH_SCREEN,
        .caps = R3D_LINE_CAP_BOTH
    };

    int retainedEdges = 0;

    // Zero means ALL unique connected triangle edges.
    R3D_Mesh wireframe = R3D_GenMeshTopologyWireframe(
        &source,
        wireStyle,
        0.0f,
        0.0001f,
        &retainedEdges
    );

    // Keep mutable source geometry and an untouched reference.
    // This simulates reconstructed mesh data arriving every frame.
    R3D_MeshData restSource = R3D_CopyMeshData(source);

    R3D_SurfaceEffect* screenEffect =
        R3D_LoadScreenLineEffect();

    R3D_Material wireMaterial =
        R3D_GetDefaultScreenLineMaterial();

    R3D_BuiltRenderMode outerGlow = R3D_RenderModeBuildOuterGlow(
        (Color){55, 210, 255, 255},
        13.0f,
        2.5f,
        2.1f
    );

    if (!points ||
        !R3D_IsMeshDataValid(restSource) ||
        cloud.capacity == 0 ||
        !R3D_IsMeshValid(wireframe) ||
        !screenEffect)
    {
        TraceLog(
            LOG_ERROR,
            "Phase 6: failed to create point cloud or wireframe resources"
        );

        if (screenEffect)
        {
            R3D_UnloadSurfaceEffect(screenEffect);
        }

        if (R3D_IsMeshValid(wireframe))
        {
            R3D_UnloadMesh(wireframe);
        }

        R3D_UnloadBuiltRenderMode(&outerGlow);
        R3D_UnloadPointCloud(&cloud);
        free(points);

        R3D_Close();
        CloseWindow();
        return 1;
    }

    // ---------------------------------------
    // Animation and interaction
    // ---------------------------------------

    bool orbital = false;
    bool animate = true;
    bool bloom = true;
    bool glow = false;
    bool streamMesh = false;
    bool meshUpdateOK = true;
    double meshUpdateMs = 0.0;

    int shapeMode = 0;
    float seconds = 0.0f;

    double uploadMs = 0.0;
    int pointCount = 0;
    bool updateOK = true;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_O)) orbital = !orbital;
        if (IsKeyPressed(KEY_SPACE)) animate = !animate;
        if (IsKeyPressed(KEY_B)) bloom = !bloom;
        if (IsKeyPressed(KEY_G)) glow = !glow;
        if (IsKeyPressed(KEY_R)) streamMesh = !streamMesh;

        if (IsKeyPressed(KEY_S))
        {
            shapeMode = (shapeMode + 1) % 4;
        }

        if (animate)
        {
            seconds += fminf(GetFrameTime(), 0.05f);
        }

        if (orbital)
        {
            UpdateCamera(&camera, CAMERA_ORBITAL);
        }

        R3D_ENVIRONMENT_SET(
            bloom.intensity, bloom ? 0.43f : 0.0f
        );

        // Regenerate synthetic depth samples and upload
        // them to the EXISTING instance buffers.
        double start = GetTime();

        pointCount = fill_depth_cloud(
            points, seconds, shapeMode
        );

        updateOK = R3D_UpdatePointCloud(
            &cloud, points, pointCount
        );

        uploadMs = (GetTime() - start) * 1000.0;

        // Simulate incoming reconstructed mesh frames.
        // Geometry and vertex colors change, topology stays connected.
        if (streamMesh)
        {
            double meshStart = GetTime();

            for (int i = 0; i < source.vertexCount; i++)
            {
                Vector3 p = restSource.vertices[i].position;

                float wave = sinf(p.y * 3.0f + seconds * 2.5f) *
                             cosf(p.x * 3.2f - seconds * 1.4f);

                source.vertices[i].position = Vector3Scale(
                    p, 1.0f + wave * 0.13f
                );

                source.vertices[i].color = ColorFromHSV(
                    165.0f +
                    (p.y + 1.35f) * 52.0f +
                    (p.x + 1.35f) * 18.0f +
                    35.0f * sinf(seconds * 0.8f),
                    0.90f,
                    1.0f
                );
            }

            // Full CPU extraction + line tessellation + GPU upload.
            // No new GPU mesh object is created.
            meshUpdateOK = R3D_UpdateTopologyWireframe(
                &wireframe,
                &source,
                wireStyle,
                0.0f,
                0.0001f,
                &retainedEdges
            );

            meshUpdateMs = (GetTime() - meshStart) * 1000.0;
        }

        // Rotate the wireframe model without rebuilding
        // its edge geometry.
        Matrix transform = MatrixMultiply(
            MatrixMultiply(
                MatrixRotateX(-0.16f),
                MatrixRotateY(seconds * 0.32f)
            ),
            MatrixTranslate(2.35f, 0.0f, 0.0f)
        );

        BeginDrawing();
        ClearBackground((Color){5, 11, 26, 255});

        R3D_Begin(camera);

        if (glow)
        {
            R3D_PushBuiltRenderMode(outerGlow);
        }

        R3D_DrawPointCloud(&cloud, pointMaterial);

        if (glow)
        {
            R3D_PopBuiltRenderMode();
        }

        R3D_DrawScreenLineMesh(
            wireframe,
            wireMaterial,
            screenEffect,
            1.0f,
            transform
        );

        R3D_End();

        DrawText(
            "MEATCORPS / R3D  -  POINT CLOUDS & TOPOLOGY WIREFRAMES",
            22, 18, 23, RAYWHITE
        );

        DrawText(
            "SYNTHETIC DEPTH CAMERA",
            138, 130, 18,
            (Color){95, 218, 249, 255}
        );

        DrawText(
            "PRESENTATION WIREFRAME",
            786, 130, 18,
            (Color){250, 178, 90, 255}
        );

        DrawText(
            TextFormat("%d colored points  /  %d retained edges", pointCount, retainedEdges),
            22, 629, 17, LIGHTGRAY
        );

        DrawText(
            TextFormat(
                "Cloud: %.3f ms  |  Mesh: %s / %.3f ms  |  %d FPS  |  %s",
                uploadMs,
                streamMesh ? "STREAMING" : "STATIC",
                meshUpdateMs,
                GetFPS(),
                updateOK && meshUpdateOK ? "OK" : "FAILED"
            ),
            22, 655, 17,
            updateOK && meshUpdateOK ? (Color){110, 240, 179, 255} : RED
        );

        DrawText(
            "S SHAPE   O ORBIT   SPACE PAUSE   B BLOOM   G GLOW   R STREAM MESH",
            22, 687, 15, LIGHTGRAY
        );

        EndDrawing();
    }

    // ---------------------------------------
    // Cleanup
    // ---------------------------------------

    R3D_UnloadBuiltRenderMode(&outerGlow);
    R3D_UnloadSurfaceEffect(screenEffect);
    R3D_UnloadMesh(wireframe);
    R3D_UnloadPointCloud(&cloud);
    R3D_UnloadMeshData(source);
    R3D_UnloadMeshData(restSource);

    free(points);

    R3D_Close();
    CloseWindow();

    return 0;
}
