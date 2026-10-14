#include <r3d/r3d.h>
#include <raymath.h>
#include "../src/modules/r3d_render.h"
#include <math.h>
#include <stdlib.h>
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

typedef struct {
    R3D_Vertex base[2];
    R3D_Vertex working[2];

    Vector3 delta0[2];
    Vector3 delta1[2];

    Vector3 minDeltas[2];
    Vector3 maxDeltas[2];

    R3D_ShapeKey keys[2];
    R3D_ShapeKeySet set;
    R3D_ShapeKeyInstance instance;
    R3D_ShapeKeyGpu gpu;

    float weights[2];
} MorphFixture;

static bool near(float a, float b)
{
    return fabsf(a - b) <= 0.0001f;
}

static bool box_equal(BoundingBox a, BoundingBox b)
{
    return near(a.min.x, b.min.x) && near(a.min.y, b.min.y) && near(a.min.z, b.min.z) &&
           near(a.max.x, b.max.x) && near(a.max.y, b.max.y) && near(a.max.z, b.max.z);
}

static bool box_contains(BoundingBox outer, BoundingBox inner)
{
    const float epsilon = 0.0001f;

    return outer.min.x <= inner.min.x + epsilon &&
           outer.min.y <= inner.min.y + epsilon &&
           outer.min.z <= inner.min.z + epsilon &&
           outer.max.x + epsilon >= inner.max.x &&
           outer.max.y + epsilon >= inner.max.y &&
           outer.max.z + epsilon >= inner.max.z;
}

static R3D_Frustum make_frustum(Vector3 center, float extent)
{
    R3D_Frustum result = {0};

    result.planes[R3D_PLANE_LEFT]   = (Vector4){ 1, 0, 0, extent - center.x};
    result.planes[R3D_PLANE_RIGHT]  = (Vector4){-1, 0, 0, extent + center.x};
    result.planes[R3D_PLANE_BOTTOM] = (Vector4){ 0, 1, 0, extent - center.y};
    result.planes[R3D_PLANE_TOP]    = (Vector4){ 0,-1, 0, extent + center.y};
    result.planes[R3D_PLANE_FRONT]  = (Vector4){ 0, 0, 1, extent - center.z};
    result.planes[R3D_PLANE_BACK]   = (Vector4){ 0, 0,-1, extent + center.z};

    return result;
}

static void init_morph_fixture(MorphFixture* f)
{
    memset(f, 0, sizeof(*f));

    f->base[0] = R3D_MakeVertex(
        (Vector3){-1, 0, 0}, (Vector2){0},
        (Vector3){0, 1, 0}, (Vector4){1, 0, 0, 1}, WHITE
    );

    f->base[1] = R3D_MakeVertex(
        (Vector3){1, 0, 0}, (Vector2){0},
        (Vector3){0, 1, 0}, (Vector4){1, 0, 0, 1}, WHITE
    );

    f->delta0[0] = (Vector3){2, 0, 0};
    f->delta0[1] = (Vector3){0, 0, 0};

    f->delta1[0] = (Vector3){0, 0, 0};
    f->delta1[1] = (Vector3){-3, 0, 0};

    f->minDeltas[0] = (Vector3){0, 0, 0};
    f->maxDeltas[0] = (Vector3){2, 0, 0};

    f->minDeltas[1] = (Vector3){-3, 0, 0};
    f->maxDeltas[1] = (Vector3){0, 0, 0};

    f->keys[0].positionDeltas = f->delta0;
    f->keys[1].positionDeltas = f->delta1;

    f->set.baseMesh.vertices = f->base;
    f->set.baseMesh.vertexCount = 2;
    f->set.baseMesh.vertexCapacity = 2;
    f->set.keys = f->keys;
    f->set.keyCount = 2;

    f->instance.source = &f->set;
    f->instance.meshData.vertices = f->working;
    f->instance.meshData.vertexCount = 2;
    f->instance.meshData.vertexCapacity = 2;
    f->instance.weights = f->weights;

    f->gpu.source = &f->set;
    f->gpu.keyCount = 2;
    f->gpu.minDeltas = f->minDeltas;
    f->gpu.maxDeltas = f->maxDeltas;
}

static bool test_validation(void)
{
    BoundingBox valid = {{-1, -2, -3}, {1, 2, 3}};
    BoundingBox reversed = {{2, 0, 0}, {1, 1, 1}};
    BoundingBox nanBounds = valid;
    BoundingBox infinityBounds = valid;

    nanBounds.min.x = NAN;
    infinityBounds.max.z = INFINITY;

    CHECK(R3D_IsBoundingBoxValid(valid));
    CHECK(!R3D_IsBoundingBoxValid(reversed));
    CHECK(!R3D_IsBoundingBoxValid(nanBounds));
    CHECK(!R3D_IsBoundingBoxValid(infinityBounds));

    return true;
}

static bool test_expansion(void)
{
    BoundingBox original = {{-1, -2, -3}, {1, 2, 3}};
    BoundingBox expected = {{-3, -3, -7}, {3, 3, 7}};
    BoundingBox result = {{0}};

    CHECK(R3D_ExpandBoundingBox(original, (Vector3){2, 1, 4}, &result));
    CHECK(box_equal(result, expected));

    // Failure must not overwrite the previous valid output.
    CHECK(!R3D_ExpandBoundingBox(original, (Vector3){-1, 0, 0}, &result));
    CHECK(box_equal(result, expected));

    CHECK(!R3D_ExpandBoundingBox(original, (Vector3){NAN, 0, 0}, &result));
    CHECK(box_equal(result, expected));

    CHECK(!R3D_ExpandBoundingBox(original, (Vector3){INFINITY, 0, 0}, &result));
    CHECK(box_equal(result, expected));

    CHECK(!R3D_ExpandBoundingBox(original, (Vector3){1, 1, 1}, NULL));
    CHECK(box_equal(result, expected));

    return true;
}

static bool test_cpu_morph(void)
{
    MorphFixture f;
    init_morph_fixture(&f);

    f.weights[0] = -1.0f;
    f.weights[1] = 2.0f;

    CHECK(R3D_ApplyShapeKeys(&f.instance));

    // v0: -1 + (-1 * 2) = -3
    // v1:  1 + ( 2 * -3) = -5
    CHECK(near(f.working[0].position.x, -3.0f));
    CHECK(near(f.working[1].position.x, -5.0f));

    BoundingBox expected = {{-5, 0, 0}, {-3, 0, 0}};
    CHECK(box_equal(f.instance.aabb, expected));

    // Re-evaluating must start from the immutable base mesh.
    f.weights[0] = 0.0f;
    f.weights[1] = 0.0f;

    CHECK(R3D_ApplyShapeKeys(&f.instance));

    BoundingBox base = {{-1, 0, 0}, {1, 0, 0}};
    CHECK(box_equal(f.instance.aabb, base));

    return true;
}

static bool test_conservative_morph(void)
{
    MorphFixture f;
    init_morph_fixture(&f);

    const float samples[][2] = {
        { 0.0f,  0.0f},
        { 1.0f,  0.0f},
        { 0.0f,  1.0f},
        {-1.0f,  2.0f},
        { 2.0f, -1.0f},
        { 0.5f, 0.75f},
        {-2.0f, -0.5f}
    };

    BoundingBox baseBounds = {{-1, 0, 0}, {1, 0, 0}};

    for (int i = 0; i < (int)(sizeof(samples) / sizeof(samples[0])); i++)
    {
        f.weights[0] = samples[i][0];
        f.weights[1] = samples[i][1];

        CHECK(R3D_ApplyShapeKeys(&f.instance));

        BoundingBox conservative = {0};

        CHECK(R3D_CalculateConservativeShapeKeyBounds(baseBounds, &f.gpu, &f.instance, &conservative));
        CHECK(R3D_IsBoundingBoxValid(conservative));
        CHECK(box_contains(conservative, f.instance.aabb));
    }

    // Invalid weights cannot corrupt the output.
    BoundingBox sentinel = {{-7, -7, -7}, {7, 7, 7}};
    BoundingBox result = sentinel;

    f.weights[0] = NAN;

    CHECK(!R3D_CalculateConservativeShapeKeyBounds(baseBounds, &f.gpu, &f.instance, &result));
    CHECK(box_equal(result, sentinel));

    return true;
}

static bool test_frustum(void)
{
    R3D_Frustum camera = make_frustum((Vector3){0}, 2.0f);

    BoundingBox outside = {{3, -1, -1}, {4, 1, 1}};
    BoundingBox expanded = {{1.5f, -1, -1}, {4, 1, 1}};

    CHECK(!R3D_FrustumIntersectsBoundingBox(&camera, outside));
    CHECK(R3D_FrustumIntersectsBoundingBox(&camera, expanded));

    return true;
}

static bool test_transformed(void)
{
    R3D_Frustum camera = make_frustum((Vector3){0}, 2.0f);
    BoundingBox local = {{3, -1, -1}, {4, 1, 1}};

    R3D_OrientedBox outside = R3D_GetOrientedBox(local, MatrixIdentity());
    R3D_OrientedBox translated = R3D_GetOrientedBox(local, MatrixTranslate(-3, 0, 0));

    CHECK(!R3D_FrustumIntersectsOrientedBox(&camera, outside));
    CHECK(R3D_FrustumIntersectsOrientedBox(&camera, translated));

    // Non-uniform scale and rotation must remain valid.
    Matrix rotation = MatrixRotateY(0.65f);
    Matrix scale = MatrixScale(1.5f, 0.5f, 2.0f);
    Matrix transform = MatrixMultiply(scale, rotation);

    R3D_OrientedBox rotated = R3D_GetOrientedBox((BoundingBox){{-1, -1, -1}, {1, 1, 1}}, transform);

    CHECK(R3D_FrustumIntersectsOrientedBox(&camera, rotated));

    return true;
}

static bool test_shadow_frustum(void)
{
    R3D_Frustum camera = make_frustum((Vector3){0}, 2.0f);
    R3D_Frustum shadow = make_frustum((Vector3){8, 0, 0}, 2.0f);

    BoundingBox object = {{7, -1, -1}, {9, 1, 1}};

    CHECK(!R3D_FrustumIntersectsBoundingBox(&camera, object));
    CHECK(R3D_FrustumIntersectsBoundingBox(&shadow, object));

    // The camera and shadow must be tested independently.
    return true;
}

static bool test_cluster_envelope(void)
{
    R3D_Frustum camera = make_frustum((Vector3){0}, 2.0f);

    BoundingBox child = {{-1, -1, -1}, {1, 1, 1}};
    BoundingBox incorrectCluster = {{8, -1, -1}, {10, 1, 1}};
    BoundingBox correctedCluster = {{-1, -1, -1}, {10, 1, 1}};

    CHECK(R3D_FrustumIntersectsBoundingBox(&camera, child));
    CHECK(!R3D_FrustumIntersectsBoundingBox(&camera, incorrectCluster));
    CHECK(R3D_FrustumIntersectsBoundingBox(&camera, correctedCluster));
    CHECK(box_contains(correctedCluster, child));

    return true;
}

static bool test_instance_math(void)
{
    R3D_Frustum camera = make_frustum((Vector3){0}, 2.0f);

    // Same source asset, independently configured envelopes.
    BoundingBox sharedMesh = {{3, -1, -1}, {4, 1, 1}};
    BoundingBox normalInstance = sharedMesh;
    BoundingBox extendedInstance = {{1, -1, -1}, {4, 1, 1}};

    CHECK(!R3D_FrustumIntersectsBoundingBox(&camera, normalInstance));
    CHECK(R3D_FrustumIntersectsBoundingBox(&camera, extendedInstance));

    // The shared input remains unchanged.
    CHECK(box_equal(sharedMesh, normalInstance));

    return true;
}


/*
 * Headless render-queue fixture.
 *
 * Fake texture IDs are only used as handles for draw submission.
 * No texture is created, bound or uploaded.
 */
static bool queue_begin(void)
{
    memset(&R3D_MOD_RENDER, 0, sizeof(R3D_MOD_RENDER));
    R3D_MOD_RENDER.activeCluster = -1;

    R3D_MOD_RENDER.clusters = R3D_LIST_CREATE(r3d_render_cluster_t, 8);
    R3D_MOD_RENDER.groupVisibility = R3D_LIST_CREATE(r3d_render_group_visibility_t, 8);
    R3D_MOD_RENDER.callIndices = R3D_LIST_CREATE(r3d_render_indices_t, 8);
    R3D_MOD_RENDER.groups = R3D_LIST_CREATE(r3d_render_group_t, 8);
    R3D_MOD_RENDER.calls = R3D_LIST_CREATE(r3d_render_call_t, 8);
    R3D_MOD_RENDER.groupIndices = R3D_LIST_CREATE(int, 8);
    R3D_MOD_RENDER.sortCache = R3D_LIST_CREATE(r3d_render_sort_t, 8);

    for (int i = 0; i < R3D_RENDER_LIST_COUNT; i++)
    {
        R3D_MOD_RENDER.list[i] = R3D_LIST_CREATE(int, 8);
    }

    // In-memory morph staging only. Never bind these as OpenGL handles.
    R3D_MOD_RENDER.morphMaxEntries = 64;
    R3D_MOD_RENDER.morphActiveBuffer = 1;
    R3D_MOD_RENDER.morphActiveTexture = 2;

    return R3D_MOD_RENDER.clusters &&
           R3D_MOD_RENDER.groupVisibility &&
           R3D_MOD_RENDER.callIndices &&
           R3D_MOD_RENDER.groups &&
           R3D_MOD_RENDER.calls &&
           R3D_MOD_RENDER.groupIndices &&
           R3D_MOD_RENDER.sortCache;
}

static void queue_end(void)
{
    R3D_LIST_DESTROY(R3D_MOD_RENDER.clusters);
    R3D_LIST_DESTROY(R3D_MOD_RENDER.groupVisibility);
    R3D_LIST_DESTROY(R3D_MOD_RENDER.callIndices);
    R3D_LIST_DESTROY(R3D_MOD_RENDER.groups);
    R3D_LIST_DESTROY(R3D_MOD_RENDER.calls);
    R3D_LIST_DESTROY(R3D_MOD_RENDER.groupIndices);
    R3D_LIST_DESTROY(R3D_MOD_RENDER.sortCache);

    for (int i = 0; i < R3D_RENDER_LIST_COUNT; i++)
    {
        R3D_LIST_DESTROY(R3D_MOD_RENDER.list[i]);
    }

    free(R3D_MOD_RENDER.morphEntries);
    memset(&R3D_MOD_RENDER, 0, sizeof(R3D_MOD_RENDER));
}

static bool queue_visible(int index, const R3D_Frustum* frustum)
{
    if (index < 0 || (size_t)index >= R3D_LIST_LENGTH(R3D_MOD_RENDER.calls)) return false;

    r3d_render_cull_groups(frustum);

    const r3d_render_call_t* call = &R3D_LIST_GET(R3D_MOD_RENDER.calls, r3d_render_call_t, index);
    return r3d_render_call_is_visible(call, frustum);
}

static R3D_Model make_test_model(R3D_Mesh* mesh, R3D_Material* material, int* materialIndex, BoundingBox bounds)
{
    *mesh = (R3D_Mesh){
        .vertexCount = 3,
        .layerMask = 1,
        .aabb = bounds
    };

    *material = (R3D_Material){
        .transparencyMode = R3D_TRANSPARENCY_OPAQUE
    };

    *materialIndex = 0;

    return (R3D_Model){
        .meshes = mesh,
        .meshCount = 1,
        .materials = material,
        .materialCount = 1,
        .meshMaterials = materialIndex,
        .aabb = bounds
    };
}

/*
 * Existing model.aabb default and independently configured model copies.
 *
 * A separate animatedAabb default is not yet implemented.
 * This verifies the current model-default draw path.
 */
static bool test_model_defaults(void)
{
    CHECK(queue_begin());

    BoundingBox original = {{4, -0.5f, -0.5f}, {5, 0.5f, 0.5f}};
    BoundingBox expanded = {{-1, -0.5f, -0.5f}, {5, 0.5f, 0.5f}};

    R3D_Mesh mesh;
    R3D_Material material;
    int materialIndex;

    R3D_Model model = make_test_model(&mesh, &material, &materialIndex, original);
    R3D_Model configured = model;

    configured.aabb = expanded;

    R3D_AnimationPlayer player = {.skinTexture = 1};

    R3D_DrawAnimatedModelPro(model, player, MatrixIdentity());
    R3D_DrawAnimatedModelPro(configured, player, MatrixIdentity());

    CHECK(R3D_LIST_LENGTH(R3D_MOD_RENDER.calls) == 2);

    R3D_Frustum camera = make_frustum((Vector3){0}, 1.0f);

    CHECK(!queue_visible(0, &camera));
    CHECK(queue_visible(1, &camera));

    CHECK(box_equal(model.aabb, original));
    CHECK(box_equal(configured.aabb, expanded));

    // Explicit per-draw bounds must not overwrite either model.
    R3D_DrawAnimatedModelProBounds(model, player, expanded, MatrixIdentity());

    CHECK(R3D_LIST_LENGTH(R3D_MOD_RENDER.calls) == 3);
    CHECK(queue_visible(2, &camera));
    CHECK(box_equal(model.aabb, original));

    queue_end();
    return true;
}

static bool test_animated_skeleton(void)
{
    R3D_BoneInfo bones[1] = {{.parent = -1}};
    Matrix localPose[1] = {MatrixTranslate(4, 0, 0)};
    Matrix modelPose[1] = {MatrixIdentity()};

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = 1,
            .rootBind = MatrixIdentity()
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1,
        .skinTexture = 1
    };

    CHECK(R3D_RecomputeAnimationModelPose(&player));

    Vector3 deformed = Vector3Transform((Vector3){0}, modelPose[0]);
    CHECK(near(deformed.x, 4.0f));

    BoundingBox staticBounds = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    BoundingBox animatedBounds = {{-0.5f, -0.5f, -0.5f}, {4.5f, 0.5f, 0.5f}};

    CHECK(!box_contains(staticBounds, (BoundingBox){deformed, deformed}));
    CHECK(box_contains(animatedBounds, (BoundingBox){deformed, deformed}));

    CHECK(queue_begin());

    R3D_Mesh mesh;
    R3D_Material material;
    int index;
    R3D_Model model = make_test_model(&mesh, &material, &index, staticBounds);

    R3D_DrawAnimatedModelPro(model, player, MatrixIdentity());
    R3D_DrawAnimatedModelProBounds(model, player, animatedBounds, MatrixIdentity());

    R3D_Frustum camera = make_frustum((Vector3){4, 0, 0}, 0.5f);

    CHECK(!queue_visible(0, &camera));
    CHECK(queue_visible(1, &camera));

    // The same submitted bounds must also work for another frustum.
    R3D_Frustum shadow = make_frustum((Vector3){4.2f, 0, 0}, 0.5f);

    CHECK(!queue_visible(0, &shadow));
    CHECK(queue_visible(1, &shadow));

    queue_end();
    return true;
}

static bool test_animated_ik(void)
{
    R3D_BoneInfo bones[3] = {
        {.parent = -1},
        {.parent = 0},
        {.parent = 1}
    };

    Matrix localPose[3] = {
        MatrixIdentity(),
        MatrixTranslate(1, 0, 0),
        MatrixTranslate(1, 0, 0)
    };

    Matrix modelPose[3] = {
        MatrixIdentity(),
        MatrixIdentity(),
        MatrixIdentity()
    };

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = 3,
            .rootBind = MatrixIdentity()
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1,
        .skinTexture = 1
    };

    CHECK(R3D_RecomputeAnimationModelPose(&player));

    R3D_TwoBoneIKParams ik = {
        .targetModel = {1, 1, 0},
        .poleModel = {0, 0, 1},
        .weight = 1.0f
    };

    CHECK(R3D_ApplyAnimationTwoBoneIK(&player, 0, 1, 2, ik));
    CHECK(R3D_RecomputeAnimationModelPoseFromBone(&player, 0));

    Vector3 end = {
        modelPose[2].m12,
        modelPose[2].m13,
        modelPose[2].m14
    };

    CHECK(Vector3Distance(end, ik.targetModel) < 0.05f);

    BoundingBox staticBounds = {{-0.1f, -0.1f, -0.1f}, {2.1f, 0.1f, 0.1f}};
    BoundingBox ikBounds = {{-1, -1, -2}, {3, 2, 2}};

    CHECK(!box_contains(staticBounds, (BoundingBox){end, end}));
    CHECK(box_contains(ikBounds, (BoundingBox){end, end}));

    CHECK(queue_begin());

    R3D_Mesh mesh;
    R3D_Material material;
    int index;
    R3D_Model model = make_test_model(&mesh, &material, &index, staticBounds);

    R3D_DrawAnimatedModelPro(model, player, MatrixIdentity());
    R3D_DrawAnimatedModelProBounds(model, player, ikBounds, MatrixIdentity());

    R3D_Frustum camera = make_frustum(ik.targetModel, 0.25f);

    CHECK(!queue_visible(0, &camera));
    CHECK(queue_visible(1, &camera));

    queue_end();
    return true;
}

static bool test_animated_spring(void)
{
    R3D_BoneInfo bones[2] = {
        {.parent = -1},
        {.parent = 0}
    };

    Matrix localPose[2] = {
        MatrixIdentity(),
        MatrixTranslate(0, 1, 0)
    };

    Matrix modelPose[2] = {
        MatrixIdentity(),
        MatrixIdentity()
    };

    R3D_AnimationPlayer player = {
        .skeleton = {
            .bones = bones,
            .boneCount = 2,
            .rootBind = MatrixIdentity()
        },
        .localPose = localPose,
        .modelPose = modelPose,
        .activeAnimIndex = -1,
        .skinTexture = 1
    };

    R3D_SpringBoneState state = {0};

    R3D_SpringBoneParams params = {
        .localAxis = {0, 1, 0},
        .length = 1.0f,
        .frequencyHz = 2.0f,
        .dampingRatio = 0.5f,
        .weight = 1.0f,
        .forces.gravityWorld = {25, 0, 0}
    };

    for (int frame = 0; frame < 90; frame++)
    {
        // Restore the authored pose before applying spring motion.
        localPose[0] = MatrixIdentity();
        localPose[1] = MatrixTranslate(0, 1, 0);

        CHECK(R3D_RecomputeAnimationModelPose(&player));
        CHECK(R3D_ApplyAnimationSpringBoneWorld(&player, 1, &state, params, MatrixIdentity(), 1.0f / 60.0f));
        CHECK(R3D_RecomputeAnimationModelPoseFromBone(&player, 1));
    }

    CHECK(state.initialized);

    Vector3 tip = Vector3Transform((Vector3){0, 1, 0}, modelPose[1]);

    CHECK(fabsf(tip.x) > 0.05f);

    BoundingBox staticBounds = {{-0.02f, 1.8f, -0.02f}, {0.02f, 2.2f, 0.02f}};
    BoundingBox springBounds = {{-2, 0, -2}, {2, 3, 2}};

    CHECK(!box_contains(staticBounds, (BoundingBox){tip, tip}));
    CHECK(box_contains(springBounds, (BoundingBox){tip, tip}));

    CHECK(queue_begin());

    R3D_Mesh mesh;
    R3D_Material material;
    int index;
    R3D_Model model = make_test_model(&mesh, &material, &index, staticBounds);

    R3D_DrawAnimatedModelPro(model, player, MatrixIdentity());
    R3D_DrawAnimatedModelProBounds(model, player, springBounds, MatrixIdentity());

    R3D_Frustum camera = make_frustum(tip, 0.03f);

    CHECK(!queue_visible(0, &camera));
    CHECK(queue_visible(1, &camera));

    queue_end();
    return true;
}

static bool test_morph_skinning(void)
{
    MorphFixture f;
    init_morph_fixture(&f);

    f.weights[0] = -1.0f;
    f.weights[1] = 2.0f;

    CHECK(R3D_ApplyShapeKeys(&f.instance));

    BoundingBox baseBounds = {{-1, 0, 0}, {1, 0, 0}};
    BoundingBox morphedBounds = {0};

    CHECK(R3D_CalculateConservativeShapeKeyBounds(baseBounds, &f.gpu, &f.instance, &morphedBounds));

    // A synthetic one-bone skinning pose that moves the
    // already-morphed vertices seven units along X.
    Matrix skin = MatrixTranslate(7, 0, 0);

    Vector3 first = Vector3Transform(f.working[0].position, skin);
    Vector3 second = Vector3Transform(f.working[1].position, skin);

    BoundingBox actual = {
        .min = Vector3Min(first, second),
        .max = Vector3Max(first, second)
    };

    BoundingBox combined = {
        .min = Vector3Transform(morphedBounds.min, skin),
        .max = Vector3Transform(morphedBounds.max, skin)
    };

    CHECK(!box_contains(morphedBounds, actual));
    CHECK(box_contains(combined, actual));

    // Fake GPU handles: queue submission only, no GL operation.
    f.gpu.buffer = 1;
    f.gpu.texture = 2;
    f.gpu.vertexCount = 2;

    R3D_Mesh mesh = {
        .vertexCount = 2,
        .layerMask = 1,
        .aabb = baseBounds
    };

    R3D_Material material = {
        .transparencyMode = R3D_TRANSPARENCY_OPAQUE
    };

    R3D_AnimationPlayer player = {.skinTexture = 1};

    CHECK(queue_begin());

    CHECK(R3D_DrawAnimatedShapeKeyMeshPro(mesh, material, player, &f.gpu, &f.instance, MatrixIdentity()));
    CHECK(R3D_DrawAnimatedShapeKeyMeshProBounds(mesh, material, player, &f.gpu, &f.instance, combined, MatrixIdentity()));

    CHECK(R3D_LIST_LENGTH(R3D_MOD_RENDER.calls) == 2);

    R3D_Frustum camera = make_frustum((Vector3){3, 0, 0}, 0.25f);

    CHECK(!queue_visible(0, &camera));
    CHECK(queue_visible(1, &camera));

    const r3d_render_call_t* queued = &R3D_LIST_GET(R3D_MOD_RENDER.calls, r3d_render_call_t, 1);

    CHECK(box_equal(queued->mesh.instance.aabb, combined));
    CHECK(queued->mesh.morph.activeCount == 2);

    queue_end();
    return true;
}

static bool test_cluster_submission(void)
{
    CHECK(queue_begin());

    BoundingBox staticBounds = {{4, -0.5f, -0.5f}, {5, 0.5f, 0.5f}};
    BoundingBox animatedBounds = {{-0.5f, -0.5f, -0.5f}, {5, 0.5f, 0.5f}};

    R3D_Mesh mesh;
    R3D_Material material;
    int index;
    R3D_Model model = make_test_model(&mesh, &material, &index, staticBounds);

    R3D_AnimationPlayer player = {.skinTexture = 1};

    // Object bounds are correct, but the cluster is too small.
    R3D_BeginCluster(staticBounds);
    R3D_DrawAnimatedModelProBounds(model, player, animatedBounds, MatrixIdentity());
    R3D_EndCluster();

    // Identical object, now inside a sufficiently large cluster.
    R3D_BeginCluster(animatedBounds);
    R3D_DrawAnimatedModelProBounds(model, player, animatedBounds, MatrixIdentity());
    R3D_EndCluster();

    CHECK(R3D_LIST_LENGTH(R3D_MOD_RENDER.clusters) == 2);
    CHECK(R3D_LIST_LENGTH(R3D_MOD_RENDER.calls) == 2);

    R3D_Frustum camera = make_frustum((Vector3){0}, 1.0f);
    R3D_Frustum shadow = make_frustum((Vector3){0.25f, 0, 0}, 1.0f);

    CHECK(!queue_visible(0, &camera));
    CHECK(queue_visible(1, &camera));

    CHECK(!queue_visible(0, &shadow));
    CHECK(queue_visible(1, &shadow));

    queue_end();
    return true;
}

int main(int argc, char** argv)
{
    const TestCase tests[] = {
        {"validation", test_validation},
        {"expansion", test_expansion},
        {"cpu_morph", test_cpu_morph},
        {"conservative_morph", test_conservative_morph},
        {"frustum", test_frustum},
        {"transformed", test_transformed},
        {"shadow_frustum", test_shadow_frustum},
        {"cluster_envelope", test_cluster_envelope},
        {"instance_math", test_instance_math},

        // Not implemented yet. These are deliberately RED,
        // so we cannot accidentally report the milestone as done.
        {"model_defaults", test_model_defaults},
        {"animated_skeleton", test_animated_skeleton},
        {"animated_ik", test_animated_ik},
        {"animated_spring", test_animated_spring},
        {"morph_skinning", test_morph_skinning},
        {"cluster_submission", test_cluster_submission}
    };

    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <test-name>\n", argv[0]);
        return 2;
    }

    for (int i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++)
    {
        if (strcmp(argv[1], tests[i].name) != 0) continue;

        if (!tests[i].function)
        {
            fprintf(stderr, "PENDING (expected failure): %s is not yet implemented\n", tests[i].name);
            return 1;
        }

        bool passed = tests[i].function();

        printf("%s: %s\n", tests[i].name, passed ? "PASS" : "FAIL");
        return passed ? 0 : 1;
    }

    fprintf(stderr, "Unknown bounds test: %s\n", argv[1]);
    return 2;
}
