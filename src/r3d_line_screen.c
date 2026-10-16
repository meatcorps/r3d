
#include <r3d/r3d_line.h>
#include <r3d/r3d_core.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// ========================================
// SCREEN-LINE VERTEX EFFECT
// ========================================


static const char* R3D_SCREEN_LINE_EFFECT =
    "uniform vec2 lineViewport;\n"
    "uniform float lineScale = 1.0;\n"
    "uniform mat4 lineInverseMvp;\n"
    "\n"
    "void vertex() {\n"
    "    vec4 centerClip = MATRIX_VIEW_PROJECTION * MATRIX_MODEL * vec4(POSITION, 1.0);\n"
    "    if (centerClip.w <= 0.00001) return;\n"
    "\n"
    "    // Each segment has its own direction, so no projected\n"
    "    // miter or outside-corner classification is needed.\n"
    "    vec3 nextPosition = POSITION + TANGENT.xyz * 0.05;\n"
    "    vec4 nextClip = MATRIX_VIEW_PROJECTION * MATRIX_MODEL * vec4(nextPosition, 1.0);\n"
    "\n"
    "    vec2 direction = vec2(1.0, 0.0);\n"
    "\n"
    "    if (nextClip.w > 0.00001) {\n"
    "        vec2 difference = (\n"
    "            nextClip.xy / nextClip.w - centerClip.xy / centerClip.w\n"
    "        ) * lineViewport;\n"
    "\n"
    "        float projectedLength = length(difference);\n"
    "        if (projectedLength > 0.00001) {\n"
    "            direction = difference / projectedLength;\n"
    "        }\n"
    "    }\n"
    "\n"
    "    vec2 side = vec2(-direction.y, direction.x);\n"
    "    // Width is stored in boneWeights.zw as 16-bit fixed-point data.\n"
    "    float encodedWidth = floor(aBoneWeights.z * 255.0 + 0.5)\n"
    "        + 256.0 * floor(aBoneWeights.w * 255.0 + 0.5);\n"
    "    float halfWidth = encodedWidth * (1.0 / 32.0) * max(lineScale, 0.0);\n"
    "    vec2 pixelOffset;\n"
    "\n"
    "    if (aBoneIndices.y == 1) {\n"
    "        // A circular join or endpoint cap.\n"
    "        vec2 circlePosition = aBoneWeights.xy * 2.0 - 1.0;\n"
    "        if (aBoneIndices.z == 1) circlePosition = vec2(0.0);\n"
    "        pixelOffset = halfWidth * (\n"
    "            direction * circlePosition.x + side * circlePosition.y\n"
    "        );\n"
    "    } else {\n"
    "        // A regular segment quad with square ends.\n"
    "        float sideSign = TEXCOORD.y * 2.0 - 1.0;\n"
    "        pixelOffset = side * sideSign * halfWidth;\n"
    "    }\n"
    "\n"
    "    vec4 expandedClip = centerClip;\n"
    "    expandedClip.xy += (\n"
    "        2.0 * pixelOffset / max(lineViewport, vec2(1.0))\n"
    "    ) * centerClip.w;\n"
    "\n"
    "    vec4 expandedLocal = lineInverseMvp * expandedClip;\n"
    "    if (abs(expandedLocal.w) > 0.000001) {\n"
    "        POSITION = expandedLocal.xyz / expandedLocal.w;\n"
    "    }\n"
    "}\n";

// ========================================
// INTERNAL HELPERS
// ========================================

#define R3D_SCREEN_LINE_CAP_STEPS 12

static unsigned char screen_line_encode(float value)
{
    value = Clamp(value, -1.0f, 1.0f);
    return (unsigned char)((value + 1.0f) * 127.5f + 0.5f);
}

static R3D_Vertex screen_line_vertex(Vector3 position, Color color, Vector3 before, Vector3 after, float distance, float side, int join, bool cap, bool center, float along, float across, float width)
{
    R3D_Vertex vertex = R3D_MakeVertex(
        position,
        (Vector2){distance, side},
        before,
        (Vector4){after.x, after.y, after.z, 1.0f},
        color
    );

    vertex.boneIndices[0] = (uint8_t)join;
    vertex.boneIndices[1] = cap ? 1 : 0;
    vertex.boneIndices[2] = center ? 1 : 0;

    vertex.boneWeights[0] = screen_line_encode(along);
    vertex.boneWeights[1] = screen_line_encode(across);

    // Screen-line meshes are not skinned.
    // Store width as a 16-bit fixed-point value using two free bytes.
    uint16_t packedWidth = (uint16_t)roundf(
        Clamp(width * 16.0f, 1.0f, 65535.0f)
    );

    vertex.boneWeights[2] = (uint8_t)(packedWidth & 0xFF);
    vertex.boneWeights[3] = (uint8_t)(packedWidth >> 8);

    return vertex;
}

static Vector3 screen_line_direction(Vector3 from, Vector3 to)
{
    return Vector3Normalize(Vector3Subtract(to, from));
}

static void screen_line_add_section(R3D_MeshData* data, Vector3 position, Color color, Vector3 before, Vector3 after, float distance, int join, float width)
{
    data->vertices[data->vertexCount++] = screen_line_vertex(
        position, color, before, after, distance, 0.0f,
        join, false, false, 0.0f, 0.0f, width
    );

    data->vertices[data->vertexCount++] = screen_line_vertex(
        position, color, before, after, distance, 1.0f,
        join, false, false, 0.0f, 0.0f, width
    );
}

static void screen_line_add_cap(R3D_MeshData* data, Vector3 position, Color color, Vector3 direction, float distance, bool start, float width)
{
    for (int i = 0; i < R3D_SCREEN_LINE_CAP_STEPS; i++)
    {
        float angleA = PI * (float)i / R3D_SCREEN_LINE_CAP_STEPS;
        float angleB = PI * (float)(i + 1) / R3D_SCREEN_LINE_CAP_STEPS;

        float alongA = sinf(angleA) * (start ? -1.0f : 1.0f);
        float alongB = sinf(angleB) * (start ? -1.0f : 1.0f);

        float acrossA = cosf(angleA) * (start ? 1.0f : -1.0f);
        float acrossB = cosf(angleB) * (start ? 1.0f : -1.0f);

        uint32_t index = (uint32_t)data->vertexCount;

        data->vertices[data->vertexCount++] = screen_line_vertex(
            position, color, direction, direction,
            distance, 0.0f, 0, true, true, 0.0f, 0.0f, width
        );

        data->vertices[data->vertexCount++] = screen_line_vertex(
            position, color, direction, direction,
            distance, 0.0f, 0, true, false, alongA, acrossA, width
        );

        data->vertices[data->vertexCount++] = screen_line_vertex(
            position, color, direction, direction,
            distance, 0.0f, 0, true, false, alongB, acrossB, width
        );

        data->indices[data->indexCount++] = index;
        data->indices[data->indexCount++] = index + 1;
        data->indices[data->indexCount++] = index + 2;
    }
}

// ========================================
// PUBLIC API
// ========================================


static int screen_line_steps(const R3D_LineArc* arc)
{
    float sweep = fabsf(arc->sweepAngle);

    if (!isfinite(sweep) || sweep < 0.0001f ||
        sweep >= 2.0f * PI - 0.0001f)
    {
        return 1;
    }

    return arc->subdivisions > 0 ? arc->subdivisions : 16;
}

static float screen_line_point_width(const R3D_LinePath* path, int index)
{
    float width = path->points[index].width;
    return width == 0.0f ? path->style.width : width;
}

static R3D_MeshData screen_line_build_data(const R3D_LinePath* path)
{
    R3D_MeshData result = {0};

    if (!path || !path->points || path->count < 2 ||
        (path->closed && path->count < 3) ||
        path->style.widthMode != R3D_LINE_WIDTH_SCREEN)
    {
        return result;
    }

    // Our two-byte width representation supports up to 4095.9375 px.
    // Validate all source widths before generating any geometry.
    for (int i = 0; i < path->count; i++)
    {
        float width = screen_line_point_width(path, i);

        if (!isfinite(width) || width <= 0.0f || width > 4095.9375f)
        {
            return result;
        }
    }

    // Reuse the existing path and arc tessellation.
    R3D_LinePath thinPath = *path;
    thinPath.style.widthMode = R3D_LINE_WIDTH_SCREEN;

    R3D_MeshData samples = R3D_GenMeshDataLinePath(&thinPath);

    if (!R3D_IsMeshDataValid(samples))
    {
        R3D_UnloadMeshData(samples);
        return result;
    }

    int originalCount = samples.vertexCount;
    float* widths = malloc((size_t)originalCount * sizeof(*widths));

    if (!widths)
    {
        R3D_UnloadMeshData(samples);
        return result;
    }

    // Assign a width to every tessellated vertex.
    // This matches the subdivision rules of R3D_GenMeshDataLinePath().
    int sourceSegments = path->closed ? path->count : path->count - 1;
    int sampleIndex = 0;

    for (int segment = 0; segment < sourceSegments; segment++)
    {
        int next = (segment + 1) % path->count;

        float fromWidth = screen_line_point_width(path, segment);
        float toWidth = screen_line_point_width(path, next);

        int steps = screen_line_steps(&path->points[segment].arcToNext);

        for (int step = 0; step < steps; step++)
        {
            int a = (sampleIndex + step) % originalCount;
            int b = (sampleIndex + step + 1) % originalCount;

            float t0 = (float)step / (float)steps;
            float t1 = (float)(step + 1) / (float)steps;

            widths[a] = Lerp(fromWidth, toWidth, t0);
            widths[b] = Lerp(fromWidth, toWidth, t1);
        }

        sampleIndex += steps;
    }

    // Remove consecutive coincident positions, preserving widths.
    int count = 0;

    for (int i = 0; i < originalCount; i++)
    {
        R3D_Vertex vertex = samples.vertices[i];

        if (!isfinite(vertex.position.x) ||
            !isfinite(vertex.position.y) ||
            !isfinite(vertex.position.z))
        {
            free(widths);
            R3D_UnloadMeshData(samples);
            return result;
        }

        if (count > 0 &&
            Vector3DistanceSqr(
                vertex.position,
                samples.vertices[count - 1].position
            ) < 0.000000000001f)
        {
            widths[count - 1] = widths[i];
            continue;
        }

        samples.vertices[count] = vertex;
        widths[count] = widths[i];
        count++;
    }

    if (path->closed && count > 1 &&
        Vector3DistanceSqr(
            samples.vertices[0].position,
            samples.vertices[count - 1].position
        ) < 0.000000000001f)
    {
        count--;
    }

    if (count < (path->closed ? 3 : 2) ||
        count > INT_MAX / (6 + 6 * R3D_SCREEN_LINE_CAP_STEPS))
    {
        free(widths);
        R3D_UnloadMeshData(samples);
        return result;
    }

    int segmentCount = path->closed ? count : count - 1;

    int quadVertices = segmentCount * 4;
    int quadIndices = segmentCount * 6;

    // Maximum capacity: two semicircular fans per point.
    // Flat endpoints may use fewer vertices than allocated.
    int roundVertices = count * 6 * R3D_SCREEN_LINE_CAP_STEPS;

    R3D_MeshData data = R3D_LoadMeshData(
        quadVertices + roundVertices,
        quadIndices + roundVertices
    );

    if (!data.vertices || !data.indices)
    {
        R3D_UnloadMeshData(data);
        free(widths);
        R3D_UnloadMeshData(samples);
        return result;
    }

    float distance = 0.0f;

    // Independent segment quads remain unchanged.
    // Only their per-vertex widths are different.
    for (int i = 0; i < segmentCount; i++)
    {
        int next = (i + 1) % count;

        Vector3 from = samples.vertices[i].position;
        Vector3 to = samples.vertices[next].position;
        Vector3 direction = screen_line_direction(from, to);

        Color fromColor = samples.vertices[i].color;
        Color toColor = samples.vertices[next].color;

        float segmentLength = Vector3Distance(from, to);
        uint32_t base = (uint32_t)data.vertexCount;

        screen_line_add_section(
            &data, from, fromColor,
            direction, direction, distance, 0, widths[i]
        );

        screen_line_add_section(
            &data, to, toColor,
            direction, direction, distance + segmentLength, 0, widths[next]
        );

        data.indices[data.indexCount++] = base + 0;
        data.indices[data.indexCount++] = base + 2;
        data.indices[data.indexCount++] = base + 3;

        data.indices[data.indexCount++] = base + 0;
        data.indices[data.indexCount++] = base + 3;
        data.indices[data.indexCount++] = base + 1;

        distance += segmentLength;
    }

    // Full circular joins preserve the stable screen-space topology.
    for (int i = 0; i < count; i++)
    {
        if (!path->closed)
        {
            if (i == 0 && !(path->style.caps & R3D_LINE_CAP_START)) continue;
            if (i == count - 1 && !(path->style.caps & R3D_LINE_CAP_END)) continue;
        }

        int next = (i + 1) % count;
        int previous = (i + count - 1) % count;

        Vector3 position = samples.vertices[i].position;
        Color color = samples.vertices[i].color;

        Vector3 direction;

        if (!path->closed && i == count - 1)
        {
            direction = screen_line_direction(
                samples.vertices[previous].position, position
            );
        }
        else
        {
            direction = screen_line_direction(
                position, samples.vertices[next].position
            );
        }

        screen_line_add_cap(
            &data, position, color, direction,
            0.0f, true, widths[i]
        );

        screen_line_add_cap(
            &data, position, color, direction,
            0.0f, false, widths[i]
        );
    }

    free(widths);
    R3D_UnloadMeshData(samples);

    return data;
}

R3D_Mesh R3D_GenMeshScreenLinePath(const R3D_LinePath* path)
{
    R3D_Mesh mesh = {0};
    R3D_MeshData data = screen_line_build_data(path);

    if (!R3D_IsMeshDataValid(data))
    {
        R3D_UnloadMeshData(data);
        return mesh;
    }

    mesh = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, data, NULL);
    mesh.shadowCastMode = R3D_SHADOW_CAST_DISABLED;

    R3D_UnloadMeshData(data);

    return mesh;
}

bool R3D_UpdateScreenLineMesh(R3D_Mesh* mesh, const R3D_LinePath* path)
{
    if (!mesh || !R3D_IsMeshValid(*mesh) ||
        mesh->primitiveType != R3D_PRIMITIVE_TRIANGLES)
    {
        return false;
    }

    R3D_MeshData data = screen_line_build_data(path);

    if (!R3D_IsMeshDataValid(data))
    {
        R3D_UnloadMeshData(data);
        return false;
    }

    bool updated = R3D_UpdateMesh(mesh, data, NULL);

    R3D_UnloadMeshData(data);

    return updated;
}

R3D_SurfaceEffect* R3D_LoadScreenLineEffect(void)
{
    return R3D_LoadSurfaceEffectFromMemory(R3D_SCREEN_LINE_EFFECT);
}

void R3D_DrawScreenLineMesh(R3D_Mesh mesh, R3D_Material material, R3D_SurfaceEffect* effect, float widthScale, Matrix transform)
{
    if (!effect || !R3D_IsMeshValid(mesh) ||
        !isfinite(widthScale) || widthScale <= 0.0f)
    {
        return;
    }

    int width = 0;
    int height = 0;

    R3D_GetResolution(&width, &height);

    if (width <= 0 || height <= 0)
    {
        return;
    }

    Vector2 viewport = {(float)width, (float)height};

    // Raymath MatrixMultiply(A, B) corresponds to B * A.
    Matrix mvp = MatrixMultiply(transform, R3D_GetMatrixViewProjection());

    if (fabsf(MatrixDeterminant(mvp)) < 0.000000000001f)
    {
        return;
    }

    Matrix inverseMvp = MatrixInvert(mvp);

    R3D_SetSurfaceEffectUniform(effect, "lineViewport", &viewport);
    R3D_SetSurfaceEffectUniform(effect, "lineScale", &widthScale);
    R3D_SetSurfaceEffectUniform(effect, "lineInverseMvp", MatrixToFloat(inverseMvp));

    // Screen ribbons are not physically illuminated surfaces.
    // Screen ribbons participate in normal depth testing.
    // Opaque rendering also writes depth between ribbon triangles.
    material.unlit = true;
    material.cullMode = R3D_CULL_NONE;
    material.transparencyMode = R3D_TRANSPARENCY_OPAQUE;
    material.depth.mode = R3D_COMPARE_LESS;

    R3D_PushSurfaceEffect(effect);
    R3D_DrawMeshPro(mesh, material, transform);
    R3D_PopSurfaceEffect();
}
