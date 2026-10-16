#include <r3d/r3d_manifold.h>
#include <r3d/r3d_vertex.h>

#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>

// MeshGL property layout:
// 0-2   position
// 3-5   normal
// 6-7   UV
// 8-11  RGBA
#define R3D_MANIFOLD_PROPERTIES 12

static unsigned char r3d_manifold_color_byte(float value)
{
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return (unsigned char)(value * 255.0f + 0.5f);
}

ManifoldMeshGL* R3D_MeshDataToManifoldMeshGL(const R3D_MeshData* data)
{
    if (!data || !data->vertices || data->vertexCount <= 0 || data->indexCount < 0)
    {
        return NULL;
    }

    size_t vertexCount = (size_t)data->vertexCount;
    size_t indexCount = data->indexCount > 0 ? (size_t)data->indexCount : vertexCount;

    if (indexCount == 0 || indexCount % 3 != 0 || (data->indexCount > 0 && !data->indices))
    {
        return NULL;
    }

    if (vertexCount > SIZE_MAX / (R3D_MANIFOLD_PROPERTIES * sizeof(float)) ||
        indexCount > SIZE_MAX / sizeof(uint32_t))
    {
        return NULL;
    }

    float* properties = malloc(vertexCount * R3D_MANIFOLD_PROPERTIES * sizeof(float));
    uint32_t* indices = malloc(indexCount * sizeof(uint32_t));

    if (!properties || !indices)
    {
        free(properties);
        free(indices);
        return NULL;
    }

    for (size_t i = 0; i < vertexCount; i++)
    {
        const R3D_Vertex* vertex = &data->vertices[i];
        float* p = properties + i * R3D_MANIFOLD_PROPERTIES;

        Vector3 pos = vertex->position;
        Vector3 normal = R3D_UnpackNormal(vertex->normal);
        Vector2 uv = R3D_UnpackTexCoord(vertex->texcoord);

        if (!isfinite(pos.x) || !isfinite(pos.y) || !isfinite(pos.z) ||
            !isfinite(uv.x) || !isfinite(uv.y))
        {
            free(properties);
            free(indices);
            return NULL;
        }

        p[0] = pos.x;
        p[1] = pos.y;
        p[2] = pos.z;
        p[3] = normal.x;
        p[4] = normal.y;
        p[5] = normal.z;
        p[6] = uv.x;
        p[7] = uv.y;
        p[8] = vertex->color.r / 255.0f;
        p[9] = vertex->color.g / 255.0f;
        p[10] = vertex->color.b / 255.0f;
        p[11] = vertex->color.a / 255.0f;
    }

    for (size_t i = 0; i < indexCount; i++)
    {
        uint32_t index = data->indexCount > 0 ? data->indices[i] : (uint32_t)i;

        if (index >= vertexCount)
        {
            free(properties);
            free(indices);
            return NULL;
        }

        indices[i] = index;
    }

    ManifoldMeshGL* raw = manifold_meshgl(
        manifold_alloc_meshgl(),
        properties,
        vertexCount,
        R3D_MANIFOLD_PROPERTIES,
        indices,
        indexCount / 3
    );

    // Restore geometric connectivity across UV and normal seams.
    ManifoldMeshGL* merged = raw ? manifold_meshgl_merge(manifold_alloc_meshgl(), raw) : NULL;

    if (raw) manifold_delete_meshgl(raw);

    free(properties);
    free(indices);

    return merged;
}

R3D_MeshData R3D_ManifoldMeshGLToMeshData(ManifoldMeshGL* mesh)
{
    R3D_MeshData result = {0};

    if (!mesh) return result;

    size_t vertexCount = manifold_meshgl_num_vert(mesh);
    size_t triangleCount = manifold_meshgl_num_tri(mesh);
    size_t propertyCount = manifold_meshgl_num_prop(mesh);

    if (vertexCount == 0 || triangleCount == 0 || propertyCount < 3 ||
        vertexCount > INT_MAX || triangleCount > INT_MAX / 3)
    {
        return result;
    }

    if (vertexCount > SIZE_MAX / propertyCount / sizeof(float))
    {
        return result;
    }

    size_t indexCount = triangleCount * 3;
    float* properties = malloc(vertexCount * propertyCount * sizeof(float));
    uint32_t* indices = malloc(indexCount * sizeof(uint32_t));

    if (!properties || !indices)
    {
        free(properties);
        free(indices);
        return result;
    }

    manifold_meshgl_vert_properties(properties, mesh);
    manifold_meshgl_tri_verts(indices, mesh);

    result = R3D_LoadMeshData((int)vertexCount, (int)indexCount);

    if (!result.vertices || !result.indices)
    {
        R3D_UnloadMeshData(result);
        result = (R3D_MeshData){0};
        goto cleanup;
    }

    result.vertexCount = (int)vertexCount;
    result.indexCount = (int)indexCount;

    for (size_t i = 0; i < vertexCount; i++)
    {
        const float* p = properties + i * propertyCount;

        Vector3 position = {p[0], p[1], p[2]};
        Vector3 normal = {0.0f, 1.0f, 0.0f};
        Vector2 uv = {0.0f, 0.0f};
        Vector4 tangent = {1.0f, 0.0f, 0.0f, 1.0f};
        Color color = WHITE;

        if (!isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z))
        {
            R3D_UnloadMeshData(result);
            result = (R3D_MeshData){0};
            goto cleanup;
        }

        if (propertyCount >= 6)
        {
            normal = (Vector3){p[3], p[4], p[5]};

            if (!isfinite(normal.x) || !isfinite(normal.y) || !isfinite(normal.z))
            {
                R3D_UnloadMeshData(result);
                result = (R3D_MeshData){0};
                goto cleanup;
            }
        }

        if (propertyCount >= 8)
        {
            uv = (Vector2){p[6], p[7]};

            if (!isfinite(uv.x) || !isfinite(uv.y))
            {
                R3D_UnloadMeshData(result);
                result = (R3D_MeshData){0};
                goto cleanup;
            }
        }

        if (propertyCount >= R3D_MANIFOLD_PROPERTIES)
        {
            color = (Color){
                r3d_manifold_color_byte(p[8]),
                r3d_manifold_color_byte(p[9]),
                r3d_manifold_color_byte(p[10]),
                r3d_manifold_color_byte(p[11])
            };
        }

        result.vertices[i] = R3D_MakeVertex(position, uv, normal, tangent, color);
    }

    for (size_t i = 0; i < indexCount; i++)
    {
        if (indices[i] >= vertexCount)
        {
            R3D_UnloadMeshData(result);
            result = (R3D_MeshData){0};
            goto cleanup;
        }

        result.indices[i] = indices[i];
    }

    if (propertyCount < 6)
    {
        R3D_GenMeshDataNormals(&result, R3D_PRIMITIVE_TRIANGLES);
    }

    R3D_GenMeshDataTangents(&result, R3D_PRIMITIVE_TRIANGLES);

cleanup:
    free(properties);
    free(indices);

    return result;
}