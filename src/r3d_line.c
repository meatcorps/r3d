
#include <r3d/r3d_line.h>
#include <raymath.h>
#include <math.h>
#include <limits.h>
#include <stddef.h>

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

R3D_MeshData R3D_GenMeshDataLinePath(const R3D_LinePath* path, R3D_LineStyle style)
{
    R3D_MeshData data = {0};

    // Width is reserved for the ribbon implementation.
    (void)style;

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

    R3D_PrimitiveType primitive = path->closed
        ? R3D_PRIMITIVE_LINE_LOOP
        : R3D_PRIMITIVE_LINE_STRIP;

    mesh = R3D_LoadMesh(primitive, data, NULL);
    mesh.shadowCastMode = R3D_SHADOW_CAST_DISABLED;

    R3D_UnloadMeshData(data);

    return mesh;
}
