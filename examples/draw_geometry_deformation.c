
#include <r3d/r3d.h>
#include <raymath.h>

#include <math.h>
#include <stdlib.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

typedef enum {
    DEFORM_INFLATE,
    DEFORM_BULGE,
    DEFORM_WAVE,
    DEFORM_TWIST,
    DEFORM_BEND,
    DEFORM_RIPPLE,
    DEFORM_NOISE,
    DEFORM_JITTER,
    DEFORM_SNAP,
    DEFORM_EXPLODE,
    DEFORM_COUNT
} DeformationType;

static const char* names[DEFORM_COUNT] = {
    "INFLATE / DEFLATE",
    "BULGE",
    "WAVE / WOBBLE",
    "TWIST",
    "BEND",
    "RIPPLE",
    "NOISE DISPLACEMENT",
    "VERTEX JITTER",
    "VERTEX SNAP",
    "TRIANGLE EXPLODE"
};

static R3D_BuiltRenderMode create_deformation(DeformationType type)
{
    switch (type)
    {
    case DEFORM_INFLATE:
        return R3D_RenderModeBuildInflate(
            0.22f, (Vector3){1.0f,1.0f,0.0f}, false, true);

    case DEFORM_BULGE:
        return R3D_RenderModeBuildBulge(
            (Vector3){0.0f,0.0f,0.0f},
            1.2f, 0.55f, 1.4f, true);

    case DEFORM_WAVE:
        return R3D_RenderModeBuildWave(
            (Vector3){1,0,0},
            (Vector3){0,1,0},
            0.25f, 7.0f, 3.0f);

    case DEFORM_TWIST:
        return R3D_RenderModeBuildTwist(
            (Vector3){0,1,0},
            1.6f, -0.8f, 0.8f);

    case DEFORM_BEND:
        return R3D_RenderModeBuildBend(
            (Vector3){0,1,0},
            (Vector3){1,0,0},
            0.95f);

    case DEFORM_RIPPLE:
        return R3D_RenderModeBuildRipple(
            (Vector3){0,0,0},
            0.25f, 11.0f, 4.0f, 0.7f, true);

    case DEFORM_NOISE:
        return R3D_RenderModeBuildNoiseDisplacement(
            (Vector3){3,3,3}, 0.25f, 0.65f, true);

    case DEFORM_JITTER:
        return R3D_RenderModeBuildVertexJitter(
            0.12f, 9.0f, 7.0f, true);

    case DEFORM_SNAP:
        return R3D_RenderModeBuildVertexSnap(
            0.22f, (Vector3){1,1,1});

    case DEFORM_EXPLODE:
        return R3D_RenderModeBuildExplode(0.7f, 17.0f);

    default:
        return (R3D_BuiltRenderMode){0};
    }
}

static void draw_subject(
    bool animated,
    bool exploded,
    R3D_Mesh mesh,
    R3D_Mesh explodedMesh,
    R3D_Material material,
    R3D_Model model,
    R3D_Model explodedModel,
    R3D_AnimationPlayer player,
    float x)
{
    Vector3 position = {x, 0.0f, 0.0f};

    if (animated)
    {
        R3D_Model selected =
            (exploded && explodedModel.meshes)
                ? explodedModel
                : model;

        R3D_DrawAnimatedModel(selected, player, position, 1.0f);
    }
    else
    {
        R3D_Mesh selected =
            (exploded && R3D_IsMeshValid(explodedMesh))
                ? explodedMesh
                : mesh;

        R3D_DrawMesh(selected, material, position, 1.0f);
    }
}

int main(void)
{
    InitWindow(1440, 810, "[r3d] Geometry Deformation");
    SetTargetFPS(60);

    if (!R3D_Init(GetScreenWidth(), GetScreenHeight()))
    {
        CloseWindow();
        return 1;
    }

    // A dense mesh is needed for smooth vertex deformation.
    R3D_MeshData cubeData =
        R3D_GenMeshDataCubeEx(
            1.2f, 1.2f, 1.2f, 20, 20, 20
        );

    R3D_Mesh cube =
        R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, cubeData, NULL);

    // Split every triangle into three independent vertices.
    R3D_MeshData explosionData =
        R3D_PrepareMeshDataForExplosion(cubeData);

    R3D_Mesh explosionCube = {0};

    if (R3D_IsMeshDataValid(explosionData))
    {
        explosionCube = R3D_LoadMesh(
            R3D_PRIMITIVE_TRIANGLES, explosionData, NULL);
    }

    R3D_UnloadMeshData(explosionData);
    R3D_UnloadMeshData(cubeData);

    R3D_ExpandMeshBoundsForDeformation(&cube, 1.5f);
    R3D_ExpandMeshBoundsForDeformation(&explosionCube, 1.5f);

    R3D_Mesh floorMesh =
        R3D_GenMeshPlane(40.0f, 40.0f, 1, 1);

    R3D_Material material = R3D_GetDefaultMaterial();
    material.albedo.color = (Color){170, 190, 220, 255};
    material.orm.roughness = 0.38f;
    material.orm.metalness = 0.35f;

    R3D_Material floorMaterial = R3D_GetDefaultMaterial();
    floorMaterial.albedo.color = (Color){45, 50, 65, 255};
    floorMaterial.orm.roughness = 0.75f;

    // Retaining CPU mesh data lets us prepare imported triangles
    // while keeping original skin weights and other attributes.
    R3D_Model model = R3D_LoadModelEx(
        RESOURCES_PATH "models/CesiumMan.glb",
        R3D_IMPORT_RETAIN_MESH_DATA
    );

    R3D_AnimationLib animations =
        R3D_LoadAnimationLib(
            RESOURCES_PATH "models/CesiumMan.glb"
        );

    R3D_AnimationPlayer player =
        R3D_LoadAnimationPlayer(model.skeleton, animations);

    R3D_SetAnimationLoop(&player, 0, true);
    R3D_PlayAnimation(&player, 0);

    R3D_ExpandModelBoundsForDeformation(&model, 1.5f);

    // Shallow model metadata copy, but its mesh array owns
    // separately prepared GPU meshes.
    R3D_Model explodedModel = model;
    explodedModel.meshes = NULL;

    R3D_Mesh* explodedMeshes = NULL;

    if (model.meshData && model.meshCount > 0)
    {
        explodedMeshes = calloc(
            (size_t)model.meshCount, sizeof(R3D_Mesh)
        );

        if (explodedMeshes)
        {
            bool valid = true;

            for (int i = 0; i < model.meshCount; i++)
            {
                R3D_MeshData prepared =
                    R3D_PrepareMeshDataForExplosion(
                        model.meshData[i]
                    );

                if (!R3D_IsMeshDataValid(prepared))
                {
                    valid = false;
                }
                else
                {
                    explodedMeshes[i] = R3D_LoadMesh(
                        R3D_PRIMITIVE_TRIANGLES,
                        prepared,
                        NULL
                    );

                    valid = valid &&
                        R3D_IsMeshValid(explodedMeshes[i]);
                }

                R3D_UnloadMeshData(prepared);

                if (!valid) break;
            }

            if (valid)
            {
                explodedModel.meshes = explodedMeshes;
                R3D_ExpandModelBoundsForDeformation(
                    &explodedModel, 1.5f);
            }
            else
            {
                for (int i = 0; i < model.meshCount; i++)
                    R3D_UnloadMesh(explodedMeshes[i]);

                free(explodedMeshes);
                explodedMeshes = NULL;
            }
        }
    }

    R3D_BuiltRenderMode effects[DEFORM_COUNT] = {0};

    for (int i = 0; i < DEFORM_COUNT; i++)
        effects[i] = create_deformation((DeformationType)i);

    // Independent secondary effect for nested stacking.
    R3D_BuiltRenderMode secondary =
        R3D_RenderModeBuildWave(
            (Vector3){0,1,0},
            (Vector3){1,0,0},
            0.12f, 5.0f, 2.0f
        );

    R3D_ENVIRONMENT_SET(
        background.color, (Color){12, 17, 30, 255});

    R3D_ENVIRONMENT_SET(
        ambient.color, (Color){55, 65, 85, 255});

    R3D_ENVIRONMENT_SET(ambient.energy, 0.85f);
    R3D_ENVIRONMENT_SET(tonemap.mode, R3D_TONEMAP_ACES);

    R3D_Light light = R3D_CreateSpotLight(
        (Vector3){-3, 8, 5},
        (Vector3){0.3f,-1,-0.5f},
        40.0f,
        (Color){255, 220, 185, 255},
        2.0f
    );

    R3D_ShadowMap shadow =
        R3D_LoadShadowMap(R3D_LIGHT_SPOT);

    shadow.softness = 4.0f;

    R3D_Light fill = R3D_CreateOmniLight(
        (Vector3){4, 3, 3},
        12.0f,
        (Color){85, 145, 255, 255},
        1.3f
    );

    Camera3D camera = {
        .position = {0, 2.3f, 10.0f},
        .target = {0, 0, 0},
        .up = {0, 1, 0},
        .fovy = 55,
        .projection = CAMERA_PERSPECTIVE
    };

    int selected = DEFORM_INFLATE;
    bool animated = false;
    bool continuous = true;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_RIGHT))
            selected = (selected + 1) % DEFORM_COUNT;

        if (IsKeyPressed(KEY_LEFT))
            selected =
                (selected + DEFORM_COUNT - 1) % DEFORM_COUNT;

        if (IsKeyPressed(KEY_SPACE))
            animated = !animated;

        if (IsKeyPressed(KEY_C))
        {
            continuous = !continuous;

            const int value = continuous ? 1 : 0;

            const DeformationType affected[] = {
                DEFORM_INFLATE,
                DEFORM_BULGE,
                DEFORM_RIPPLE,
                DEFORM_NOISE,
                DEFORM_JITTER
            };

            for (size_t i = 0;
                 i < sizeof(affected) / sizeof(affected[0]);
                 i++)
            {
                R3D_SurfaceEffect* effect =
                    effects[affected[i]].effect;

                if (effect)
                {
                    R3D_SetSurfaceEffectUniform(
                        effect, "continuous", &value
                    );
                }
            }
        }

        float delta = GetFrameTime();

        R3D_UpdateAnimationPlayer(&player, delta);

        // Animate explosion progress by updating the resource
        // BEFORE Push. Submitted draws keep their snapshots.
        if (effects[DEFORM_EXPLODE].effect)
        {
            float time = (float)GetTime();
            float amount =
                0.72f * (0.5f - 0.5f*cosf(time*1.7f));

            R3D_SetSurfaceEffectUniform(
                effects[DEFORM_EXPLODE].effect,
                "amount",
                &amount
            );
        }

        bool exploded = selected == DEFORM_EXPLODE;

        BeginDrawing();
        ClearBackground((Color){12, 17, 30, 255});

        R3D_Begin(camera);

        R3D_PushLightEx(light, shadow, true);
        R3D_PushLight(fill);

        R3D_DrawMesh(
            floorMesh, floorMaterial,
            (Vector3){0, -1.0f, 0}, 1.0f
        );

        // Original appearance.
        draw_subject(
            animated, false,
            cube, explosionCube, material,
            model, explodedModel, player, -3.0f
        );

        // Selected deformation.
        R3D_PushBuiltRenderMode(effects[selected]);

        draw_subject(
            animated, exploded,
            cube, explosionCube, material,
            model, explodedModel, player, -1.0f
        );

        // Nested deformation on the SAME object.
        R3D_PushBuiltRenderMode(secondary);

        draw_subject(
            animated, exploded,
            cube, explosionCube, material,
            model, explodedModel, player, 1.0f
        );

        R3D_PopBuiltRenderMode();
        R3D_PopBuiltRenderMode();

        // Verify original rendering has been restored.
        draw_subject(
            animated, false,
            cube, explosionCube, material,
            model, explodedModel, player, 3.0f
        );

        R3D_End();

        DrawRectangle(
            0, 0, GetScreenWidth(), 105,
            (Color){5, 9, 17, 220}
        );

        DrawText(
            "R3D / GEOMETRY DEFORMATION",
            22, 12, 20, SKYBLUE
        );

        DrawText(
            TextFormat("%02d / %02d  %s",
                selected+1, DEFORM_COUNT, names[selected]),
            22, 45, 28, RAYWHITE
        );

        DrawText(
            "LEFT / RIGHT: EFFECT    SPACE: CUBE / ANIMATED    C: CONTINUITY",
            22, GetScreenHeight()-40, 18, LIGHTGRAY
        );

        DrawText(
            TextFormat("DEFORMATION: %s",
                continuous ? "CONTINUOUS" : "INDEPENDENT"),
            950, 55, 20,
            continuous ? SKYBLUE : ORANGE
        );

        DrawText(
            "DEFAULT       EFFECT       NESTED       DEFAULT",
            270, GetScreenHeight()-72, 18, RAYWHITE
        );

        DrawFPS(GetScreenWidth()-105, 20);

        EndDrawing();
    }

    R3D_UnloadBuiltRenderMode(&secondary);

    for (int i = 0; i < DEFORM_COUNT; i++)
        R3D_UnloadBuiltRenderMode(&effects[i]);

    if (explodedMeshes)
    {
        for (int i = 0; i < model.meshCount; i++)
            R3D_UnloadMesh(explodedMeshes[i]);

        free(explodedMeshes);
    }

    R3D_UnloadShadowMap(shadow);

    R3D_UnloadAnimationPlayer(player);
    R3D_UnloadAnimationLib(animations);
    R3D_UnloadModel(model, true);

    R3D_UnloadMesh(explosionCube);
    R3D_UnloadMesh(cube);
    R3D_UnloadMesh(floorMesh);

    R3D_Close();
    CloseWindow();

    return 0;
}
