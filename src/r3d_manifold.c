#include <r3d/r3d_manifold.h>
#include <r3d/r3d_vertex.h>
#include <raymath.h>
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

// ========================================
// CONVENIENCE OPERATIONS
// ========================================

static void r3d_manifold_apply_slice_uvs(R3D_MeshData* data, Vector3 normal, float offset)
{
    if (!data || !R3D_IsMeshDataValid(*data)) return;

    normal = Vector3Normalize(normal);

    Vector3 reference = fabsf(normal.y) < 0.9f
        ? (Vector3){0.0f, 1.0f, 0.0f}
    : (Vector3){0.0f, 0.0f, 1.0f};

    Vector3 axisU = Vector3Normalize(Vector3CrossProduct(reference, normal));
    Vector3 axisV = Vector3CrossProduct(normal, axisU);

    BoundingBox bounds = R3D_CalculateMeshDataBoundingBox(*data);
    Vector3 size = Vector3Subtract(bounds.max, bounds.min);

    float tolerance = 0.0001f * fmaxf(1.0f, fmaxf(size.x, fmaxf(size.y, size.z)));

    for (int i = 0; i < data->vertexCount; i++)
    {
        R3D_Vertex* vertex = &data->vertices[i];

        Vector3 vertexNormal = R3D_UnpackNormal(vertex->normal);
        Vector3 position = vertex->position;

        float distance = Vector3DotProduct(position, normal) - offset;
        float alignment = Vector3DotProduct(vertexNormal, normal);

        // Only vertices on the new planar cut surface.
        if (fabsf(distance) > tolerance || fabsf(alignment) < 0.99f)
        {
            continue;
        }

        Vector2 uv = {
            0.5f + 0.5f * Vector3DotProduct(position, axisU),
            0.5f + 0.5f * Vector3DotProduct(position, axisV)
        };

        R3D_PackTexCoord(vertex->texcoord, uv);

        // New caps inherit the existing material without transparent colors.
        vertex->color = WHITE;
    }

    R3D_GenMeshDataTangents(data, R3D_PRIMITIVE_TRIANGLES);
}

static R3D_MeshData r3d_manifold_convert_solid(ManifoldManifold* solid, const Vector3* sliceNormal, float sliceOffset)
{
    R3D_MeshData data = {0};

    if (!solid || manifold_status(solid) != MANIFOLD_NO_ERROR || manifold_is_empty(solid))
    {
        return data;
    }

    ManifoldManifold* withNormals = manifold_calculate_normals(
        manifold_alloc_manifold(), solid, 0, 52.5
    );

    if (!withNormals) return data;

    if (manifold_status(withNormals) == MANIFOLD_NO_ERROR)
    {
        ManifoldMeshGL* output = manifold_get_meshgl(
            manifold_alloc_meshgl(), withNormals
        );

        if (output)
        {
            data = R3D_ManifoldMeshGLToMeshData(output);
            manifold_delete_meshgl(output);
        }
    }

    manifold_delete_manifold(withNormals);

    if (R3D_IsMeshDataValid(data))
    {
        if (sliceNormal)
        {
            r3d_manifold_apply_slice_uvs(&data, *sliceNormal, sliceOffset);
        }
        else if (manifold_num_prop(solid) < 5)
        {
            R3D_GenMeshDataUVsPlanar(
                &data, (Vector2){1.0f, 1.0f}, (Vector3){0.0f, 0.0f, 1.0f}
            );

            R3D_GenMeshDataTangents(&data, R3D_PRIMITIVE_TRIANGLES);
        }
    }

    return data;
}

static bool r3d_manifold_upload_data(R3D_Mesh* mesh, R3D_MeshData* data)
{
    if (!mesh || !data || !R3D_IsMeshDataValid(*data))
    {
        return false;
    }

    R3D_MeshData* linked = mesh->meshData;

    if (R3D_IsMeshValid(*mesh))
    {
        if (!R3D_UpdateMesh(mesh, *data, NULL))
        {
            return false;
        }

        mesh->primitiveType = R3D_PRIMITIVE_TRIANGLES;
    }
    else
    {
        R3D_Mesh uploaded = R3D_LoadMesh(
            R3D_PRIMITIVE_TRIANGLES, *data, NULL
        );

        if (!R3D_IsMeshValid(uploaded))
        {
            return false;
        }

        // R3D_LoadMesh returns a new struct, so preserve the CPU link.
        uploaded.meshData = linked;
        *mesh = uploaded;
    }

    if (linked)
    {
        // The existing linked CPU buffers are caller-owned.
        // Replace their contents without changing the linked struct's address.
        R3D_UnloadMeshData(*linked);
        *linked = *data;

        // Ownership of the new buffers has moved to the linked struct.
        *data = (R3D_MeshData){0};
    }

    return true;
}

bool R3D_UpdateMeshFromManifold(R3D_Mesh* mesh, ManifoldManifold* solid)
{
    if (!mesh || !solid) return false;

    R3D_MeshData data = r3d_manifold_convert_solid(solid, NULL, 0.0f);

    if (!R3D_IsMeshDataValid(data)) return false;

    bool success = r3d_manifold_upload_data(mesh, &data);

    R3D_UnloadMeshData(data);
    return success;
}

static bool r3d_manifold_collect(ManifoldManifoldVec* all, ManifoldManifold* solid)
{
    if (!solid || manifold_is_empty(solid)) return true;

    ManifoldManifoldVec* components = manifold_decompose(
        manifold_alloc_manifold_vec(), solid
    );

    if (!components) return false;

    size_t count = manifold_manifold_vec_length(components);

    for (size_t i = 0; i < count; i++)
    {
        ManifoldManifold* component = manifold_manifold_vec_get(
            manifold_alloc_manifold(), components, i
        );

        if (!component)
        {
            manifold_delete_manifold_vec(components);
            return false;
        }

        manifold_manifold_vec_push_back(all, component);
        manifold_delete_manifold(component);
    }

    manifold_delete_manifold_vec(components);
    return true;
}

static R3D_ManifoldResult r3d_manifold_upload_components(R3D_Mesh* target, ManifoldManifold* first, ManifoldManifold* second, const Vector3* sliceNormal, float sliceOffset)
{
    R3D_ManifoldResult result = {
        .status = R3D_MANIFOLD_INVALID_INPUT,
        .manifoldError = MANIFOLD_NO_ERROR
    };

    if (!target || !first) return result;

    ManifoldError error = manifold_status(first);

    if (error == MANIFOLD_NO_ERROR && second)
    {
        error = manifold_status(second);
    }

    if (error != MANIFOLD_NO_ERROR)
    {
        result.status = R3D_MANIFOLD_NATIVE_ERROR;
        result.manifoldError = error;
        return result;
    }

    ManifoldManifoldVec* all = manifold_manifold_empty_vec(
        manifold_alloc_manifold_vec()
    );

    if (!all)
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
        return result;
    }

    R3D_MeshData primaryData = {0};
    R3D_Mesh* extras = NULL;
    R3D_MeshData* extraData = NULL;
    int uploadedExtras = 0;

    if (!r3d_manifold_collect(all, first) ||
        !r3d_manifold_collect(all, second))
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
        goto cleanup;
    }

    size_t count = manifold_manifold_vec_length(all);

    if (count == 0)
    {
        result.status = R3D_MANIFOLD_EMPTY_RESULT;
        goto cleanup;
    }

    if (count > INT_MAX || (count - 1) > SIZE_MAX / sizeof(R3D_Mesh))
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
        goto cleanup;
    }

    // Prepare the main geometry before modifying any GPU resources.
    ManifoldManifold* primary = manifold_manifold_vec_get(
        manifold_alloc_manifold(), all, 0
    );

    if (!primary)
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
        goto cleanup;
    }

    primaryData = r3d_manifold_convert_solid(primary, sliceNormal, sliceOffset);
    manifold_delete_manifold(primary);

    if (!R3D_IsMeshDataValid(primaryData))
    {
        result.status = R3D_MANIFOLD_CONVERSION_FAILED;
        goto cleanup;
    }

    // Stage additional GPU meshes first. If any fail, do not update target.
    if (count > 1)
    {
        extras = calloc(count - 1, sizeof(R3D_Mesh));
        extraData = calloc(count - 1, sizeof(R3D_MeshData));

        if (!extras || !extraData)
        {
            result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
            goto cleanup;
        }
    }

    for (size_t i = 1; i < count; i++)
    {
        ManifoldManifold* component = manifold_manifold_vec_get(
            manifold_alloc_manifold(), all, i
        );

        if (!component)
        {
            result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
            goto cleanup;
        }

        extraData[uploadedExtras] = r3d_manifold_convert_solid(component, sliceNormal, sliceOffset);
        manifold_delete_manifold(component);

        if (!R3D_IsMeshDataValid(extraData[uploadedExtras]))
        {
            result.status = R3D_MANIFOLD_CONVERSION_FAILED;
            goto cleanup;
        }

        extras[uploadedExtras] = R3D_LoadMesh(
            R3D_PRIMITIVE_TRIANGLES,
            extraData[uploadedExtras],
            NULL
        );

        if (!R3D_IsMeshValid(extras[uploadedExtras]))
        {
            result.status = R3D_MANIFOLD_UPLOAD_FAILED;
            goto cleanup;
        }

        R3D_LinkMeshData(
            &extras[uploadedExtras],
            &extraData[uploadedExtras]
        );

        uploadedExtras++;
    }

    // Commit the primary result only after additional pieces have succeeded.
    if (!r3d_manifold_upload_data(target, &primaryData))
    {
        result.status = R3D_MANIFOLD_UPLOAD_FAILED;
        goto cleanup;
    }

    result.status = R3D_MANIFOLD_OK;
    result.extraMeshes = extras;
    result.extraData = extraData;
    result.extraCount = uploadedExtras;

    extras = NULL;
    extraData = NULL;

cleanup:
    if (extras)
    {
        for (int i = 0; i < uploadedExtras; i++)
        {
            R3D_UnloadMesh(extras[i]);
        }

        free(extras);
    }

    if (extraData)
    {
        for (int i = 0; i < uploadedExtras + 1 && i < (int)count - 1; i++)
        {
            R3D_UnloadMeshData(extraData[i]);
        }

        free(extraData);
    }

    R3D_UnloadMeshData(primaryData);
    manifold_delete_manifold_vec(all);

    return result;
}

static R3D_ManifoldResult r3d_manifold_boolean(R3D_Mesh* target, const R3D_Mesh* other, ManifoldOpType operation)
{
    R3D_ManifoldResult result = {
        .status = R3D_MANIFOLD_INVALID_INPUT,
        .manifoldError = MANIFOLD_NO_ERROR
    };

    if (!target || !other || !target->meshData || !other->meshData)
    {
        return result;
    }

    const R3D_MeshData* a = target->meshData;
    const R3D_MeshData* b = other->meshData;

    if (!R3D_IsMeshDataValid(*a) || !R3D_IsMeshDataValid(*b))
    {
        return result;
    }

    if (target->primitiveType != R3D_PRIMITIVE_TRIANGLES ||
        other->primitiveType != R3D_PRIMITIVE_TRIANGLES)
    {
        return result;
    }

    ManifoldMeshGL* meshA = R3D_MeshDataToManifoldMeshGL(a);
    ManifoldMeshGL* meshB = R3D_MeshDataToManifoldMeshGL(b);

    ManifoldManifold* solidA = NULL;
    ManifoldManifold* solidB = NULL;
    ManifoldManifold* output = NULL;

    if (!meshA || !meshB)
    {
        result.status = R3D_MANIFOLD_CONVERSION_FAILED;
        goto cleanup;
    }

    solidA = manifold_of_meshgl(manifold_alloc_manifold(), meshA);
    solidB = manifold_of_meshgl(manifold_alloc_manifold(), meshB);

    if (!solidA || !solidB)
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
        goto cleanup;
    }

    ManifoldError error = manifold_status(solidA);

    if (error == MANIFOLD_NO_ERROR)
    {
        error = manifold_status(solidB);
    }

    if (error != MANIFOLD_NO_ERROR)
    {
        result.status = R3D_MANIFOLD_NATIVE_ERROR;
        result.manifoldError = error;
        goto cleanup;
    }

    output = manifold_boolean(
        manifold_alloc_manifold(), solidA, solidB, operation
    );

    if (!output)
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
        goto cleanup;
    }

    result = r3d_manifold_upload_components(target, output, NULL, NULL, 0.0f);

cleanup:
    if (output) manifold_delete_manifold(output);
    if (solidB) manifold_delete_manifold(solidB);
    if (solidA) manifold_delete_manifold(solidA);
    if (meshB) manifold_delete_meshgl(meshB);
    if (meshA) manifold_delete_meshgl(meshA);

    return result;
}

R3D_ManifoldResult R3D_ManifoldUnion(R3D_Mesh* mesh, const R3D_Mesh* other)
{
    return r3d_manifold_boolean(mesh, other, MANIFOLD_ADD);
}

R3D_ManifoldResult R3D_ManifoldSubtract(R3D_Mesh* mesh, const R3D_Mesh* other)
{
    return r3d_manifold_boolean(mesh, other, MANIFOLD_SUBTRACT);
}

R3D_ManifoldResult R3D_ManifoldIntersect(R3D_Mesh* mesh, const R3D_Mesh* other)
{
    return r3d_manifold_boolean(mesh, other, MANIFOLD_INTERSECT);
}

R3D_ManifoldResult R3D_ManifoldExtrude(R3D_Mesh* mesh, const Vector2* outline, int pointCount, float height)
{
    R3D_ManifoldResult result = {
        .status = R3D_MANIFOLD_INVALID_INPUT,
        .manifoldError = MANIFOLD_NO_ERROR
    };

    if (!mesh || !outline || pointCount < 3 ||
        !isfinite(height) || height <= 0.0f)
    {
        return result;
    }

    ManifoldVec2* points = malloc((size_t)pointCount * sizeof(ManifoldVec2));

    if (!points)
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
        return result;
    }

    for (int i = 0; i < pointCount; i++)
    {
        if (!isfinite(outline[i].x) || !isfinite(outline[i].y))
        {
            free(points);
            return result;
        }

        points[i] = (ManifoldVec2){outline[i].x, outline[i].y};
    }

    ManifoldSimplePolygon* polygon = manifold_simple_polygon(
        manifold_alloc_simple_polygon(), points, (size_t)pointCount
    );

    free(points);

    if (!polygon)
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
        return result;
    }

    ManifoldSimplePolygon* contours[1] = {polygon};

    ManifoldPolygons* polygons = manifold_polygons(
        manifold_alloc_polygons(), contours, 1
    );

    ManifoldManifold* solid = polygons ? manifold_extrude(
        manifold_alloc_manifold(), polygons, height, 1, 0.0, 1.0, 1.0
    ) : NULL;

    if (solid)
    {
        result = r3d_manifold_upload_components(mesh, solid, NULL, NULL, 0.0f);
        manifold_delete_manifold(solid);
    }
    else
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
    }

    if (polygons) manifold_delete_polygons(polygons);
    manifold_delete_simple_polygon(polygon);

    return result;
}

R3D_ManifoldResult R3D_ManifoldSlicePlane(R3D_Mesh* mesh, Vector3 normal, float offset)
{
    R3D_ManifoldResult result = {
        .status = R3D_MANIFOLD_INVALID_INPUT,
        .manifoldError = MANIFOLD_NO_ERROR
    };

    if (!mesh || !mesh->meshData ||
        !isfinite(normal.x) || !isfinite(normal.y) ||
        !isfinite(normal.z) || !isfinite(offset) ||
        (normal.x == 0.0f && normal.y == 0.0f && normal.z == 0.0f))
    {
        return result;
    }

    if (mesh->primitiveType != R3D_PRIMITIVE_TRIANGLES ||
    !R3D_IsMeshDataValid(*mesh->meshData))
    {
        return result;
    }

    ManifoldMeshGL* input = R3D_MeshDataToManifoldMeshGL(mesh->meshData);

    if (!input)
    {
        result.status = R3D_MANIFOLD_CONVERSION_FAILED;
        return result;
    }

    ManifoldManifold* solid = manifold_of_meshgl(
        manifold_alloc_manifold(), input
    );

    if (!solid)
    {
        result.status = R3D_MANIFOLD_OUT_OF_MEMORY;
    }
    else if (manifold_status(solid) != MANIFOLD_NO_ERROR)
    {
        result.status = R3D_MANIFOLD_NATIVE_ERROR;
        result.manifoldError = manifold_status(solid);
    }
    else
    {
        ManifoldManifoldPair halves = manifold_split_by_plane(
            manifold_alloc_manifold(),
            manifold_alloc_manifold(),
            solid,
            normal.x, normal.y, normal.z, offset
        );

        result = r3d_manifold_upload_components(mesh, halves.first, halves.second, &normal, offset);

        if (halves.first) manifold_delete_manifold(halves.first);
        if (halves.second) manifold_delete_manifold(halves.second);
    }

    if (solid) manifold_delete_manifold(solid);
    manifold_delete_meshgl(input);

    return result;
}

void R3D_UnloadManifoldResult(R3D_ManifoldResult* result)
{
    if (!result) return;

    for (int i = 0; i < result->extraCount; i++)
    {
        R3D_UnloadMesh(result->extraMeshes[i]);

        if (result->extraData)
        {
            R3D_UnloadMeshData(result->extraData[i]);
        }
    }

    free(result->extraMeshes);
    free(result->extraData);

    *result = (R3D_ManifoldResult){0};
}