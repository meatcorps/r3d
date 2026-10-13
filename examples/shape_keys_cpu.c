#include <r3d/r3d.h>
#include <raymath.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

static int check(const char* name, bool condition)
{
    printf("%s: %s\n", name, condition ? "PASS" : "FAIL");
    return condition ? 0 : 1;
}

static bool near_vec(Vector3 a, Vector3 b)
{
    return Vector3Distance(a, b) < 0.0001f;
}

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "R3D Shape Keys CPU Test");

    if (!IsWindowReady()) return 1;

    if (!R3D_Init(64, 64))
    {
        CloseWindow();
        return 1;
    }

    R3D_Model model = R3D_LoadModel(
        RESOURCES_PATH "models/AnimatedMorphCube.glb"
    );

    int failures = 0;
    int meshIndex = -1;

    if (model.shapeKeySets)
    {
        for (int i = 0; i < model.meshCount; i++)
        {
            if (R3D_GetShapeKeyCount(
                &model.shapeKeySets[i]) > 0)
            {
                meshIndex = i;
                break;
            }
        }
    }

    failures += check(
        "AnimatedMorphCube shape keys imported",
        meshIndex >= 0
    );

    if (meshIndex >= 0)
    {
        const R3D_ShapeKeySet* set =
            &model.shapeKeySets[meshIndex];

        int count = R3D_GetShapeKeyCount(set);

        printf(
            "\nMesh %d: %d vertices, %d shape keys\n",
            meshIndex,
            set->baseMesh.vertexCount,
            count
        );

        for (int i = 0; i < count; i++)
        {
            printf(
                "  [%d] %s (default weight %.3f)\n",
                i,
                R3D_GetShapeKeyName(set, i),
                set->keys[i].defaultWeight
            );
        }

        failures += check(
            "At least two shape keys",
            count >= 2
        );

        R3D_ShapeKeyInstance a =
            R3D_LoadShapeKeyInstance(set);

        R3D_ShapeKeyInstance b =
            R3D_LoadShapeKeyInstance(set);

        bool instancesValid =
            a.meshData.vertices &&
            b.meshData.vertices &&
            a.weights &&
            b.weights;

        failures += check(
            "Two independent CPU instances created",
            instancesValid
        );

        if (instancesValid && count >= 2)
        {
            // Start from the exact zero-weight base.
            bool zeroWeights = true;

            for (int i = 0; i < count; i++)
            {
                zeroWeights &=
                    R3D_SetShapeKeyWeight(&a, i, 0.0f);

                zeroWeights &=
                    R3D_SetShapeKeyWeight(&b, i, 0.0f);
            }

            zeroWeights &=
                R3D_ApplyShapeKeys(&a) &&
                R3D_ApplyShapeKeys(&b);

            failures += check(
                "Zero-weight application succeeds",
                zeroWeights
            );

            size_t vertexBytes =
                (size_t)set->baseMesh.vertexCount *
                sizeof(R3D_Vertex);

            failures += check(
                "Zero-weight vertices equal immutable base",
                memcmp(
                    a.meshData.vertices,
                    set->baseMesh.vertices,
                    vertexBytes
                ) == 0
            );

            // Apply TWO morph targets at once.
            bool weightsSet =
                R3D_SetShapeKeyWeight(&a, 0, 0.35f) &&
                R3D_SetShapeKeyWeight(&a, 1, 0.65f);

            failures += check(
                "Two shape-key weights accepted",
                weightsSet
            );

            failures += check(
                "Combined shape-key application succeeds",
                R3D_ApplyShapeKeys(&a)
            );

            // Compare a real imported vertex against the
            // mathematical sum of its target deltas.
            const int vertex = 0;

            Vector3 expected =
                set->baseMesh.vertices[vertex].position;

            if (set->keys[0].positionDeltas)
            {
                expected = Vector3Add(
                    expected,
                    Vector3Scale(
                        set->keys[0].positionDeltas[vertex],
                        0.35f
                    )
                );
            }

            if (set->keys[1].positionDeltas)
            {
                expected = Vector3Add(
                    expected,
                    Vector3Scale(
                        set->keys[1].positionDeltas[vertex],
                        0.65f
                    )
                );
            }

            failures += check(
                "Combined position matches weighted deltas",
                near_vec(
                    a.meshData.vertices[vertex].position,
                    expected
                )
            );

            // Applying the same weights twice must not
            // progressively deform the mesh.
            R3D_MeshData snapshot =
                R3D_CopyMeshData(a.meshData);

            bool repeated = R3D_ApplyShapeKeys(&a);

            failures += check(
                "Repeated application succeeds",
                repeated
            );

            failures += check(
                "Repeated application introduces no drift",
                snapshot.vertices &&
                memcmp(
                    a.meshData.vertices,
                    snapshot.vertices,
                    vertexBytes
                ) == 0
            );

            R3D_UnloadMeshData(snapshot);

            failures += check(
                "Second instance remains at base geometry",
                memcmp(
                    b.meshData.vertices,
                    set->baseMesh.vertices,
                    vertexBytes
                ) == 0
            );

            // Undo everything. Verify that packed normals
            // and tangents return exactly to their source.
            R3D_SetShapeKeyWeight(&a, 0, 0.0f);
            R3D_SetShapeKeyWeight(&a, 1, 0.0f);

            failures += check(
                "Resetting shape keys succeeds",
                R3D_ApplyShapeKeys(&a)
            );

            failures += check(
                "Reset restores exact original vertex data",
                memcmp(
                    a.meshData.vertices,
                    set->baseMesh.vertices,
                    vertexBytes
                ) == 0
            );

            // Verify bounds against the actual CPU geometry.
            BoundingBox calculated =
                R3D_CalculateMeshDataBoundingBox(
                    a.meshData
                );

            failures += check(
                "CPU bounds updated",
                near_vec(a.aabb.min, calculated.min) &&
                near_vec(a.aabb.max, calculated.max)
            );

            failures += check(
                "Invalid shape-key index rejected",
                !R3D_SetShapeKeyWeight(
                    &a, count, 1.0f
                )
            );
        }

        R3D_UnloadShapeKeyInstance(a);
        R3D_UnloadShapeKeyInstance(b);
    }

    R3D_UnloadModel(model, true);
    R3D_Close();
    CloseWindow();

    printf(
        "\nShape Keys CPU tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED"
    );

    return failures == 0 ? 0 : 1;
}