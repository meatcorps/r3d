#include <r3d/r3d.h>
#include <r3d/r3d_manifold.h>
#include <raymath.h>

static R3D_Mesh create_cut_mesh(const R3D_MeshData* original)
{
    R3D_Mesh result = {0};

    // Convert two R3D meshes into Manifold solids.
    R3D_MeshData cutterData = R3D_GenMeshDataCube(1.45f, 1.45f, 1.45f);

    ManifoldMeshGL* originalGL = R3D_MeshDataToManifoldMeshGL(original);
    ManifoldMeshGL* cutterGL = R3D_MeshDataToManifoldMeshGL(&cutterData);

    ManifoldManifold* cube = originalGL ? manifold_of_meshgl(manifold_alloc_manifold(), originalGL) : NULL;
    ManifoldManifold* cutter = cutterGL ? manifold_of_meshgl(manifold_alloc_manifold(), cutterGL) : NULL;
    ManifoldManifold* moved = NULL;
    ManifoldManifold* cut = NULL;

    if (cube && cutter &&
        manifold_status(cube) == MANIFOLD_NO_ERROR &&
        manifold_status(cutter) == MANIFOLD_NO_ERROR)
    {
        moved = manifold_translate(manifold_alloc_manifold(), cutter, 0.65, 0.65, 0.65);
        cut = manifold_difference(manifold_alloc_manifold(), cube, moved);
    }

    if (cut && manifold_status(cut) == MANIFOLD_NO_ERROR && !manifold_is_empty(cut))
    {
        // Recalculate the normal channel, preserving other vertex properties.
        ManifoldMeshGL* outputGL = manifold_get_meshgl_w_normals(manifold_alloc_meshgl(), cut, 0);

        if (outputGL)
        {
            R3D_MeshData output = R3D_ManifoldMeshGLToMeshData(outputGL);

            if (R3D_IsMeshDataValid(output))
            {
                result = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, output, NULL);
            }

            R3D_UnloadMeshData(output);
            manifold_delete_meshgl(outputGL);
        }
    }
    else
    {
        TraceLog(LOG_WARNING, "Manifold Boolean operation failed");
    }

    if (cut) manifold_delete_manifold(cut);
    if (moved) manifold_delete_manifold(moved);
    if (cutter) manifold_delete_manifold(cutter);
    if (cube) manifold_delete_manifold(cube);
    if (cutterGL) manifold_delete_meshgl(cutterGL);
    if (originalGL) manifold_delete_meshgl(originalGL);

    R3D_UnloadMeshData(cutterData);

    return result;
}

int main(void)
{
    InitWindow(1152, 648, "[r3d] Manifold Boolean Test");
    SetTargetFPS(60);

    R3D_Init(GetScreenWidth(), GetScreenHeight());

    // Both meshes are generated once.
    R3D_MeshData cubeData = R3D_GenMeshDataCube(2.0f, 2.0f, 2.0f);
    R3D_Mesh original = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, cubeData, NULL);
    R3D_Mesh cut = create_cut_mesh(&cubeData);
    R3D_UnloadMeshData(cubeData);

    if (!R3D_IsMeshValid(original) || !R3D_IsMeshValid(cut))
    {
        TraceLog(LOG_ERROR, "Failed to create Manifold example meshes");

        if (R3D_IsMeshValid(original)) R3D_UnloadMesh(original);
        if (R3D_IsMeshValid(cut)) R3D_UnloadMesh(cut);

        R3D_Close();
        CloseWindow();
        return 1;
    }

    // Checkerboard texture to inspect UV continuity.
    Image image = GenImageChecked(256, 256, 32, 32, RAYWHITE, DARKGRAY);
    Texture2D checker = LoadTextureFromImage(image);
    UnloadImage(image);

    R3D_Material material = R3D_GetDefaultMaterial();
    material.albedo.texture = checker;

    R3D_ENVIRONMENT_SET(ambient.color, (Color){100, 100, 100, 255});

    R3D_Light light = R3D_CreateDirLight((Vector3){-0.5f, -1.0f, -0.7f}, WHITE, 2.0f);

    Camera3D camera = {
        .position = {3.5f, 2.5f, 3.5f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 45.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    bool showCut = false;

    while (!WindowShouldClose())
    {
        UpdateCamera(&camera, CAMERA_ORBITAL);

        if (IsKeyPressed(KEY_SPACE))
        {
            showCut = !showCut;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        R3D_Begin(camera);
            R3D_PushLight(light);
            R3D_DrawMesh(showCut ? cut : original, material, Vector3Zero(), 1.0f);
        R3D_End();

        DrawText("SPACE - Toggle Boolean cut", 20, 20, 20, BLACK);
        DrawText(showCut ? "CUT" : "ORIGINAL", 20, 50, 20, BLACK);

        EndDrawing();
    }

    R3D_UnloadMesh(cut);
    R3D_UnloadMesh(original);
    UnloadTexture(checker);

    R3D_Close();
    CloseWindow();

    return 0;
}