
/* r3d_wireframe.c -- Weld, classify and batch topology edges. */

#include <r3d/r3d_wireframe.h>
#include <raymath.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

typedef struct R3D_WeldKey {
    int64_t x, y, z;
    int original;
} R3D_WeldKey;

typedef struct R3D_EdgeCandidate {
    int a, b;
    Vector3 normal;
} R3D_EdgeCandidate;

static int r3d_compare_weld_key(const void* pa, const void* pb)
{
    const R3D_WeldKey* a = pa;
    const R3D_WeldKey* b = pb;

    if (a->x != b->x) return (a->x > b->x) - (a->x < b->x);
    if (a->y != b->y) return (a->y > b->y) - (a->y < b->y);
    if (a->z != b->z) return (a->z > b->z) - (a->z < b->z);

    return (a->original > b->original) -
           (a->original < b->original);
}

static int r3d_compare_edge_candidate(const void* pa, const void* pb)
{
    const R3D_EdgeCandidate* a = pa;
    const R3D_EdgeCandidate* b = pb;

    if (a->a != b->a) return (a->a > b->a) - (a->a < b->a);

    return (a->b > b->b) - (a->b < b->b);
}

static void r3d_wireframe_add_edge(R3D_EdgeCandidate* edges, int* count, int a, int b, Vector3 normal)
{
    if (a == b)
    {
        return;
    }

    if (a > b)
    {
        int temp = a;
        a = b;
        b = temp;
    }

    edges[*count] = (R3D_EdgeCandidate){a, b, normal};
    (*count)++;
}

R3D_MeshData R3D_GenMeshDataTopologyWireframe(const R3D_MeshData* source, R3D_LineStyle style, float creaseAngleDegrees, float weldEpsilon, int* outEdgeCount)
{
    R3D_MeshData result = {0};

    if (outEdgeCount)
    {
        *outEdgeCount = 0;
    }

    if (!source || !source->vertices || source->vertexCount < 3 ||
        !isfinite(weldEpsilon) || weldEpsilon <= 0.0f ||
        !isfinite(creaseAngleDegrees) ||
        creaseAngleDegrees < 0.0f || creaseAngleDegrees > 180.0f ||
        !isfinite(style.width) || style.width <= 0.0f)
    {
        return result;
    }

    if (source->indexCount > 0 && !source->indices)
    {
        return result;
    }

    int triangleCount = (
        source->indexCount > 0
            ? source->indexCount
            : source->vertexCount
    ) / 3;

    if (triangleCount <= 0 || triangleCount > INT_MAX / 3)
    {
        return result;
    }

    R3D_WeldKey* keys = malloc(
        (size_t)source->vertexCount * sizeof(*keys)
    );

    int* canonical = malloc(
        (size_t)source->vertexCount * sizeof(*canonical)
    );

    R3D_EdgeCandidate* edges = malloc(
        (size_t)triangleCount * 3 * sizeof(*edges)
    );

    if (!keys || !canonical || !edges)
    {
        goto cleanup;
    }

    // Weld duplicated positions, including vertices split
    // for different face normals or texture coordinates.
    for (int i = 0; i < source->vertexCount; i++)
    {
        Vector3 p = source->vertices[i].position;

        double x = (double)p.x / weldEpsilon;
        double y = (double)p.y / weldEpsilon;
        double z = (double)p.z / weldEpsilon;

        if (!isfinite(x) || !isfinite(y) || !isfinite(z) ||
            fabs(x) >= (double)LLONG_MAX * 0.5 ||
            fabs(y) >= (double)LLONG_MAX * 0.5 ||
            fabs(z) >= (double)LLONG_MAX * 0.5)
        {
            goto cleanup;
        }

        keys[i] = (R3D_WeldKey){
            llround(x), llround(y), llround(z), i
        };
    }

    qsort(
        keys,
        (size_t)source->vertexCount,
        sizeof(*keys),
        r3d_compare_weld_key
    );

    int group = keys[0].original;
    canonical[group] = group;

    for (int i = 1; i < source->vertexCount; i++)
    {
        if (keys[i].x != keys[i - 1].x ||
            keys[i].y != keys[i - 1].y ||
            keys[i].z != keys[i - 1].z)
        {
            group = keys[i].original;
        }

        canonical[keys[i].original] = group;
    }

    // Collect all three edges from each nondegenerate triangle.
    int edgeCount = 0;

    for (int i = 0; i < triangleCount; i++)
    {
        uint32_t a = source->indexCount > 0
            ? source->indices[i * 3 + 0]
            : (uint32_t)(i * 3 + 0);

        uint32_t b = source->indexCount > 0
            ? source->indices[i * 3 + 1]
            : (uint32_t)(i * 3 + 1);

        uint32_t c = source->indexCount > 0
            ? source->indices[i * 3 + 2]
            : (uint32_t)(i * 3 + 2);

        if (a >= (uint32_t)source->vertexCount ||
            b >= (uint32_t)source->vertexCount ||
            c >= (uint32_t)source->vertexCount)
        {
            goto cleanup;
        }

        Vector3 p = source->vertices[a].position;
        Vector3 q = source->vertices[b].position;
        Vector3 r = source->vertices[c].position;

        Vector3 normal = Vector3CrossProduct(
            Vector3Subtract(q, p),
            Vector3Subtract(r, p)
        );

        if (Vector3LengthSqr(normal) < 1.0e-12f)
        {
            continue;
        }

        normal = Vector3Normalize(normal);

        r3d_wireframe_add_edge(
            edges, &edgeCount,
            canonical[a], canonical[b], normal
        );

        r3d_wireframe_add_edge(
            edges, &edgeCount,
            canonical[b], canonical[c], normal
        );

        r3d_wireframe_add_edge(
            edges, &edgeCount,
            canonical[c], canonical[a], normal
        );
    }

    if (edgeCount == 0)
    {
        goto cleanup;
    }

    qsort(
        edges,
        (size_t)edgeCount,
        sizeof(*edges),
        r3d_compare_edge_candidate
    );

    // Build ONE combined line mesh, not one GPU mesh per edge.
    R3D_MeshData merged = R3D_LoadMeshData(32, 64);

    if (!merged.vertices || !merged.indices)
    {
        R3D_UnloadMeshData(merged);
        goto cleanup;
    }

    float creaseDot = cosf(creaseAngleDegrees * DEG2RAD);
    int selected = 0;

    for (int i = 0; i < edgeCount; )
    {
        int end = i + 1;

        while (end < edgeCount &&
               edges[end].a == edges[i].a &&
               edges[end].b == edges[i].b)
        {
            end++;
        }

        // A single triangle owning an edge means a boundary.
        // Zero degrees retains all unique mesh connections.
        // Higher values retain boundaries and sharp creases only.
        bool keep = creaseAngleDegrees == 0.0f || end == i + 1;

        // Multiple triangles sharing an edge:
        // keep it only when the face normals differ enough.
        for (int j = i + 1; !keep && j < end; j++)
        {
            if (Vector3DotProduct(
                    edges[i].normal,
                    edges[j].normal
                ) < creaseDot)
            {
                keep = true;
            }
        }

        if (keep)
        {
            int a = edges[i].a;
            int b = edges[i].b;

            R3D_LinePoint points[2] = {
                {
                    .position = source->vertices[a].position,
                    .color = source->vertices[a].color
                },
                {
                    .position = source->vertices[b].position,
                    .color = source->vertices[b].color
                }
            };

            R3D_LinePath path = {
                .points = points,
                .count = 2,
                .style = style
            };

            R3D_MeshData segment =
                style.widthMode == R3D_LINE_WIDTH_SCREEN
                    ? R3D_GenMeshDataScreenLinePath(&path)
                    : R3D_GenMeshDataLinePath(&path);

            if (!R3D_IsMeshDataValid(segment) ||
                segment.indexCount <= 0)
            {
                R3D_UnloadMeshData(segment);
                R3D_UnloadMeshData(merged);
                goto cleanup;
            }

            if (merged.vertexCount > INT_MAX - segment.vertexCount ||
                merged.indexCount > INT_MAX - segment.indexCount)
            {
                R3D_UnloadMeshData(segment);
                R3D_UnloadMeshData(merged);
                goto cleanup;
            }

            int newVertices = merged.vertexCount + segment.vertexCount;
            int newIndices = merged.indexCount + segment.indexCount;

            if (newVertices > merged.vertexCapacity ||
                newIndices > merged.indexCapacity)
            {
                int reserveVertices = newVertices > merged.vertexCapacity
                    ? (newVertices > INT_MAX / 2 ? newVertices : newVertices * 2)
                    : merged.vertexCapacity;

                int reserveIndices = newIndices > merged.indexCapacity
                    ? (newIndices > INT_MAX / 2 ? newIndices : newIndices * 2)
                    : merged.indexCapacity;

                R3D_ReserveMeshData(
                    &merged, reserveVertices, reserveIndices
                );
            }

            int base = merged.vertexCount;

            for (int j = 0; j < segment.vertexCount; j++)
            {
                merged.vertices[merged.vertexCount++] =
                    segment.vertices[j];
            }

            for (int j = 0; j < segment.indexCount; j++)
            {
                merged.indices[merged.indexCount++] =
                    segment.indices[j] + (uint32_t)base;
            }

            R3D_UnloadMeshData(segment);
            selected++;
        }

        i = end;
    }

    if (selected > 0)
    {
        // Transfer ownership of the generated CPU data.
        result = merged;

        if (outEdgeCount)
        {
            *outEdgeCount = selected;
        }
    }
    else
    {
        R3D_UnloadMeshData(merged);
    }

cleanup:
    free(edges);
    free(canonical);
    free(keys);

    return result;
}


R3D_Mesh R3D_GenMeshTopologyWireframe(const R3D_MeshData* source, R3D_LineStyle style, float creaseAngleDegrees, float weldEpsilon, int* outEdgeCount)
{
    R3D_Mesh mesh = {0};
    R3D_MeshData data = R3D_GenMeshDataTopologyWireframe(source, style, creaseAngleDegrees, weldEpsilon, outEdgeCount);

    if (!R3D_IsMeshDataValid(data))
    {
        R3D_UnloadMeshData(data);
        return mesh;
    }

    mesh = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, data, NULL);

    if (R3D_IsMeshValid(mesh))
    {
        mesh.shadowCastMode = R3D_SHADOW_CAST_DISABLED;
    }
    else if (outEdgeCount)
    {
        *outEdgeCount = 0;
    }

    R3D_UnloadMeshData(data);
    return mesh;
}

bool R3D_UpdateTopologyWireframe(R3D_Mesh* mesh, const R3D_MeshData* source, R3D_LineStyle style, float creaseAngleDegrees, float weldEpsilon, int* outEdgeCount)
{
    if (!mesh || !R3D_IsMeshValid(*mesh) ||
        mesh->primitiveType != R3D_PRIMITIVE_TRIANGLES)
    {
        return false;
    }

    int selectedEdges = 0;
    R3D_MeshData data = R3D_GenMeshDataTopologyWireframe(source, style, creaseAngleDegrees, weldEpsilon, &selectedEdges);

    if (!R3D_IsMeshDataValid(data))
    {
        R3D_UnloadMeshData(data);
        return false;
    }

    bool updated = R3D_UpdateMesh(mesh, data, NULL);

    if (updated && outEdgeCount)
    {
        *outEdgeCount = selectedEdges;
    }

    R3D_UnloadMeshData(data);
    return updated;
}
