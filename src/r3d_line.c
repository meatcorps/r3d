
#include <r3d/r3d_line.h>
#include <raymath.h>
#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>

// ========================================
// INTERNAL FUNCTIONS
// ========================================

static int line_segment_steps(const R3D_LineArc* arc)
{
    float sweep = fabsf(arc->sweepAngle);

    if (!isfinite(sweep) || sweep < 0.0001f || sweep >= 2.0f * PI - 0.0001f) {
        return 1;
    }

    return arc->subdivisions > 0 ? arc->subdivisions : 16;
}

static bool line_arc_geometry(Vector3 from, Vector3 to, const R3D_LineArc* arc, Vector3* center, Vector3* axis)
{
    Vector3 chord = Vector3Subtract(to, from);
    float length = Vector3Length(chord);

    if (length < 0.000001f) {
        return false;
    }

    float halfSweep = arc->sweepAngle * 0.5f;
    float sine = sinf(halfSweep);

    if (fabsf(sine) < 0.0001f) {
        return false;
    }

    Vector3 direction = Vector3Scale(chord, 1.0f / length);

    // Ensure the arc plane contains both endpoints.
    Vector3 normal = Vector3Subtract(
        arc->normal,
        Vector3Scale(direction, Vector3DotProduct(arc->normal, direction))
    );

    float normalLength = Vector3Length(normal);

    if (normalLength < 0.000001f) {
        return false;
    }

    *axis = Vector3Scale(normal, 1.0f / normalLength);

    float offset = 0.5f * length * cosf(halfSweep) / sine;

    *center = Vector3Add(
        Vector3Scale(Vector3Add(from, to), 0.5f),
        Vector3Scale(Vector3CrossProduct(*axis, direction), offset)
    );

    return true;
}

static unsigned char line_lerp_channel(unsigned char a, unsigned char b, float t)
{
    return (unsigned char)((float)a + ((float)b - (float)a) * t + 0.5f);
}

static Color line_lerp_color(Color a, Color b, float t)
{
    return (Color) {
        line_lerp_channel(a.r, b.r, t),
        line_lerp_channel(a.g, b.g, t),
        line_lerp_channel(a.b, b.b, t),
        line_lerp_channel(a.a, b.a, t)
    };
}

static R3D_Vertex line_make_vertex(Vector3 position, Color color)
{
    return R3D_MakeVertex(
        position,
        (Vector2){0.0f, 0.0f},
        (Vector3){0.0f, 0.0f, 1.0f},
        (Vector4){1.0f, 0.0f, 0.0f, 1.0f},
        color
    );
}

// ========================================
// PUBLIC API
// ========================================

// ========================================
// WORLD-SPACE RIBBONS
// ========================================

static bool line_valid_position(Vector3 p)
{
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static Vector3 line_ribbon_direction(Vector3 from, Vector3 to)
{
    return Vector3Normalize(Vector3Subtract(to, from));
}

static Vector3 line_ribbon_side(Vector3 direction, Vector3 referenceNormal)
{
    Vector3 side = Vector3CrossProduct(referenceNormal, direction);

    if (Vector3LengthSqr(side) < 0.00000001f)
    {
        Vector3 fallback = fabsf(direction.y) < 0.9f
            ? (Vector3){0.0f, 1.0f, 0.0f}
            : (Vector3){1.0f, 0.0f, 0.0f};

        side = Vector3CrossProduct(fallback, direction);
    }

    return Vector3Normalize(side);
}

static Vector3 line_ribbon_reference_normal(const R3D_LinePath* path, const R3D_MeshData* samples, R3D_LineStyle style)
{
    Vector3 normal = style.normal;

    if (Vector3LengthSqr(normal) > 0.00000001f && line_valid_position(normal))
    {
        return Vector3Normalize(normal);
    }

    // First try the plane supplied by the original arc definitions.
    for (int i = 0; i < path->count; i++)
    {
        normal = path->points[i].arcToNext.normal;

        if (line_valid_position(normal) && Vector3LengthSqr(normal) > 0.00000001f)
        {
            return Vector3Normalize(normal);
        }
    }

    // Otherwise derive a plane from the first non-collinear segments.
    for (int i = 1; i < samples->vertexCount - 1; i++)
    {
        Vector3 a = Vector3Subtract(samples->vertices[i].position, samples->vertices[i - 1].position);
        Vector3 b = Vector3Subtract(samples->vertices[i + 1].position, samples->vertices[i].position);

        normal = Vector3CrossProduct(a, b);

        if (Vector3LengthSqr(normal) > 0.00000001f)
        {
            return Vector3Normalize(normal);
        }
    }

    return (Vector3){0.0f, 0.0f, 1.0f};
}

static void line_ribbon_add_section(R3D_MeshData* mesh, Vector3 center, Vector3 rightOffset, Vector3 leftOffset, Color color, float distance, Vector3 tangent, Vector3 fallbackNormal)
{
    Vector3 normal = Vector3CrossProduct(tangent, Vector3Subtract(leftOffset, rightOffset));

    if (Vector3LengthSqr(normal) < 0.00000001f)
    {
        normal = fallbackNormal;
    }

    normal = Vector3Normalize(normal);

    Vector4 tangent4 = {
        tangent.x,
        tangent.y,
        tangent.z,
        1.0f
    };

    mesh->vertices[mesh->vertexCount++] = R3D_MakeVertex(
        Vector3Add(center, rightOffset),
        (Vector2){distance, 0.0f},
        normal,
        tangent4,
        color
    );

    mesh->vertices[mesh->vertexCount++] = R3D_MakeVertex(
        Vector3Add(center, leftOffset),
        (Vector2){distance, 1.0f},
        normal,
        tangent4,
        color
    );
}


/*
 * Prevent dense curve tessellation from folding into a sharp join.
 *
 * Only points close to a sharp corner are removed. The corner
 * itself and all other sharp corners are always preserved.
 *
 * This is intentionally a lightweight fallback, not a general
 * self-intersection repair algorithm.
 */
static int line_ribbon_trim_sharp_joins(R3D_MeshData* samples, bool closed, float halfWidth)
{
    const int count = samples->vertexCount;

    if (count < 4 || halfWidth <= 0.0f)
    {
        return count;
    }

    // Bit 0: sharp corner
    // Bit 1: removable sample
    unsigned char* marks = calloc((size_t)count, sizeof(*marks));

    if (!marks)
    {
        return count;
    }

    // Identify sharp corners before removing any samples.
    for (int i = 0; i < count; i++)
    {
        if (!closed && (i == 0 || i == count - 1))
        {
            continue;
        }

        int previous = (i + count - 1) % count;
        int next = (i + 1) % count;

        Vector3 before = line_ribbon_direction(
            samples->vertices[previous].position,
            samples->vertices[i].position
        );

        Vector3 after = line_ribbon_direction(
            samples->vertices[i].position,
            samples->vertices[next].position
        );

        float cosine = Vector3DotProduct(before, after);

        // Only treat turns greater than 30 degrees as sharp.
        if (cosine < 0.8660254f)
        {
            marks[i] |= 1;
        }
    }

    for (int i = 0; i < count; i++)
    {
        if (!(marks[i] & 1))
        {
            continue;
        }

        int previous = (i + count - 1) % count;
        int next = (i + 1) % count;

        Vector3 before = line_ribbon_direction(
            samples->vertices[previous].position,
            samples->vertices[i].position
        );

        Vector3 after = line_ribbon_direction(
            samples->vertices[i].position,
            samples->vertices[next].position
        );

        float cosine = Clamp(
            Vector3DotProduct(before, after),
            -0.9999f,
            1.0f
        );

        // Distance occupied by the miter along the centerline.
        float clearance = halfWidth *
            sqrtf((1.0f - cosine) / (1.0f + cosine)) * 1.1f;

        if (!isfinite(clearance))
        {
            continue;
        }

        // Walk backward and forward from this corner.
        for (int direction = -1; direction <= 1; direction += 2)
        {
            int cursor = i;
            float traveled = 0.0f;

            for (int step = 0; step < count - 1; step++)
            {
                int candidate = cursor + direction;

                if (!closed && (candidate <= 0 || candidate >= count - 1))
                {
                    break;
                }

                if (candidate < 0) candidate = count - 1;
                if (candidate >= count) candidate = 0;

                // Never remove another sharp corner.
                if (marks[candidate] & 1)
                {
                    break;
                }

                traveled += Vector3Distance(
                    samples->vertices[cursor].position,
                    samples->vertices[candidate].position
                );

                if (traveled >= clearance)
                {
                    break;
                }

                marks[candidate] |= 2;
                cursor = candidate;
            }
        }
    }

    int remaining = 0;

    for (int i = 0; i < count; i++)
    {
        if (!(marks[i] & 2))
        {
            remaining++;
        }
    }

    // Don't destroy small or excessively thick paths.
    if (remaining >= (closed ? 3 : 2))
    {
        int writeIndex = 0;

        for (int i = 0; i < count; i++)
        {
            if (!(marks[i] & 2))
            {
                samples->vertices[writeIndex++] = samples->vertices[i];
            }
        }

        samples->vertexCount = writeIndex;
    }

    free(marks);

    return samples->vertexCount;
}

/*
 * Map the U coordinate along the actual ribbon boundaries.
 *
 * The outside and inside edges travel different distances through
 * corners. Using independent distances reduces corner stretching.
 *
 * Closed paths normalize both edge lengths to the same total
 * centerline length, preserving the UV seam.
 */
static void line_ribbon_update_uvs(R3D_MeshData* ribbon, bool closed, float centerlineLength)
{
    int sections = ribbon->vertexCount / 2;

    if (sections < 2)
    {
        return;
    }

    float totalRight = 0.0f;
    float totalLeft = 0.0f;

    for (int i = 1; i < sections; i++)
    {
        totalRight += Vector3Distance(
            ribbon->vertices[(i - 1) * 2].position,
            ribbon->vertices[i * 2].position
        );

        totalLeft += Vector3Distance(
            ribbon->vertices[(i - 1) * 2 + 1].position,
            ribbon->vertices[i * 2 + 1].position
        );
    }

    float rightScale = 1.0f;
    float leftScale = 1.0f;

    if (closed)
    {
        if (totalRight > 0.000001f)
        {
            rightScale = centerlineLength / totalRight;
        }

        if (totalLeft > 0.000001f)
        {
            leftScale = centerlineLength / totalLeft;
        }
    }

    float rightDistance = 0.0f;
    float leftDistance = 0.0f;

    for (int i = 0; i < sections; i++)
    {
        if (i > 0)
        {
            rightDistance += Vector3Distance(
                ribbon->vertices[(i - 1) * 2].position,
                ribbon->vertices[i * 2].position
            );

            leftDistance += Vector3Distance(
                ribbon->vertices[(i - 1) * 2 + 1].position,
                ribbon->vertices[i * 2 + 1].position
            );
        }

        R3D_PackTexCoord(
            ribbon->vertices[i * 2].texcoord,
            (Vector2){rightDistance * rightScale, 0.0f}
        );

        R3D_PackTexCoord(
            ribbon->vertices[i * 2 + 1].texcoord,
            (Vector2){leftDistance * leftScale, 1.0f}
        );
    }
}

static R3D_MeshData line_generate_world_ribbon(const R3D_LinePath* path, R3D_LineStyle style)
{
    R3D_MeshData ribbon = {0};

    if (!path || !path->points || path->count < 2 ||
        (path->closed && path->count < 3) ||
        !isfinite(style.width) || style.width <= 0.0f)
    {
        return ribbon;
    }

    // Reuse Phase 1's arc tessellation, including vertex colors.
    R3D_LineStyle thinStyle = {0};
    R3D_MeshData samples = R3D_GenMeshDataLinePath(path, thinStyle);

    if (!R3D_IsMeshDataValid(samples))
    {
        R3D_UnloadMeshData(samples);
        return ribbon;
    }

    // Remove consecutive coincident points.
    // This avoids zero-length segment directions.
    int count = 0;

    for (int i = 0; i < samples.vertexCount; i++)
    {
        R3D_Vertex vertex = samples.vertices[i];

        if (!line_valid_position(vertex.position))
        {
            R3D_UnloadMeshData(samples);
            return ribbon;
        }

        if (count > 0)
        {
            Vector3 difference = Vector3Subtract(vertex.position, samples.vertices[count - 1].position);

            if (Vector3LengthSqr(difference) < 0.000000000001f)
            {
                continue;
            }
        }

        samples.vertices[count++] = vertex;
    }

    // A closed path stores its first point only once.
    if (path->closed && count > 1)
    {
        Vector3 difference = Vector3Subtract(samples.vertices[count - 1].position, samples.vertices[0].position);

        if (Vector3LengthSqr(difference) < 0.000000000001f)
        {
            count--;
        }
    }

    samples.vertexCount = count;

    // Keep the original corners, but avoid tessellation points
    // that would overlap the miter geometry at sharp joins.
    count = line_ribbon_trim_sharp_joins(
        &samples,
        path->closed,
        style.width * 0.5f
    );

    if (count < (path->closed ? 3 : 2) || count > (INT_MAX - 12) / 12)
    {
        R3D_UnloadMeshData(samples);
        return ribbon;
    }

    // Maximum two cross-sections per centerline point.
    // Each section contains two vertices; adjacent sections
    // are connected by six indices.
    int sectionCapacity = 2 * (count + 1);

    ribbon = R3D_LoadMeshData(
        sectionCapacity * 2,
        (sectionCapacity - 1) * 6
    );

    if (!ribbon.vertices || !ribbon.indices)
    {
        R3D_UnloadMeshData(ribbon);
        R3D_UnloadMeshData(samples);
        return (R3D_MeshData){0};
    }

    const float halfWidth = style.width * 0.5f;
    const float miterLimit = isfinite(style.miterLimit) && style.miterLimit >= 1.0f
        ? style.miterLimit
        : 4.0f;

    Vector3 referenceNormal = line_ribbon_reference_normal(path, &samples, style);

    float distance = 0.0f;
    int last = path->closed ? count : count - 1;

    for (int i = 0; i <= last; i++)
    {
        int current = i % count;
        int previous = (current + count - 1) % count;
        int next = (current + 1) % count;

        Vector3 center = samples.vertices[current].position;
        Color color = samples.vertices[current].color;

        bool isStart = !path->closed && current == 0;
        bool isEnd = !path->closed && current == count - 1;

        Vector3 beforeDirection;
        Vector3 afterDirection;

        if (isStart)
        {
            beforeDirection = afterDirection = line_ribbon_direction(center, samples.vertices[next].position);
        }
        else if (isEnd)
        {
            beforeDirection = afterDirection = line_ribbon_direction(samples.vertices[previous].position, center);
        }
        else
        {
            beforeDirection = line_ribbon_direction(samples.vertices[previous].position, center);
            afterDirection = line_ribbon_direction(center, samples.vertices[next].position);
        }

        Vector3 beforeSide = line_ribbon_side(beforeDirection, referenceNormal);
        Vector3 afterSide = line_ribbon_side(afterDirection, referenceNormal);

        Vector3 miter = Vector3Add(beforeSide, afterSide);
        float miterLength = Vector3Length(miter);

        float offsetLength = halfWidth;
        bool useBevel = false;

        if (miterLength > 0.000001f)
        {
            miter = Vector3Scale(miter, 1.0f / miterLength);

            float denominator = Vector3DotProduct(miter, afterSide);

            if (denominator > 0.000001f)
            {
                offsetLength = halfWidth / denominator;
                useBevel = offsetLength > halfWidth * miterLimit;
            }
            else
            {
                useBevel = true;
            }
        }
        else
        {
            miter = beforeSide;
            useBevel = true;
        }

        // Limit the inside intersection at extremely sharp corners.
        offsetLength = fminf(offsetLength, halfWidth * miterLimit);

        Vector3 miterOffset = Vector3Scale(miter, offsetLength);

        Vector3 tangent = Vector3Add(beforeDirection, afterDirection);

        if (Vector3LengthSqr(tangent) < 0.00000001f)
        {
            tangent = afterDirection;
        }

        tangent = Vector3Normalize(tangent);

        if (!useBevel || isStart || isEnd)
        {
            line_ribbon_add_section(
                &ribbon,
                center,
                Vector3Negate(miterOffset),
                miterOffset,
                color,
                distance,
                tangent,
                referenceNormal
            );
        }
        else
        {
            // Determine which side is outside the corner.
            float turn = Vector3DotProduct(
                Vector3CrossProduct(beforeDirection, afterDirection),
                referenceNormal
            );

            bool leftTurn = turn >= 0.0f;

            Vector3 beforeRight;
            Vector3 beforeLeft;
            Vector3 afterRight;
            Vector3 afterLeft;

            if (leftTurn)
            {
                // Outside is the right edge.
                beforeRight = Vector3Scale(beforeSide, -halfWidth);
                afterRight = Vector3Scale(afterSide, -halfWidth);
                beforeLeft = miterOffset;
                afterLeft = miterOffset;
            }
            else
            {
                // Outside is the left edge.
                beforeRight = Vector3Negate(miterOffset);
                afterRight = Vector3Negate(miterOffset);
                beforeLeft = Vector3Scale(beforeSide, halfWidth);
                afterLeft = Vector3Scale(afterSide, halfWidth);
            }

            // At the beginning of a closed path, output only
            // the outgoing section. Its incoming counterpart
            // is generated at the duplicated UV seam.
            if (i != 0)
            {
                line_ribbon_add_section(
                    &ribbon,
                    center,
                    beforeRight,
                    beforeLeft,
                    color,
                    distance,
                    beforeDirection,
                    referenceNormal
                );
            }

            line_ribbon_add_section(
                &ribbon,
                center,
                afterRight,
                afterLeft,
                color,
                distance,
                afterDirection,
                referenceNormal
            );
        }

        if (i < last)
        {
            Vector3 nextPosition = samples.vertices[(i + 1) % count].position;
            distance += Vector3Distance(center, nextPosition);
        }
    }

    // Join consecutive cross-sections with indexed triangles.
    // Closed paths already include their duplicated UV seam.
    int sectionCount = ribbon.vertexCount / 2;
    int index = 0;

    for (int i = 0; i < sectionCount - 1; i++)
    {
        uint32_t a = (uint32_t)(i * 2);
        uint32_t b = a + 2;

        ribbon.indices[index++] = a;
        ribbon.indices[index++] = b;
        ribbon.indices[index++] = b + 1;

        ribbon.indices[index++] = a;
        ribbon.indices[index++] = b + 1;
        ribbon.indices[index++] = a + 1;
    }

    ribbon.indexCount = index;

    // Generate UVs using the lengths of the actual ribbon edges.
    line_ribbon_update_uvs(&ribbon, path->closed, distance);

    R3D_UnloadMeshData(samples);

    return ribbon;
}

R3D_MeshData R3D_GenMeshDataLinePath(const R3D_LinePath* path, R3D_LineStyle style)
{
    if (style.widthMode == R3D_LINE_WIDTH_WORLD)
    {
        return line_generate_world_ribbon(path, style);
    }

    R3D_MeshData data = {0};

    if (!path || !path->points || path->count < 2) {
        return data;
    }

    if (path->closed && path->count < 3) {
        return data;
    }

    int segmentCount = path->closed ? path->count : path->count - 1;
    int vertexCount = 1;
    int maxVertices = INT_MAX / (int)sizeof(R3D_Vertex);

    // Calculate the required vertex count.
    for (int i = 0; i < segmentCount; i++) {
        int steps = line_segment_steps(&path->points[i].arcToNext);

        if (steps > maxVertices - vertexCount) {
            return data;
        }

        vertexCount += steps;
    }

    // GL_LINE_LOOP closes the final segment automatically.
    if (path->closed) {
        vertexCount--;
    }

    data = R3D_LoadMeshData(vertexCount, 0);

    if (!data.vertices) {
        return data;
    }

    int vertexIndex = 0;

    data.vertices[vertexIndex++] = line_make_vertex(
        path->points[0].position,
        path->points[0].color
    );

    for (int i = 0; i < segmentCount; i++) {
        const R3D_LinePoint* from = &path->points[i];
        const R3D_LinePoint* to = &path->points[(i + 1) % path->count];

        int steps = line_segment_steps(&from->arcToNext);

        Vector3 center = {0};
        Vector3 axis = {0};

        bool curved = steps > 1 && line_arc_geometry(
            from->position,
            to->position,
            &from->arcToNext,
            &center,
            &axis
        );

        Vector3 radiusVector = curved
            ? Vector3Subtract(from->position, center)
            : (Vector3){0};

        for (int step = 1; step <= steps; step++) {
            if (path->closed && i == segmentCount - 1 && step == steps) {
                continue;
            }

            float t = (float)step / (float)steps;

            Vector3 position;

            if (curved) {
                position = Vector3Add(
                    center,
                    Vector3RotateByAxisAngle(
                        radiusVector,
                        axis,
                        from->arcToNext.sweepAngle * t
                    )
                );
            }
            else {
                position = Vector3Lerp(from->position, to->position, t);
            }

            // Use the exact endpoint to avoid accumulated numerical error.
            if (step == steps) {
                position = to->position;
            }

            Color color = line_lerp_color(from->color, to->color, t);

            data.vertices[vertexIndex++] = line_make_vertex(position, color);
        }
    }

    data.vertexCount = vertexIndex;

    return data;
}

R3D_Mesh R3D_GenMeshLinePath(const R3D_LinePath* path, R3D_LineStyle style)
{
    R3D_Mesh mesh = {0};

    R3D_MeshData data = R3D_GenMeshDataLinePath(path, style);

    if (!R3D_IsMeshDataValid(data)) {
        R3D_UnloadMeshData(data);
        return mesh;
    }

    R3D_PrimitiveType primitive;

    if (style.widthMode == R3D_LINE_WIDTH_WORLD)
    {
        primitive = R3D_PRIMITIVE_TRIANGLES;
    }
    else
    {
        primitive = path->closed
            ? R3D_PRIMITIVE_LINE_LOOP
            : R3D_PRIMITIVE_LINE_STRIP;
    }

    mesh = R3D_LoadMesh(primitive, data, NULL);
    mesh.shadowCastMode = R3D_SHADOW_CAST_DISABLED;

    R3D_UnloadMeshData(data);

    return mesh;
}
