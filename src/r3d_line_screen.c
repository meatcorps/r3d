
#include <r3d/r3d_line.h>
#include <r3d/r3d_core.h>
#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

// ========================================
// SCREEN-LINE VERTEX EFFECT
// ========================================

static const char* R3D_SCREEN_LINE_EFFECT =
    "uniform vec2 lineViewport;\n"
    "uniform float lineWidth = 4.0;\n"
    "uniform float lineMiterLimit = 4.0;\n"
    "uniform mat4 lineInverseMvp;\n"
    "\n"
    "vec2 lineSafeDirection(vec2 value) {\n"
    "    float lengthValue = length(value);\n"
    "    return lengthValue > 0.00001 ? value / lengthValue : vec2(1.0, 0.0);\n"
    "}\n"
    "\n"
    "void vertex() {\n"
    "    vec4 centerClip = MATRIX_VIEW_PROJECTION * MATRIX_MODEL * vec4(POSITION, 1.0);\n"
    "    if (centerClip.w <= 0.00001) return;\n"
    "\n"
    "    vec3 beforeWorld = POSITION - NORMAL * 0.05;\n"
    "    vec3 afterWorld = POSITION + TANGENT.xyz * 0.05;\n"
    "\n"
    "    vec4 beforeClip = MATRIX_VIEW_PROJECTION * MATRIX_MODEL * vec4(beforeWorld, 1.0);\n"
    "    vec4 afterClip = MATRIX_VIEW_PROJECTION * MATRIX_MODEL * vec4(afterWorld, 1.0);\n"
    "\n"
    "    vec2 centerNdc = centerClip.xy / centerClip.w;\n"
    "    vec2 beforeNdc = beforeClip.w > 0.00001 ? beforeClip.xy / beforeClip.w : centerNdc;\n"
    "    vec2 afterNdc = afterClip.w > 0.00001 ? afterClip.xy / afterClip.w : centerNdc;\n"
    "\n"
    "            vec2 beforeVector = (centerNdc - beforeNdc) * lineViewport;\n"
    "    vec2 afterVector = (afterNdc - centerNdc) * lineViewport;\n"
    "\n"
    "    float beforeLength = length(beforeVector);\n"
    "    float afterLength = length(afterVector);\n"
    "\n"
    "    vec2 beforeDirection = lineSafeDirection(beforeVector);\n"
    "    vec2 afterDirection = lineSafeDirection(afterVector);\n"
    "\n"
    "    // Stabilize joins when one projected leg becomes too short.\n"
    "    if (beforeLength < 1.0) beforeDirection = afterDirection;\n"
    "    if (afterLength < 1.0) afterDirection = beforeDirection;\n"
    "\n"
    "    vec2 beforeSide = vec2(-beforeDirection.y, beforeDirection.x);\n"
    "    vec2 afterSide = vec2(-afterDirection.y, afterDirection.x);\n"
    "\n"
    "    float halfWidth = max(0.0, lineWidth) * 0.5;\n"
    "    vec2 pixelOffset = vec2(0.0);\n"
    "\n"
    "    if (aBoneIndices.y == 1) {\n"
    "        vec2 direction = lineSafeDirection(beforeDirection + afterDirection);\n"
    "        vec2 side = vec2(-direction.y, direction.x);\n"
    "        vec2 capCoordinates = aBoneWeights.xy * 2.0 - 1.0;\n"
    "        if (aBoneIndices.z == 1) capCoordinates = vec2(0.0);\n"
    "        pixelOffset = halfWidth * (direction * capCoordinates.x + side * capCoordinates.y);\n"
    "    } else {\n"
    "        float sideSign = TEXCOORD.y * 2.0 - 1.0;\n"
    "        float turn = beforeDirection.x * afterDirection.y - beforeDirection.y * afterDirection.x;\n"
    "        float alignment = clamp(dot(beforeDirection, afterDirection), -1.0, 1.0);\n"
    "        float bendAmount = 1.0 - alignment;\n"
    "\n"
    "        vec2 selectedSide = (aBoneIndices.x == 0) ? beforeSide : afterSide;\n"
    "        vec2 straightOffset = selectedSide * (sideSign * halfWidth);\n"
    "\n"
    "        vec2 summedSide = beforeSide + afterSide;\n"
    "        vec2 miter = lineSafeDirection(dot(summedSide, summedSide) > 0.000001 ? summedSide : selectedSide);\n"
    "\n"
    "        float divisor = max(abs(dot(miter, afterSide)), 0.0001);\n"
    "        float miterScale = 1.0 / divisor;\n"
    "        float limitValue = max(lineMiterLimit, 1.0);\n"
    "\n"
    "        vec2 limitedMiterOffset = miter * (sideSign * halfWidth * min(miterScale, limitValue));\n"
    "\n"
    "        float outsideSign = (turn >= 0.0) ? -1.0 : 1.0;\n"
    "        bool outsideEdge = (sideSign * outsideSign) > 0.0;\n"
    "\n"
    "        vec2 bevelOffset = outsideEdge\n"
    "            ? selectedSide * (sideSign * halfWidth)\n"
    "            : limitedMiterOffset;\n"
    "\n"
    "        // If the corner is almost straight, behave like a straight segment.\n"
    "        float cornerBlend = smoothstep(0.0005, 0.02, bendAmount);\n"
    "\n"
    "        // Avoid hard popping around the miter limit.\n"
    "        float bevelBlend = smoothstep(limitValue * 0.9, limitValue * 1.1, miterScale);\n"
    "\n"
    "        vec2 cornerOffset = mix(straightOffset, limitedMiterOffset, cornerBlend);\n"
    "        pixelOffset = mix(cornerOffset, bevelOffset, bevelBlend);\n"
    "    }\n"
    "\n"
    "    vec4 expandedClip = centerClip;\n"
    "    expandedClip.xy += (2.0 * pixelOffset / max(lineViewport, vec2(1.0))) * centerClip.w;\n"
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

static R3D_Vertex screen_line_vertex(Vector3 position, Color color, Vector3 before, Vector3 after, float distance, float side, int join, bool cap, bool center, float along, float across)
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

    return vertex;
}

static Vector3 screen_line_direction(Vector3 from, Vector3 to)
{
    return Vector3Normalize(Vector3Subtract(to, from));
}

static void screen_line_add_section(R3D_MeshData* data, Vector3 position, Color color, Vector3 before, Vector3 after, float distance, int join)
{
    data->vertices[data->vertexCount++] = screen_line_vertex(
        position, color, before, after, distance, 0.0f,
        join, false, false, 0.0f, 0.0f
    );

    data->vertices[data->vertexCount++] = screen_line_vertex(
        position, color, before, after, distance, 1.0f,
        join, false, false, 0.0f, 0.0f
    );
}

static void screen_line_add_cap(R3D_MeshData* data, Vector3 position, Color color, Vector3 direction, float distance, bool start)
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
            distance, 0.0f, 0, true, true, 0.0f, 0.0f
        );

        data->vertices[data->vertexCount++] = screen_line_vertex(
            position, color, direction, direction,
            distance, 0.0f, 0, true, false, alongA, acrossA
        );

        data->vertices[data->vertexCount++] = screen_line_vertex(
            position, color, direction, direction,
            distance, 0.0f, 0, true, false, alongB, acrossB
        );

        data->indices[data->indexCount++] = index;
        data->indices[data->indexCount++] = index + 1;
        data->indices[data->indexCount++] = index + 2;
    }
}

// ========================================
// PUBLIC API
// ========================================

R3D_Mesh R3D_GenMeshScreenLinePath(const R3D_LinePath* path)
{
    R3D_Mesh mesh = {0};

    if (!path || !path->points || path->count < 2 ||
        (path->closed && path->count < 3))
    {
        return mesh;
    }

    // Reuse the thin path generator for all arc interpolation.
    R3D_LineStyle thin = {0};
    R3D_MeshData samples = R3D_GenMeshDataLinePath(path, thin);

    if (!R3D_IsMeshDataValid(samples))
    {
        R3D_UnloadMeshData(samples);
        return mesh;
    }

    // Remove consecutive zero-length segments.
    int count = 0;

    for (int i = 0; i < samples.vertexCount; i++)
    {
        R3D_Vertex vertex = samples.vertices[i];

        if (count > 0 &&
            Vector3DistanceSqr(
                vertex.position,
                samples.vertices[count - 1].position
            ) < 0.000000000001f)
        {
            continue;
        }

        samples.vertices[count++] = vertex;
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
        count > (INT_MAX - 128) / 16)
    {
        R3D_UnloadMeshData(samples);
        return mesh;
    }

    int maximumSections = 2 * (count + 1);
    int maximumCapVertices = path->closed ? 0 : 6 * R3D_SCREEN_LINE_CAP_STEPS;
    int maximumCapIndices = maximumCapVertices;

    R3D_MeshData data = R3D_LoadMeshData(
        maximumSections * 2 + maximumCapVertices,
        (maximumSections - 1) * 6 + maximumCapIndices
    );

    if (!data.vertices || !data.indices)
    {
        R3D_UnloadMeshData(data);
        R3D_UnloadMeshData(samples);
        return mesh;
    }

    float distance = 0.0f;
    int last = path->closed ? count : count - 1;

    for (int i = 0; i <= last; i++)
    {
        int current = i % count;
        int previous = (current + count - 1) % count;
        int next = (current + 1) % count;

        Vector3 position = samples.vertices[current].position;
        Color color = samples.vertices[current].color;

        bool start = !path->closed && current == 0;
        bool end = !path->closed && current == count - 1;

        Vector3 before;
        Vector3 after;

        if (start)
        {
            before = after = screen_line_direction(
                position, samples.vertices[next].position
            );
        }
        else if (end)
        {
            before = after = screen_line_direction(
                samples.vertices[previous].position, position
            );
        }
        else
        {
            before = screen_line_direction(
                samples.vertices[previous].position, position
            );

            after = screen_line_direction(
                position, samples.vertices[next].position
            );
        }

        if (start || end || i == 0 || (path->closed && i == count))
        {
            screen_line_add_section(
                &data, position, color, before, after, distance,
                (path->closed && i == count) ? 0 : 1
            );
        }
        else
        {
            // Two sections allow the shader to create a bevel
            // when the projected miter exceeds its limit.
            screen_line_add_section(
                &data, position, color, before, after, distance, 0
            );

            screen_line_add_section(
                &data, position, color, before, after, distance, 1
            );
        }

        if (i < last)
        {
            distance += Vector3Distance(
                position,
                samples.vertices[(i + 1) % count].position
            );
        }
    }

    int sectionCount = data.vertexCount / 2;

    for (int i = 0; i < sectionCount - 1; i++)
    {
        uint32_t a = (uint32_t)(2 * i);
        uint32_t b = a + 2;

        data.indices[data.indexCount++] = a;
        data.indices[data.indexCount++] = b;
        data.indices[data.indexCount++] = b + 1;

        data.indices[data.indexCount++] = a;
        data.indices[data.indexCount++] = b + 1;
        data.indices[data.indexCount++] = a + 1;
    }

    // Connect the incoming and outgoing sections at the
    // closing seam. Without this, bevel joins can split.
    if (path->closed)
    {
        uint32_t a = (uint32_t)((sectionCount - 1) * 2);
        uint32_t b = 0;

        data.indices[data.indexCount++] = a;
        data.indices[data.indexCount++] = b;
        data.indices[data.indexCount++] = b + 1;

        data.indices[data.indexCount++] = a;
        data.indices[data.indexCount++] = b + 1;
        data.indices[data.indexCount++] = a + 1;
    }

    if (!path->closed)
    {
        Vector3 startDirection = screen_line_direction(
            samples.vertices[0].position,
            samples.vertices[1].position
        );

        Vector3 endDirection = screen_line_direction(
            samples.vertices[count - 2].position,
            samples.vertices[count - 1].position
        );

        screen_line_add_cap(
            &data, samples.vertices[0].position,
            samples.vertices[0].color, startDirection, 0.0f, true
        );

        screen_line_add_cap(
            &data, samples.vertices[count - 1].position,
            samples.vertices[count - 1].color, endDirection,
            distance, false
        );
    }

    mesh = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, data, NULL);
    mesh.shadowCastMode = R3D_SHADOW_CAST_DISABLED;

    R3D_UnloadMeshData(data);
    R3D_UnloadMeshData(samples);

    return mesh;
}

R3D_SurfaceEffect* R3D_LoadScreenLineEffect(void)
{
    return R3D_LoadSurfaceEffectFromMemory(R3D_SCREEN_LINE_EFFECT);
}

void R3D_DrawScreenLineMesh(R3D_Mesh mesh, R3D_Material material, R3D_SurfaceEffect* effect, R3D_LineStyle style, Matrix transform)
{
    if (!effect || !R3D_IsMeshValid(mesh) ||
        !isfinite(style.width) || style.width <= 0.0f)
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

    float miterLimit = isfinite(style.miterLimit) && style.miterLimit >= 1.0f
        ? style.miterLimit
        : 4.0f;

    R3D_SetSurfaceEffectUniform(effect, "lineViewport", &viewport);
    R3D_SetSurfaceEffectUniform(effect, "lineWidth", &style.width);
    R3D_SetSurfaceEffectUniform(effect, "lineMiterLimit", &miterLimit);
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
