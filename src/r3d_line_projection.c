
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

    // Validate resolved point widths before allocating projected data.
    for (int i = 0; i < path->count; i++)
    {
        float width = path->points[i].width;

        if (width == 0.0f)
        {
            width = path->style.width;
        }

        if (!isfinite(width) || width <= 0.0f)
        {
            return result;
        }
    }

    R3D_LinePath thinPath = *path;
    thinPath.style.widthMode = R3D_LINE_WIDTH_SCREEN;

    R3D_MeshData samples = R3D_GenMeshDataLinePath(&thinPath);

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
    result.caps = path->style.caps;
    result.sourceSegmentCount = path->closed ? path->count : path->count - 1;

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

        int next = (segment + 1) % path->count;

        float fromWidth = path->points[segment].width;
        float toWidth = path->points[next].width;

        if (fromWidth == 0.0f) fromWidth = path->style.width;
        if (toWidth == 0.0f) toWidth = path->style.width;

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

            output->start.width = Lerp(fromWidth, toWidth, output->start.segmentT);
            output->end.width = Lerp(fromWidth, toWidth, output->end.segmentT);

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


void R3D_Draw2DProjectedLinePath(const R3D_ProjectedLinePath* path, float widthScale)
{
    if (!path || !path->segments || path->count <= 0 ||
        !isfinite(widthScale) || widthScale <= 0.0f)
    {
        return;
    }

    for (int i = 0; i < path->count; i++)
    {
        const R3D_ProjectedLineSegment* segment = &path->segments[i];

        Vector2 start = segment->start.position;
        Vector2 end = segment->end.position;

        if (!isfinite(start.x) || !isfinite(start.y) ||
            !isfinite(end.x) || !isfinite(end.y) ||
            !isfinite(segment->start.width) ||
            !isfinite(segment->end.width))
        {
            continue;
        }

        float length = Vector2Distance(start, end);

        if (!isfinite(length))
        {
            continue;
        }

        // Short pieces approximate both the color gradient
        // and the smoothly changing width using DrawLineEx().
        int steps = (int)fminf(
            256.0f,
            fmaxf(1.0f, ceilf(length / 4.0f))
        );

        for (int j = 0; j < steps; j++)
        {
            float t0 = (float)j / (float)steps;
            float t1 = (float)(j + 1) / (float)steps;
            float middle = (t0 + t1) * 0.5f;

            Vector2 a = Vector2Lerp(start, end, t0);
            Vector2 b = Vector2Lerp(start, end, t1);

            float width = Lerp(
                segment->start.width,
                segment->end.width,
                middle
            ) * widthScale;

            Color color = ColorLerp(
                segment->start.color,
                segment->end.color,
                middle
            );

            if (width > 0.0f)
            {
                DrawLineEx(a, b, width, color);
            }
        }

        // Only fill a join if this section connects to the
        // preceding visible section. Don't fill clipping gaps.
        int previous = i > 0 ? i - 1 : path->count - 1;

        bool connected = (i > 0 || path->closed) &&
            Vector2DistanceSqr(
                path->segments[previous].end.position,
                start
            ) < 0.25f;

        bool originalStart = !path->closed &&
            segment->sourceSegment == 0 &&
            segment->start.segmentT <= 0.00001f;

        bool drawStartCap = originalStart &&
            (path->caps & R3D_LINE_CAP_START);

        if (connected || drawStartCap)
        {
            DrawCircleV(
                start,
                segment->start.width * widthScale * 0.5f,
                segment->start.color
            );
        }
    }

    // The last cap is only drawn if the actual final source
    // endpoint survived near/far clipping.
    if (!path->closed && (path->caps & R3D_LINE_CAP_END))
    {
        const R3D_ProjectedLineSegment* last =
            &path->segments[path->count - 1];

        bool originalEnd =
            last->sourceSegment == path->sourceSegmentCount - 1 &&
            last->end.segmentT >= 0.99999f;

        if (originalEnd &&
            isfinite(last->end.position.x) &&
            isfinite(last->end.position.y))
        {
            DrawCircleV(
                last->end.position,
                last->end.width * widthScale * 0.5f,
                last->end.color
            );
        }
    }
}
