
#include <r3d/r3d_line.h>
#include <raymath.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

// ========================================
// INTERNAL FUNCTIONS
// ========================================

static int projected_line_steps(const R3D_LineArc* arc)
{
    float sweep = fabsf(arc->sweepAngle);

    if (!isfinite(sweep) || sweep < 0.0001f ||
        sweep >= 2.0f * PI - 0.0001f)
    {
        return 1;
    }

    return arc->subdivisions > 0 ? arc->subdivisions : 16;
}

static unsigned char projected_lerp_byte(unsigned char a, unsigned char b, float t)
{
    return (unsigned char)((float)a + ((float)b - (float)a) * t + 0.5f);
}

static Color projected_lerp_color(Color a, Color b, float t)
{
    return (Color){
        projected_lerp_byte(a.r, b.r, t),
        projected_lerp_byte(a.g, b.g, t),
        projected_lerp_byte(a.b, b.b, t),
        projected_lerp_byte(a.a, b.a, t)
    };
}

/*
 * Clips the parametric segment against a valid camera
 * depth interval, without changing its original endpoints.
 */
static bool projected_clip_depth(float depthA, float depthB, float nearPlane, float farPlane, float* tStart, float* tEnd)
{
    float difference = depthB - depthA;

    if (fabsf(difference) < 0.0000001f)
    {
        return depthA >= nearPlane && depthA <= farPlane;
    }

    float a = (nearPlane - depthA) / difference;
    float b = (farPlane - depthA) / difference;

    if (a > b)
    {
        float tmp = a;
        a = b;
        b = tmp;
    }

    *tStart = fmaxf(*tStart, a);
    *tEnd = fminf(*tEnd, b);

    return *tEnd > *tStart;
}

// ========================================
// PUBLIC API
// ========================================

R3D_ProjectedLinePath R3D_ProjectLinePath2D(const R3D_LinePath* path, R3D_View view)
{
    R3D_ProjectedLinePath result = {0};

    if (!path || !path->points || path->count < 2 ||
        (path->closed && path->count < 3))
    {
        return result;
    }

    R3D_LineStyle thinStyle = {0};
    R3D_MeshData samples = R3D_GenMeshDataLinePath(path, thinStyle);

    if (!R3D_IsMeshDataValid(samples))
    {
        R3D_UnloadMeshData(samples);
        return result;
    }

    int sampleCount = samples.vertexCount;
    int capacity = sampleCount - 1 + (path->closed ? 1 : 0);

    if (capacity <= 0 ||
        (size_t)capacity > SIZE_MAX / sizeof(*result.segments))
    {
        R3D_UnloadMeshData(samples);
        return result;
    }

    result.segments = malloc((size_t)capacity * sizeof(*result.segments));

    if (!result.segments)
    {
        R3D_UnloadMeshData(samples);
        return result;
    }

    result.closed = path->closed;

    Vector3 cameraPosition = view.camera.position;
    Vector3 forward = R3D_GetCameraForward(view.camera);

    float nearPlane = (float)view.camera.nearPlane + 0.0001f;
    float farPlane = (float)view.camera.farPlane;

    int segmentCount = path->closed ? path->count : path->count - 1;
    int firstSample = 0;

    for (int segment = 0; segment < segmentCount; segment++)
    {
        const R3D_LineArc* arc = &path->points[segment].arcToNext;
        int steps = projected_line_steps(arc);

        for (int step = 0; step < steps; step++)
        {
            int indexA = (firstSample + step) % sampleCount;
            int indexB = (firstSample + step + 1) % sampleCount;

            const R3D_Vertex* vertexA = &samples.vertices[indexA];
            const R3D_Vertex* vertexB = &samples.vertices[indexB];

            Vector3 positionA = vertexA->position;
            Vector3 positionB = vertexB->position;

            float depthA = Vector3DotProduct(
                Vector3Subtract(positionA, cameraPosition), forward
            );

            float depthB = Vector3DotProduct(
                Vector3Subtract(positionB, cameraPosition), forward
            );

            float tStart = 0.0f;
            float tEnd = 1.0f;

            if (!projected_clip_depth(
                depthA, depthB, nearPlane, farPlane,
                &tStart, &tEnd))
            {
                continue;
            }

            if (tStart >= tEnd)
            {
                continue;
            }

            Vector3 clippedA = Vector3Lerp(positionA, positionB, tStart);
            Vector3 clippedB = Vector3Lerp(positionA, positionB, tEnd);

            R3D_ScreenProjection projectionA;
            R3D_ScreenProjection projectionB;

            if (!R3D_WorldToScreen(clippedA, view, &projectionA) ||
                !R3D_WorldToScreen(clippedB, view, &projectionB))
            {
                continue;
            }

            R3D_ProjectedLineSegment* output = &result.segments[result.count++];

            output->start.position = projectionA.position;
            output->end.position = projectionB.position;

            output->start.depth = projectionA.depth;
            output->end.depth = projectionB.depth;

            output->start.color = projected_lerp_color(
                vertexA->color, vertexB->color, tStart
            );

            output->end.color = projected_lerp_color(
                vertexA->color, vertexB->color, tEnd
            );

            output->start.segmentT = ((float)step + tStart) / (float)steps;
            output->end.segmentT = ((float)step + tEnd) / (float)steps;

            output->sourceSegment = segment;
            output->sourceArc = *arc;
        }

        firstSample += steps;
    }

    R3D_UnloadMeshData(samples);

    if (result.count == 0)
    {
        R3D_UnloadProjectedLinePath(result);
        return (R3D_ProjectedLinePath){0};
    }

    return result;
}

void R3D_UnloadProjectedLinePath(R3D_ProjectedLinePath path)
{
    free(path.segments);
}


void R3D_Draw2DProjectedLinePath(const R3D_ProjectedLinePath* path, float width)
{
    if (!path || !path->segments || path->count <= 0 ||
        !isfinite(width) || width <= 0.0f)
    {
        return;
    }

    for (int i = 0; i < path->count; i++)
    {
        const R3D_ProjectedLineSegment* segment = &path->segments[i];

        Vector2 start = segment->start.position;
        Vector2 end = segment->end.position;

        if (!isfinite(start.x) || !isfinite(start.y) ||
            !isfinite(end.x) || !isfinite(end.y))
        {
            continue;
        }

        float length = Vector2Distance(start, end);

        if (!isfinite(length))
        {
            continue;
        }

        // Raylib's DrawLineEx supports only one color per call.
        // Subdivide long segments to preserve interpolated colors.
        int steps = (int)fminf(
            128.0f,
            fmaxf(1.0f, ceilf(length / 6.0f))
        );

        for (int j = 0; j < steps; j++)
        {
            float t0 = (float)j / (float)steps;
            float t1 = (float)(j + 1) / (float)steps;

            Vector2 a = Vector2Lerp(start, end, t0);
            Vector2 b = Vector2Lerp(start, end, t1);

            Color color = ColorLerp(
                segment->start.color,
                segment->end.color,
                (t0 + t1) * 0.5f
            );

            DrawLineEx(a, b, width, color);
        }

        // Rounded joints.
        DrawCircleV(start, width * 0.5f, segment->start.color);
    }

    // Finish the last endpoint on open paths.
    if (!path->closed)
    {
        const R3D_ProjectedLineSegment* last =
            &path->segments[path->count - 1];

        Vector2 end = last->end.position;

        if (isfinite(end.x) && isfinite(end.y))
        {
            DrawCircleV(
                end,
                width * 0.5f,
                last->end.color
            );
        }
    }
}
