
#include <r3d/r3d.h>
#include <raymath.h>

#include "../src/modules/r3d_target.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

typedef bool (*TestFunction)(void);

typedef struct {
    const char* name;
    TestFunction function;
} TestCase;

static bool near(float a, float b, float tolerance)
{
    return isfinite(a) && isfinite(b) && fabsf(a - b) <= tolerance;
}

static bool near_vec2(Vector2 a, Vector2 b, float tolerance)
{
    return near(a.x, b.x, tolerance) &&
           near(a.y, b.y, tolerance);
}

static bool near_vec3(Vector3 a, Vector3 b, float tolerance)
{
    return Vector3Distance(a, b) <= tolerance;
}

static bool near_rect(Rectangle a, Rectangle b, float tolerance)
{
    return near(a.x, b.x, tolerance) &&
           near(a.y, b.y, tolerance) &&
           near(a.width, b.width, tolerance) &&
           near(a.height, b.height, tolerance);
}

static bool rect_contains(Rectangle rect, Vector2 position)
{
    const float epsilon = 0.1f;

    return position.x >= rect.x - epsilon &&
           position.y >= rect.y - epsilon &&
           position.x <= rect.x + rect.width + epsilon &&
           position.y <= rect.y + rect.height + epsilon;
}

static void reset_state(void)
{
    R3D_SetAspectMode(R3D_ASPECT_EXPAND);
    R3D_SetUpscaleMode(R3D_UPSCALE_NEAREST);

    // Set only the dimensions. No GPU resources are created.
    R3D_MOD_TARGET.resW = 640;
    R3D_MOD_TARGET.resH = 360;
}

static R3D_View make_view(int width, int height)
{
    R3D_View view = {0};

    view.camera = R3D_CAMERA_BASE;
    view.camera.nearPlane = 0.1;
    view.camera.farPlane = 100.0;

    // Synthetic RenderTexture metadata, not actual GL handles.
    view.target.id = 1;
    view.target.texture.id = 2;
    view.target.texture.width = width;
    view.target.texture.height = height;

    return view;
}

static bool test_perspective(void)
{
    R3D_View view = make_view(800, 600);

    R3D_ScreenProjection center;
    R3D_ScreenProjection right;
    R3D_ScreenProjection above;

    CHECK(R3D_WorldToScreen((Vector3){0, 0, -5}, view, &center));
    CHECK(R3D_WorldToScreen((Vector3){1, 0, -5}, view, &right));
    CHECK(R3D_WorldToScreen((Vector3){0, 1, -5}, view, &above));

    CHECK(near_vec2(center.position, (Vector2){400, 300}, 0.01f));
    CHECK(center.inFront);
    CHECK(center.insideViewport);
    CHECK(center.depth > 0.0f && center.depth < 1.0f);

    CHECK(right.position.x > center.position.x);
    CHECK(near(right.position.y, 300.0f, 0.01f));

    // Positive world Y must move upward in target coordinates.
    CHECK(above.position.y < center.position.y);
    CHECK(near(above.position.x, 400.0f, 0.01f));

    return true;
}

static bool test_orthographic(void)
{
    R3D_View view = make_view(800, 600);

    view.camera.projection = R3D_PROJECTION_ORTHOGRAPHIC;
    view.camera.fovy = 4.0;

    R3D_ScreenProjection projected;

    CHECK(R3D_WorldToScreen((Vector3){2, 1, -5}, view, &projected));

    CHECK(near_vec2(projected.position, (Vector2){700, 150}, 0.01f));
    CHECK(projected.inFront);
    CHECK(projected.insideViewport);

    Ray centerRay;
    Ray offsetRay;

    CHECK(R3D_ScreenToWorldRay((Vector2){400, 300}, view, &centerRay));
    CHECK(R3D_ScreenToWorldRay((Vector2){700, 150}, view, &offsetRay));

    CHECK(near_vec3(centerRay.direction, (Vector3){0, 0, -1}, 0.001f));
    CHECK(near_vec3(offsetRay.direction, centerRay.direction, 0.001f));

    CHECK(near_vec3(centerRay.position, (Vector3){0, 0, -0.1f}, 0.001f));
    CHECK(near_vec3(offsetRay.position, (Vector3){2, 1, -0.1f}, 0.001f));

    return true;
}

static bool test_roundtrip(void)
{
    R3D_View view = make_view(1280, 720);

    view.camera.position = (Vector3){2, 3, 4};
    R3D_CameraLookAt(&view.camera, (Vector3){0, 0, 0}, (Vector3){0, 1, 0});

    Vector3 forward = R3D_GetCameraForward(view.camera);
    Vector3 right = R3D_GetCameraRight(view.camera);
    Vector3 up = R3D_GetCameraUp(view.camera);

    Vector3 world = view.camera.position;
    world = Vector3Add(world, Vector3Scale(forward, 6.0f));
    world = Vector3Add(world, Vector3Scale(right, 0.8f));
    world = Vector3Add(world, Vector3Scale(up, 0.4f));

    R3D_ScreenProjection projected;
    Ray ray;

    CHECK(R3D_WorldToScreen(world, view, &projected));
    CHECK(projected.inFront && projected.insideViewport);
    CHECK(R3D_ScreenToWorldRay(projected.position, view, &ray));

    Vector3 toPoint = Vector3Subtract(world, ray.position);
    float distance = Vector3DotProduct(toPoint, ray.direction);
    Vector3 closest = Vector3Add(ray.position, Vector3Scale(ray.direction, distance));

    CHECK(distance > 0.0f);
    CHECK(Vector3Distance(world, closest) < 0.005f);

    return true;
}

static bool test_target_sizes(void)
{
    const int sizes[][2] = {
        {320, 180},
        {640, 360},
        {800, 600},
        {1280, 720},
        {1920, 1080}
    };

    for (int i = 0; i < 5; i++)
    {
        const int width = sizes[i][0];
        const int height = sizes[i][1];

        R3D_View view = make_view(width, height);

        R3D_ScreenProjection center;
        Ray ray;

        CHECK(R3D_WorldToScreen((Vector3){0, 0, -5}, view, &center));

        CHECK(near(center.position.x, width * 0.5f, 0.01f));
        CHECK(near(center.position.y, height * 0.5f, 0.01f));

        CHECK(R3D_ScreenToWorldRay(center.position, view, &ray));
        CHECK(near_vec3(ray.position, (Vector3){0, 0, 0}, 0.001f));
        CHECK(near_vec3(ray.direction, (Vector3){0, 0, -1}, 0.001f));
    }

    return true;
}

static bool test_viewport(void)
{
    R3D_View view = make_view(1024, 768);
    view.viewport = (Rectangle){80, 60, 400, 200};

    R3D_ScreenProjection center;

    CHECK(R3D_WorldToScreen((Vector3){0, 0, -5}, view, &center));
    CHECK(near_vec2(center.position, (Vector2){280, 160}, 0.01f));
    CHECK(center.insideViewport);

    Ray ray;

    CHECK(R3D_ScreenToWorldRay((Vector2){280, 160}, view, &ray));
    CHECK(near_vec3(ray.direction, (Vector3){0, 0, -1}, 0.001f));

    CHECK(!R3D_ScreenToWorldRay((Vector2){79, 160}, view, &ray));
    CHECK(!R3D_ScreenToWorldRay((Vector2){480, 160}, view, &ray));
    CHECK(!R3D_ScreenToWorldRay((Vector2){280, 59}, view, &ray));
    CHECK(!R3D_ScreenToWorldRay((Vector2){280, 260}, view, &ray));

    R3D_ScreenProjection outside;
    CHECK(R3D_WorldToScreen((Vector3){10, 0, -5}, view, &outside));
    CHECK(outside.inFront);
    CHECK(!outside.insideViewport);

    return true;
}

static bool test_keep_letterbox(void)
{
    R3D_SetAspectMode(R3D_ASPECT_KEEP);

    // 16:9 scene presented inside an 800x600 (4:3) target.
    R3D_MOD_TARGET.resW = 640;
    R3D_MOD_TARGET.resH = 360;

    R3D_View view = make_view(800, 600);
    R3D_ScreenProjection center;
    Ray ray;

    CHECK(R3D_WorldToScreen((Vector3){0, 0, -5}, view, &center));
    CHECK(near_vec2(center.position, (Vector2){400, 300}, 0.01f));

    // 800x450 content, with 75 pixels above and below.
    CHECK(!R3D_ScreenToWorldRay((Vector2){400, 20}, view, &ray));
    CHECK(!R3D_ScreenToWorldRay((Vector2){400, 74}, view, &ray));

    CHECK(R3D_ScreenToWorldRay((Vector2){400, 75}, view, &ray));
    CHECK(R3D_ScreenToWorldRay((Vector2){400, 524}, view, &ray));

    CHECK(!R3D_ScreenToWorldRay((Vector2){400, 525}, view, &ray));
    CHECK(!R3D_ScreenToWorldRay((Vector2){400, 580}, view, &ray));

    return true;
}

static bool test_keep_pillarbox(void)
{
    R3D_SetAspectMode(R3D_ASPECT_KEEP);

    R3D_MOD_TARGET.resW = 600;
    R3D_MOD_TARGET.resH = 600;

    R3D_View view = make_view(800, 600);

    R3D_ScreenProjection center;
    Ray ray;

    CHECK(R3D_WorldToScreen((Vector3){0, 0, -5}, view, &center));
    CHECK(near_vec2(center.position, (Vector2){400, 300}, 0.01f));

    // 600x600 content with 100 pixels on either side.
    CHECK(!R3D_ScreenToWorldRay((Vector2){99, 300}, view, &ray));
    CHECK(R3D_ScreenToWorldRay((Vector2){100, 300}, view, &ray));
    CHECK(R3D_ScreenToWorldRay((Vector2){699, 300}, view, &ray));
    CHECK(!R3D_ScreenToWorldRay((Vector2){700, 300}, view, &ray));

    return true;
}

static bool test_resolution_independence(void)
{
    R3D_SetAspectMode(R3D_ASPECT_KEEP);

    R3D_View view = make_view(800, 600);
    Vector3 world = {0.8f, 0.5f, -5.0f};

    const int resolutions[][2] = {
        {320, 180},
        {640, 360},
        {1280, 720},
        {1920, 1080}
    };

    Vector2 firstPosition = {0};

    for (int i = 0; i < 4; i++)
    {
        R3D_MOD_TARGET.resW = resolutions[i][0];
        R3D_MOD_TARGET.resH = resolutions[i][1];

        R3D_ScreenProjection projected;
        Ray ray;

        CHECK(R3D_WorldToScreen(world, view, &projected));
        CHECK(R3D_ScreenToWorldRay(projected.position, view, &ray));

        if (i == 0)
        {
            firstPosition = projected.position;
        }
        else
        {
            // Internal render resolution must not move UI markers.
            CHECK(near_vec2(projected.position, firstPosition, 0.01f));
        }

        Vector3 toPoint = Vector3Subtract(world, ray.position);
        Vector3 closest = Vector3Add(
            ray.position,
            Vector3Scale(ray.direction, Vector3DotProduct(toPoint, ray.direction))
        );

        CHECK(Vector3Distance(closest, world) < 0.005f);
    }

    return true;
}

static bool test_output_scaling(void)
{
    R3D_View small = make_view(640, 360);
    R3D_View large = make_view(1920, 1080);

    Vector3 world = {0.75f, 0.5f, -5};

    R3D_ScreenProjection pSmall;
    R3D_ScreenProjection pLarge;

    CHECK(R3D_WorldToScreen(world, small, &pSmall));
    CHECK(R3D_WorldToScreen(world, large, &pLarge));

    // Same aspect, target size increased by exactly 3.
    CHECK(near(pLarge.position.x, pSmall.position.x * 3.0f, 0.1f));
    CHECK(near(pLarge.position.y, pSmall.position.y * 3.0f, 0.1f));

    Ray raySmall;
    Ray rayLarge;

    CHECK(R3D_ScreenToWorldRay(pSmall.position, small, &raySmall));
    CHECK(R3D_ScreenToWorldRay(pLarge.position, large, &rayLarge));

    CHECK(near_vec3(raySmall.direction, rayLarge.direction, 0.001f));

    // Upscale filtering should not affect projection coordinates.
    R3D_SetUpscaleMode(R3D_UPSCALE_LINEAR);

    R3D_ScreenProjection again;
    CHECK(R3D_WorldToScreen(world, large, &again));
    CHECK(near_vec2(again.position, pLarge.position, 0.01f));

    return true;
}

static bool test_clipping(void)
{
    R3D_View view = make_view(800, 600);

    R3D_ScreenProjection behind;
    R3D_ScreenProjection beforeNear;
    R3D_ScreenProjection beyondFar;

    CHECK(R3D_WorldToScreen((Vector3){0, 0, 5}, view, &behind));
    CHECK(!behind.inFront);

    CHECK(R3D_WorldToScreen((Vector3){0, 0, -0.05f}, view, &beforeNear));
    CHECK(beforeNear.inFront);
    CHECK(beforeNear.depth < 0.0f);

    CHECK(R3D_WorldToScreen((Vector3){0, 0, -150}, view, &beyondFar));
    CHECK(beyondFar.inFront);
    CHECK(beyondFar.depth > 1.0f);

    return true;
}

static bool test_screen_rect(void)
{
    R3D_View view = make_view(800, 600);

    BoundingBox bounds = {
        .min = {-1, -1, -6},
        .max = {1, 1, -4}
    };

    Rectangle boxRect;
    Rectangle meshRect;
    Rectangle modelRect;

    R3D_Mesh mesh = {.aabb = bounds};
    R3D_Model model = {.aabb = bounds};

    CHECK(R3D_BoundingBoxToScreenRect(bounds, MatrixIdentity(), view, &boxRect));
    CHECK(R3D_MeshToScreenRect(mesh, MatrixIdentity(), view, &meshRect));
    CHECK(R3D_ModelToScreenRect(model, MatrixIdentity(), view, &modelRect));

    CHECK(near_rect(boxRect, meshRect, 0.01f));
    CHECK(near_rect(boxRect, modelRect, 0.01f));

    CHECK(boxRect.width > 0.0f);
    CHECK(boxRect.height > 0.0f);

    // Symmetric box, therefore the bottom-center anchor is X=400.
    CHECK(near(boxRect.x + boxRect.width * 0.5f, 400.0f, 0.01f));
    CHECK(boxRect.y + boxRect.height > 300.0f);

    // A configured animated model envelope must take precedence.
    model.hasAnimatedAabb = true;
    model.animatedAabb = (BoundingBox){
        .min = {-2, -1, -6},
        .max = {2, 1, -4}
    };

    Rectangle animatedRect;
    Rectangle explicitRect;

    CHECK(R3D_ModelToScreenRect(model, MatrixIdentity(), view, &animatedRect));
    CHECK(R3D_BoundingBoxToScreenRect(model.animatedAabb, MatrixIdentity(), view, &explicitRect));

    CHECK(near_rect(animatedRect, explicitRect, 0.01f));
    CHECK(animatedRect.width > modelRect.width);
    CHECK(near(animatedRect.x + animatedRect.width * 0.5f, 400.0f, 0.01f));

    return true;
}

static bool test_transformed_rect(void)
{
    R3D_View view = make_view(800, 600);

    BoundingBox local = {
        .min = {-1, -1, -1},
        .max = {1, 1, 1}
    };

    Matrix rotation = MatrixRotateY(0.7f);
    Matrix translation = MatrixTranslate(1, 0, -5);
    Matrix transform = MatrixMultiply(rotation, translation);

    Rectangle rect;

    CHECK(R3D_BoundingBoxToScreenRect(local, transform, view, &rect));
    CHECK(rect.width > 0.0f && rect.height > 0.0f);

    // Every projected corner must lie inside the calculated rectangle.
    for (int i = 0; i < 8; i++)
    {
        Vector3 corner = {
            (i & 1) ? local.max.x : local.min.x,
            (i & 2) ? local.max.y : local.min.y,
            (i & 4) ? local.max.z : local.min.z
        };

        Vector3 world = Vector3Transform(corner, transform);
        R3D_ScreenProjection projected;

        CHECK(R3D_WorldToScreen(world, view, &projected));
        CHECK(projected.inFront);
        CHECK(rect_contains(rect, projected.position));
    }

    return true;
}

static bool test_near_plane_rect(void)
{
    R3D_View view = make_view(800, 600);

    BoundingBox crossingNear = {
        .min = {-0.1f, -0.1f, -1.0f},
        .max = {0.1f, 0.1f, 0.2f}
    };

    Rectangle rect;

    CHECK(R3D_BoundingBoxToScreenRect(crossingNear, MatrixIdentity(), view, &rect));

    CHECK(isfinite(rect.x) && isfinite(rect.y));
    CHECK(isfinite(rect.width) && isfinite(rect.height));
    CHECK(rect.width > 0.0f && rect.height > 0.0f);
    CHECK(near(rect.x + rect.width * 0.5f, 400.0f, 0.2f));

    BoundingBox completelyBehind = {
        .min = {-1, -1, 1},
        .max = {1, 1, 2}
    };

    Rectangle sentinel = {9, 8, 7, 6};
    Rectangle unchanged = sentinel;

    CHECK(!R3D_BoundingBoxToScreenRect(completelyBehind, MatrixIdentity(), view, &unchanged));
    CHECK(near_rect(unchanged, sentinel, 0.001f));

    return true;
}

static bool test_picking(void)
{
    R3D_View view = make_view(800, 600);

    BoundingBox box = {
        .min = {-1, -1, -6},
        .max = {1, 1, -4}
    };

    RayCollision hit = {0};

    CHECK(R3D_PickBoundingBox((Vector2){400, 300}, view, box, &hit));

    CHECK(hit.hit);
    CHECK(near(hit.distance, 4.0f, 0.01f));
    CHECK(near_vec3(hit.point, (Vector3){0, 0, -4}, 0.01f));

    // Front face normal points toward the camera.
    CHECK(near_vec3(hit.normal, (Vector3){0, 0, 1}, 0.01f));

    CHECK(!R3D_PickBoundingBox((Vector2){790, 300}, view, box, &hit));
    CHECK(!hit.hit);

    return true;
}

static bool test_inside_box(void)
{
    R3D_View view = make_view(800, 600);

    // Perspective ray originates at (0,0,0), inside this box.
    BoundingBox box = {
        .min = {-1, -1, -1},
        .max = {1, 1, 1}
    };

    RayCollision hit = {0};

    CHECK(R3D_PickBoundingBox((Vector2){400, 300}, view, box, &hit));
    CHECK(hit.hit);
    CHECK(near(hit.distance, 0.0f, 0.001f));
    CHECK(near_vec3(hit.point, (Vector3){0, 0, 0}, 0.001f));

    return true;
}

static bool test_multiple_cameras(void)
{
    R3D_View first = make_view(800, 600);
    R3D_View second = make_view(800, 600);

    second.camera.position = (Vector3){5, 0, 0};

    R3D_CameraLookAt(
        &second.camera,
        (Vector3){0, 0, 0},
        (Vector3){0, 1, 0}
    );

    R3D_ScreenProjection firstCenter;
    R3D_ScreenProjection secondCenter;

    CHECK(R3D_WorldToScreen((Vector3){0, 0, -5}, first, &firstCenter));
    CHECK(R3D_WorldToScreen((Vector3){0, 0, 0}, second, &secondCenter));

    CHECK(near_vec2(firstCenter.position, (Vector2){400, 300}, 0.01f));
    CHECK(near_vec2(secondCenter.position, (Vector2){400, 300}, 0.01f));

    Ray firstRay;
    Ray secondRay;

    CHECK(R3D_ScreenToWorldRay((Vector2){400, 300}, first, &firstRay));
    CHECK(R3D_ScreenToWorldRay((Vector2){400, 300}, second, &secondRay));

    CHECK(near_vec3(firstRay.direction, (Vector3){0, 0, -1}, 0.001f));
    CHECK(near_vec3(secondRay.direction, (Vector3){-1, 0, 0}, 0.001f));

    // Explicit view information is independent of the last rendered camera.
    return true;
}

static bool test_invalid_input(void)
{
    R3D_View view = make_view(800, 600);

    R3D_ScreenProjection projected;
    Ray ray;
    Rectangle rect;
    RayCollision hit;

    BoundingBox valid = {
        .min = {-1, -1, -6},
        .max = {1, 1, -4}
    };

    BoundingBox reversed = {
        .min = {1, 1, 1},
        .max = {-1, -1, -1}
    };

    CHECK(!R3D_WorldToScreen((Vector3){0, 0, -5}, view, NULL));
    CHECK(!R3D_ScreenToWorldRay((Vector2){400, 300}, view, NULL));
    CHECK(!R3D_BoundingBoxToScreenRect(valid, MatrixIdentity(), view, NULL));
    CHECK(!R3D_PickBoundingBox((Vector2){400, 300}, view, valid, NULL));

    CHECK(!R3D_WorldToScreen((Vector3){NAN, 0, -5}, view, &projected));
    CHECK(!R3D_ScreenToWorldRay((Vector2){NAN, 300}, view, &ray));

    CHECK(!R3D_BoundingBoxToScreenRect(reversed, MatrixIdentity(), view, &rect));
    CHECK(!R3D_PickBoundingBox((Vector2){400, 300}, view, reversed, &hit));
    CHECK(!hit.hit);

    // Invalid camera projection parameters must be rejected.
    view.camera.fovy = 0.0;

    CHECK(!R3D_WorldToScreen((Vector3){0, 0, -5}, view, &projected));
    CHECK(!R3D_ScreenToWorldRay((Vector2){400, 300}, view, &ray));

    return true;
}

int main(int argc, char** argv)
{
    const TestCase tests[] = {
        {"perspective", test_perspective},
        {"orthographic", test_orthographic},
        {"roundtrip", test_roundtrip},
        {"target_sizes", test_target_sizes},
        {"viewport", test_viewport},
        {"keep_letterbox", test_keep_letterbox},
        {"keep_pillarbox", test_keep_pillarbox},
        {"resolution_independence", test_resolution_independence},
        {"output_scaling", test_output_scaling},
        {"clipping", test_clipping},
        {"screen_rect", test_screen_rect},
        {"transformed_rect", test_transformed_rect},
        {"near_plane_rect", test_near_plane_rect},
        {"picking", test_picking},
        {"inside_box", test_inside_box},
        {"multiple_cameras", test_multiple_cameras},
        {"invalid_input", test_invalid_input}
    };

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <test-name>\n", argv[0]);
        return 2;
    }

    reset_state();

    for (int i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++)
    {
        if (strcmp(argv[1], tests[i].name) != 0) continue;

        bool passed = tests[i].function();

        printf("%s: %s\n", tests[i].name, passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }

    fprintf(stderr, "Unknown projection test: %s\n", argv[1]);
    return 2;
}
