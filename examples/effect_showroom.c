/*
 * Meatcorps R3D - EFFECTS ATLAS
 * Standalone cinematic visual validation browser for built render modes.
 *
 * Assets are bundled with the R3D example resources:
 *   models/DamagedHelmet.glb
 *   models/CesiumMan.glb (animation)
 *   images/decal.png
 *   panorama/indoor.hdr
 *
 *
 * Keyboard: LEFT/RIGHT effect | TAB next category | SHIFT+TAB previous
 *           SPACE pause | A parameter sweep | B depth blocker
 *           R reset phase | ESC exit
 * Mouse:   click arrows or category tabs.
 */

#include <r3d/r3d.h>
#include <raymath.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "r3d/r3d_texture.h"

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

#define SCREEN_W 1600
#define SCREEN_H 900
#define STAGE_X 3.65f
#define STAGE_TOP -1.06f
#define TWO_PI 6.28318530718f

typedef enum {
    GROUP_PIPELINE,
    GROUP_COLOR,
    GROUP_SILHOUETTE,
    GROUP_ANIMATED,
    GROUP_PROCEDURAL,
    GROUP_COMBOS,
    GROUP_COUNT
} Group;

typedef enum {
    P_DEFAULT,
    P_UNLIT,
    P_ALPHA,
    P_ADDITIVE,
    P_MULTIPLY,
    P_SCREEN,
    P_DOUBLE_SIDED,
    P_XRAY,
    P_OCCLUDED,
    P_WIREFRAME,
    P_WIREFRAME_OVERLAY,
    P_SILHOUETTE,
    P_SHADOW_ONLY,
    P_OVERLAY,
    P_DEPTH_ONLY,
    P_DITHERED,
    P_STENCIL_TEST,
    P_OUTLINE,

    P_COLOR_REPLACE,
    P_COLOR_MULTIPLY,
    P_COLOR_MIX,
    P_COLOR_ADD,
    P_COLOR_SCREEN,
    P_EMISSION_COLOR,
    P_ALBEDO_TEXTURE,
    P_EMISSION_TEXTURE,

    P_RIM_GLOW,
    P_FRESNEL,
    P_OUTER_GLOW,
    P_DROP_SHADOW,

    P_HOLOGRAM,
    P_DISSOLVE,
    P_EMISSION_PULSE,
    P_GLITCH,

    P_UV_SCROLL,
    P_UV_DISTORTION,
    P_GRADIENT,
    P_PIXELATE,
    P_DITHER_FADE,
    P_SLICE_DISPLACE,
    P_CUT_CLIP,

    P_HOLO_FRESNEL,
    P_DISSOLVE_RIM,
    P_GLOW_SHADOW,
    P_CUT_PULSE,
    P_GLITCH_DITHER,
    P_GRADIENT_SCROLL,

    PRESET_COUNT
} PresetId;

typedef struct {
    const char* title;
    const char* description;
    const char* detail;
    Group group;
    R3D_BuiltRenderMode built;
} Preset;

typedef struct {
    Texture2D decal;
    R3D_Mesh cube;
    R3D_Mesh floorMesh;
    R3D_Mesh pedestal;
    R3D_Mesh rimBar;
    R3D_Mesh backdrop;
    R3D_Mesh blocker;
    R3D_Mesh stencilSphere;
    R3D_Model helmet;
    R3D_Model character;
    R3D_AnimationLib animations;
    R3D_AnimationPlayer player;
    R3D_Cubemap hdri;
    R3D_AmbientMap ambient;
    R3D_ShadowMap shadow;
    R3D_Light key;
    R3D_Light fill;
    R3D_Light rim;
    R3D_Material cubeMat;
    R3D_Material floorMat;
    R3D_Material pedestalMat;
    R3D_Material wallMat;
    R3D_Material lightMat;
    R3D_Material blockerMat;
    R3D_BuiltRenderMode stencilWriter;
    Preset presets[PRESET_COUNT];
} Showroom;

static const char* GROUP_NAMES[GROUP_COUNT] = {
    "PIPELINE", "COLOR + TEXTURE", "SILHOUETTE", "ANIMATED", "PROCEDURAL", "COMBINATIONS"
};

static const Color GROUP_COLORS[GROUP_COUNT] = {
    {71, 190, 248, 255},
    {249, 190, 91, 255},
    {165, 119, 255, 255},
    {249, 100, 156, 255},
    {98, 230, 190, 255},
    {255, 153, 105, 255}
};

static float clamp01(float n)
{
    return fmaxf(0.0f, fminf(1.0f, n));
}

static Rectangle box_rect(float x, float y, float w, float h)
{
    return (Rectangle){x, y, w, h};
}

static Matrix model_in_podium(R3D_Model model, float x, float targetHeight, float angle)
{
    Vector3 size = Vector3Subtract(model.aabb.max, model.aabb.min);
    Vector3 center = Vector3Scale(Vector3Add(model.aabb.min, model.aabb.max), 0.5f);
    float biggest = fmaxf(size.x, fmaxf(size.y, size.z));
    float scale = biggest > 0.001f ? targetHeight / biggest : 1.0f;

    Matrix local = MatrixMultiply(
        MatrixTranslate(-center.x, -center.y, -center.z),
        MatrixScale(scale, scale, scale)
    );
    local = MatrixMultiply(local, MatrixRotateY(angle));
    return MatrixMultiply(local, MatrixTranslate(x, STAGE_TOP + targetHeight * 0.5f + 0.08f, 0.0f));
}

static void set_preset(Preset* preset, Group group, const char* title, const char* description, const char* detail, R3D_BuiltRenderMode built)
{
    preset->group = group;
    preset->title = title;
    preset->description = description;
    preset->detail = detail;
    preset->built = built;
}

static void make_presets(Showroom* s)
{
    Preset* p = s->presets;
    Color cyan = {40, 220, 255, 255};
    Color coral = {255, 98, 80, 255};
    Color violet = {189, 95, 255, 255};
    Color gold = {255, 179, 55, 255};

    #define ENTRY(id,group,name,desc,detail,expr) \
        set_preset(&p[id], group, name, desc, detail, expr)

    ENTRY(P_DEFAULT, GROUP_PIPELINE, "BASE MATERIAL", "Unmodified PBR material, direct lighting and shadows.", "REFERENCE", R3D_RenderModeBuildDefault());
    ENTRY(P_UNLIT, GROUP_PIPELINE, "UNLIT", "Inspect material textures without scene lighting.", "LIGHTING OFF", R3D_RenderModeBuildUnlit());
    ENTRY(P_ALPHA, GROUP_PIPELINE, "ALPHA BLEND", "Transparent forward rendering over the test stage.", "FORWARD", R3D_RenderModeBuildAlphaBlend());
    R3D_RenderModeSetOpacity(&p[P_ALPHA].built, 0.58f);
    ENTRY(P_ADDITIVE, GROUP_PIPELINE, "ADDITIVE", "Add the object's color to what is behind it.", "BLEND", R3D_RenderModeBuildAdditive());
    ENTRY(P_MULTIPLY, GROUP_PIPELINE, "MULTIPLY", "Darken the background with the object color.", "BLEND", R3D_RenderModeBuildMultiply());
    ENTRY(P_SCREEN, GROUP_PIPELINE, "SCREEN", "Brighten the background using screen blending.", "BLEND", R3D_RenderModeBuildScreen());
    ENTRY(P_DOUBLE_SIDED, GROUP_PIPELINE, "DOUBLE SIDED", "Render both front and back faces of the subject.", "CULL OFF", R3D_RenderModeBuildDoubleSided());
    ENTRY(P_XRAY, GROUP_PIPELINE, "X-RAY", "Draw the subject even behind a foreground obstacle.", "DEPTH", R3D_RenderModeBuildXRay(cyan, 0.82f));
    ENTRY(P_OCCLUDED, GROUP_PIPELINE, "OCCLUDED ONLY", "Only the portion hidden behind geometry remains visible.", "DEPTH", R3D_RenderModeBuildOccludedOnly(coral, 0.88f));
    ENTRY(P_WIREFRAME, GROUP_PIPELINE, "WIREFRAME", "Expose polygon topology and mesh density.", "GEOMETRY", R3D_RenderModeBuildWireframe(cyan));
    ENTRY(P_WIREFRAME_OVERLAY, GROUP_PIPELINE, "WIREFRAME OVERLAY", "Show wire edges over original PBR surfaces.", "GEOMETRY", R3D_RenderModeBuildWireframeOverlay(gold));
    ENTRY(P_SILHOUETTE, GROUP_PIPELINE, "SILHOUETTE", "Fill the entire object with a flat stylized color.", "MASK", R3D_RenderModeBuildSilhouette(violet));
    ENTRY(P_SHADOW_ONLY, GROUP_PIPELINE, "SHADOW ONLY", "The object disappears, but still casts a physical shadow.", "SHADOW", R3D_RenderModeBuildShadowOnly());
    ENTRY(P_OVERLAY, GROUP_PIPELINE, "OVERLAY", "Render the object as a forward overlay.", "FORWARD", R3D_RenderModeBuildOverlay());
    R3D_RenderModeSetOpacity(&p[P_OVERLAY].built, 0.80f);
    ENTRY(P_DEPTH_ONLY, GROUP_PIPELINE, "DEPTH ONLY", "Write depth without visible surface color.", "DEPTH", R3D_RenderModeBuildDepthOnly());
    ENTRY(P_DITHERED, GROUP_PIPELINE, "DITHERED TRANSPARENCY", "Ordered coverage without alpha blending.", "MASK", R3D_RenderModeBuildDitheredTransparency(0.55f));
    ENTRY(P_STENCIL_TEST, GROUP_PIPELINE, "STENCIL TEST", "Visible only inside three invisible test volumes.", "STENCIL", R3D_RenderModeBuildStencilTest(0x01));
    ENTRY(P_OUTLINE, GROUP_PIPELINE, "HARD OUTLINE", "Sharp selection outline around each model.", "SILHOUETTE", R3D_RenderModeBuildOutline(cyan, 5.0f, false));

    ENTRY(P_COLOR_REPLACE, GROUP_COLOR, "COLOR REPLACE", "Replace the albedo but retain PBR lighting.", "COLOR", R3D_RenderModeBuildColorReplace(coral));
    ENTRY(P_COLOR_MULTIPLY, GROUP_COLOR, "COLOR MULTIPLY", "Multiply the original material by an aqua tint.", "COLOR", R3D_RenderModeBuildColorMultiply(cyan));
    ENTRY(P_COLOR_MIX, GROUP_COLOR, "COLOR MIX", "Blend the original albedo with violet.", "COLOR", R3D_RenderModeBuildColorMix(violet, 0.65f));
    ENTRY(P_COLOR_ADD, GROUP_COLOR, "COLOR ADD", "Add warm color to the original albedo.", "COLOR", R3D_RenderModeBuildColorAdd(gold));
    ENTRY(P_COLOR_SCREEN, GROUP_COLOR, "COLOR SCREEN", "Screen blend on the material, before lighting.", "COLOR", R3D_RenderModeBuildColorScreen(cyan));
    ENTRY(P_EMISSION_COLOR, GROUP_COLOR, "EMISSION COLOR", "Inject bright HDR emission into the object.", "HDR", R3D_RenderModeBuildEmissionColor(violet, 3.0f));
    ENTRY(P_ALBEDO_TEXTURE, GROUP_COLOR, "ALBEDO TEXTURE", "Layer the decal texture over all three subjects.", "TEXTURE", R3D_RenderModeBuildAlbedoTexture(s->decal, R3D_TEXTURE_BLEND_MIX, (Color){255, 255, 255, 255}, 0.8f));
    ENTRY(P_EMISSION_TEXTURE, GROUP_COLOR, "EMISSION TEXTURE", "Use the decal as an emissive overlay.", "TEXTURE + HDR", R3D_RenderModeBuildEmissionTexture(s->decal, R3D_TEXTURE_BLEND_ADD, cyan, 0.8f, 3.0f));

    ENTRY(P_RIM_GLOW, GROUP_SILHOUETTE, "RIM GLOW", "HDR Fresnel emission on the object's visible edges.", "CAMERA ANGLE", R3D_RenderModeBuildRimGlow(cyan, 4.5f, 2.8f));
    ENTRY(P_FRESNEL, GROUP_SILHOUETTE, "FRESNEL TRANSPARENCY", "Center becomes transparent; silhouettes remain opaque.", "CAMERA ANGLE", R3D_RenderModeBuildFresnelTransparency(0.09f, 0.96f, 2.0f));
    ENTRY(P_OUTER_GLOW, GROUP_SILHOUETTE, "OUTER GLOW", "Soft screen-space halo from explicitly selected models.", "SCREEN SPACE", R3D_RenderModeBuildOuterGlow(cyan, 17.0f, 4.0f, 3.0f));
    ENTRY(P_DROP_SHADOW, GROUP_SILHOUETTE, "DROP SHADOW", "Photoshop-style blurred and offset silhouette.", "SCREEN SPACE", R3D_RenderModeBuildDropShadow((Color){0, 0, 0, 255}, (Vector2){20.0f, 14.0f}, 18.0f, 2.0f, 0.85f));

    ENTRY(P_HOLOGRAM, GROUP_ANIMATED, "HOLOGRAM", "Scrolling cyan lines, translucency and signal dropouts.", "TIME / ALPHA", R3D_RenderModeBuildHologram(cyan, 0.78f, 23.0f, 1.3f, 3.2f));
    ENTRY(P_DISSOLVE, GROUP_ANIMATED, "DISSOLVE", "Procedural holes and a bright burning transition edge.", "TIME / SHADOWS", R3D_RenderModeBuildDissolve(coral, 0.40f, 4.0f, 0.10f, 3.5f));
    ENTRY(P_EMISSION_PULSE, GROUP_ANIMATED, "EMISSION PULSE", "Smooth, continuous breathing emission in HDR.", "TIME / HDR", R3D_RenderModeBuildEmissionPulse(violet, 0.20f, 3.4f, 0.65f));
    ENTRY(P_GLITCH, GROUP_ANIMATED, "GLITCH", "Subtle digital corruption with intermittent tearing bursts.", "TIME / VERTEX", R3D_RenderModeBuildGlitch(0.10f, 12.0f, 17.0f));

    ENTRY(P_UV_SCROLL, GROUP_PROCEDURAL, "UV SCROLL", "The texture moves while the geometry stays put.", "UV / TIME", R3D_RenderModeBuildUVScroll((Vector2){0.22f, 0.10f}));
    ENTRY(P_UV_DISTORTION, GROUP_PROCEDURAL, "UV DISTORTION", "Animated per-fragment UV waves with normal-map resampling.", "UV / TIME", R3D_RenderModeBuildUVDistortion(0.050f, 6.0f, 1.4f));
    ENTRY(P_GRADIENT, GROUP_PROCEDURAL, "GRADIENT", "A violet-to-cyan gradient in mesh-local coordinates.", "LOCAL SPACE", R3D_RenderModeBuildGradient(violet, cyan, (Vector3){0, 1, 0}, -1.0f, 1.0f, 0.92f));
    ENTRY(P_PIXELATE, GROUP_PROCEDURAL, "PIXELATE + QUANTIZE", "Coarse texture sampling and reduced albedo palette.", "UV / COLOR", R3D_RenderModeBuildPixelate(10.0f, 4.0f));
    ENTRY(P_DITHER_FADE, GROUP_PROCEDURAL, "DITHER FADE", "Animated 4x4 ordered fragment coverage.", "TIME / MASK", R3D_RenderModeBuildDitherFade(0.40f, 2.0f));
    ENTRY(P_SLICE_DISPLACE, GROUP_PROCEDURAL, "DISPLACED SLICE", "Shift separate horizontal bands; watch mesh tessellation.", "VERTEX / TIME", R3D_RenderModeBuildSliceDisplace(7.0f, 0.17f, 0.07f, 1.5f));
    ENTRY(P_CUT_CLIP, GROUP_PROCEDURAL, "CUT CLIP", "A moving clipping plane with an HDR hot edge.", "LOCAL SPACE", R3D_RenderModeBuildCutClip((Vector3){0, 1, 0}, 0.0f, coral, 0.10f, 4.0f));

    ENTRY(P_HOLO_FRESNEL, GROUP_COMBOS, "HOLOGRAM + FRESNEL", "Holographic scanlines fade toward the center of the view.", "COMPOSED / 2", R3D_RenderModeBuildDefault());
    ENTRY(P_DISSOLVE_RIM, GROUP_COMBOS, "DISSOLVE + RIM GLOW", "Molten holes plus a glowing camera-dependent silhouette.", "COMPOSED / 2", R3D_RenderModeBuildDefault());
    ENTRY(P_GLOW_SHADOW, GROUP_COMBOS, "OUTER GLOW + DROP SHADOW", "Two independent screen-space silhouettes in one scope.", "COMPOSED / 2", R3D_RenderModeBuildDefault());
    ENTRY(P_CUT_PULSE, GROUP_COMBOS, "CUT CLIP + EMISSION PULSE", "A moving laser cut across a breathing energy surface.", "COMPOSED / 2", R3D_RenderModeBuildDefault());
    ENTRY(P_GLITCH_DITHER, GROUP_COMBOS, "GLITCH + DITHER FADE", "Digital displacement and progressive ordered disappearance.", "COMPOSED / 2", R3D_RenderModeBuildDefault());
    ENTRY(P_GRADIENT_SCROLL, GROUP_COMBOS, "GRADIENT + UV SCROLL", "Gradient over a continuously scrolling textured surface.", "COMPOSED / 2", R3D_RenderModeBuildDefault());

    #undef ENTRY
}

static int first_of_group(const Showroom* s, Group group)
{
    for (int i = 0; i < PRESET_COUNT; i++)
        if (s->presets[i].group == group) return i;
    return 0;
}

static void update_parameters(Showroom* s, float phase, bool animate)
{
    float pulse = 0.5f + 0.5f * sinf(phase * 0.83f);
    float dissolve = animate ? 0.12f + 0.71f * pulse : 0.46f;
    float dither = animate ? 0.08f + 0.78f * (0.5f + 0.5f * sinf(phase * 0.55f)) : 0.48f;
    float cut = animate ? 0.50f * sinf(phase * 0.8f) : 0.0f;
    R3D_RenderModeSetProgress(&s->presets[P_DISSOLVE].built, dissolve);
    R3D_RenderModeSetProgress(&s->presets[P_DITHER_FADE].built, dither);
    R3D_RenderModeSetCutDistance(&s->presets[P_CUT_CLIP].built, cut);
}

static void push_effect(Showroom* s, int index)
{
    if (index == P_HOLO_FRESNEL) {
        R3D_PushBuiltRenderMode(s->presets[P_HOLOGRAM].built);
        R3D_PushBuiltRenderMode(s->presets[P_FRESNEL].built);
    } else if (index == P_DISSOLVE_RIM) {
        R3D_PushBuiltRenderMode(s->presets[P_DISSOLVE].built);
        R3D_PushBuiltRenderMode(s->presets[P_RIM_GLOW].built);
    } else if (index == P_GLOW_SHADOW) {
        R3D_PushBuiltRenderMode(s->presets[P_DROP_SHADOW].built);
        R3D_PushBuiltRenderMode(s->presets[P_OUTER_GLOW].built);
    } else if (index == P_CUT_PULSE) {
        R3D_PushBuiltRenderMode(s->presets[P_CUT_CLIP].built);
        R3D_PushBuiltRenderMode(s->presets[P_EMISSION_PULSE].built);
    } else if (index == P_GLITCH_DITHER) {
        R3D_PushBuiltRenderMode(s->presets[P_GLITCH].built);
        R3D_PushBuiltRenderMode(s->presets[P_DITHER_FADE].built);
    } else if (index == P_GRADIENT_SCROLL) {
        R3D_PushBuiltRenderMode(s->presets[P_UV_SCROLL].built);
        R3D_PushBuiltRenderMode(s->presets[P_GRADIENT].built);
    } else {
        R3D_PushBuiltRenderMode(s->presets[index].built);
    }
}

static void pop_effect(int index)
{
    R3D_PopBuiltRenderMode();
    if (index >= P_HOLO_FRESNEL) R3D_PopBuiltRenderMode();
}

static void setup_environment(void)
{
    R3D_SetAntiAliasingMode(R3D_ANTI_ALIASING_MODE_FXAA);
    R3D_SetAntiAliasingPreset(R3D_ANTI_ALIASING_PRESET_HIGH);

    R3D_ENVIRONMENT_SET(background.color, (Color){4, 7, 17, 255});
    R3D_ENVIRONMENT_SET(background.energy, 1.0f);
    R3D_ENVIRONMENT_SET(ambient.color, (Color){55, 78, 125, 255});
    R3D_ENVIRONMENT_SET(ambient.energy, 0.75f);
    R3D_ENVIRONMENT_SET(ssao.enabled, true);
    R3D_ENVIRONMENT_SET(ssao.sampleCount, 24);
    R3D_ENVIRONMENT_SET(ssao.radius, 0.85f);
    R3D_ENVIRONMENT_SET(ssao.intensity, 0.82f);
    R3D_ENVIRONMENT_SET(bloom.mode, R3D_BLOOM_MIX);
    R3D_ENVIRONMENT_SET(bloom.intensity, 0.072f);
    R3D_ENVIRONMENT_SET(bloom.threshold, 1.0f);
    R3D_ENVIRONMENT_SET(bloom.softThreshold, 0.65f);
    R3D_ENVIRONMENT_SET(bloom.levels, 1);
    R3D_ENVIRONMENT_SET(tonemap.mode, R3D_TONEMAP_ACES);
    R3D_ENVIRONMENT_SET(tonemap.exposure, 0.84f);
    R3D_ENVIRONMENT_SET(tonemap.white, 4.0f);
    R3D_ENVIRONMENT_SET(color.contrast, 1.08f);
    R3D_ENVIRONMENT_SET(color.saturation, 1.10f);
}

static void studio_init(Showroom* s)
{
    s->floorMesh = R3D_GenMeshPlane(24.0f, 19.0f, 1, 1);
    s->backdrop = R3D_GenMeshCube(16.0f, 7.0f, 0.2f);
    s->pedestal = R3D_GenMeshCube(2.9f, 0.30f, 2.9f);
    s->rimBar = R3D_GenMeshCube(2.96f, 0.065f, 2.96f);
    s->blocker = R3D_GenMeshCube(0.78f, 2.75f, 0.23f);
    s->stencilSphere = R3D_GenMeshSphere(0.88f, 36, 36);
    s->cube = R3D_GenMeshCubeEx(2.0f, 2.0f, 2.0f, 24, 24, 24);

    s->floorMat = R3D_GetDefaultMaterial();
    s->floorMat.albedo.color = (Color){24, 32, 53, 255};
    s->floorMat.orm.roughness = 0.50f;
    s->floorMat.orm.metalness = 0.26f;

    s->pedestalMat = R3D_GetDefaultMaterial();
    s->pedestalMat.albedo.color = (Color){66, 76, 105, 255};
    s->pedestalMat.orm.roughness = 0.30f;
    s->pedestalMat.orm.metalness = 0.76f;

    s->wallMat = R3D_GetDefaultMaterial();
    s->wallMat.albedo.color = (Color){10, 17, 35, 255};
    s->wallMat.orm.roughness = 0.90f;

    s->lightMat = R3D_GetDefaultMaterial();
    s->lightMat.albedo.color = (Color){25, 65, 95, 255};
    s->lightMat.emission.color = (Color){40, 190, 250, 255};
    s->lightMat.emission.energy = 5.0f;

    s->blockerMat = R3D_GetDefaultMaterial();
    s->blockerMat.albedo.color = (Color){190, 206, 225, 255};
    s->blockerMat.orm.metalness = 0.72f;
    s->blockerMat.orm.roughness = 0.25f;

    s->cubeMat = R3D_GetDefaultMaterial();
    s->cubeMat.albedo.color = (Color){255, 255, 255, 255};
    s->cubeMat.albedo.texture = s->decal;
    s->cubeMat.orm.metalness = 0.28f;
    s->cubeMat.orm.roughness = 0.48f;

    s->key = R3D_CreateSpotLight(
        (Vector3){-3.4f, 7.5f, 7.0f}, (Vector3){3.4f, -7.0f, -7.0f}, 28.0f,
        (Color){255, 215, 176, 255}, 2.7f
    );
    s->shadow = R3D_LoadShadowMap(R3D_LIGHT_SPOT);
    s->shadow.softness = 4.0f;
    s->fill = R3D_CreateOmniLight((Vector3){6.2f, 1.8f, 5.0f}, 18.0f, (Color){70, 155, 255, 255}, 2.25f);
    s->rim = R3D_CreateSpotLight(
        (Vector3){2.4f, 4.1f, -4.6f}, (Vector3){-2.4f, -4.1f, 4.6f}, 22.0f,
        (Color){244, 92, 186, 255}, 2.2f
    );
}

static void draw_studio(Showroom* s, Color accent)
{
    R3D_Material frame = s->lightMat;
    frame.emission.color = accent;

    R3D_DrawMesh(s->floorMesh, s->floorMat, (Vector3){0.0f, -1.74f, 0.0f}, 1.0f);
    R3D_DrawMesh(s->backdrop, s->wallMat, (Vector3){0.0f, 0.50f, -4.25f}, 1.0f);

    for (int i = 0; i < 3; i++)
    {
        float x = (float)(i - 1) * STAGE_X;
        R3D_DrawMesh(s->pedestal, s->pedestalMat, (Vector3){x, -1.25f, 0.0f}, 1.0f);
        R3D_DrawMesh(s->rimBar, frame, (Vector3){x, -1.067f, 0.0f}, 1.0f);
    }

    // Gallery architecture: illuminated pillars and ceiling rim.
    R3D_Mesh bar = s->blocker;
    R3D_DrawMesh(bar, frame, (Vector3){-6.4f, 1.0f, -3.96f}, 1.0f);
    R3D_DrawMesh(bar, frame, (Vector3){ 6.4f, 1.0f, -3.96f}, 1.0f);
}

static void draw_subjects(Showroom* s, int selected, float seconds, bool blocker)
{
    bool depthDemo = selected == P_XRAY || selected == P_OCCLUDED || selected == P_DEPTH_ONLY;
    if (blocker && depthDemo)
    {
        for (int i = 0; i < 3; i++)
            R3D_DrawMesh(s->blocker, s->blockerMat, (Vector3){(float)(i - 1) * STAGE_X + 0.43f, -0.10f, 1.47f}, 1.0f);
    }

    if (selected == P_STENCIL_TEST)
    {
        R3D_PushBuiltRenderMode(s->stencilWriter);
        for (int i = 0; i < 3; i++)
            R3D_DrawMesh(s->stencilSphere, R3D_MATERIAL_BASE, (Vector3){(float)(i - 1) * STAGE_X + 0.30f, 0.24f, 1.00f}, 1.0f);
        R3D_PopBuiltRenderMode();
    }

    float rotation = seconds * 0.34f;
    push_effect(s, selected);

    // Cube: a high-resolution procedural mesh with a very visible decal texture.
    Matrix cubeMatrix = MatrixMultiply(MatrixRotateY(rotation + 0.30f), MatrixRotateX(-0.18f));
    cubeMatrix = MatrixMultiply(cubeMatrix, MatrixTranslate(-STAGE_X, 0.12f, 0.0f));
    R3D_DrawMeshPro(s->cube, s->cubeMat, cubeMatrix);

    // Animated character: pose and skinning change every frame.
    Matrix characterMatrix = model_in_podium(s->character, 0.0f, 2.7f, -0.22f);
    R3D_DrawAnimatedModelPro(s->character, s->player, characterMatrix);

    // Helmet: textured imported PBR model with metallic response.
    Matrix helmetMatrix = model_in_podium(s->helmet, STAGE_X, 2.50f, rotation + 0.30f);
    R3D_DrawModelPro(s->helmet, helmetMatrix);

    pop_effect(selected);
}

static void draw_arrow(Rectangle bounds, bool right, Color accent, bool hovered)
{
    Color bg = hovered ? (Color){26, 50, 75, 244} : (Color){11, 23, 42, 230};
    DrawRectangleRounded(bounds, 0.20f, 8, bg);
    DrawRectangleRoundedLinesEx(bounds, 0.20f, 8, 2.0f, Fade(accent, hovered ? 0.95f : 0.45f));
    float midX = bounds.x + bounds.width * 0.5f;
    float midY = bounds.y + bounds.height * 0.5f;
    float step = right ? 1.0f : -1.0f;
    Vector2 a = {midX - step * 8.0f, midY - 11.0f};
    Vector2 b = {midX + step * 8.0f, midY};
    Vector2 c = {midX - step * 8.0f, midY + 11.0f};
    DrawLineEx(a, b, 3.3f, accent);
    DrawLineEx(b, c, 3.3f, accent);
}

static void draw_hud(Showroom* s, int selected, float seconds, float transition, bool rotating, bool animate, bool blocker)
{
    int w = GetScreenWidth();
    int h = GetScreenHeight();
    Preset* p = &s->presets[selected];
    Color accent = GROUP_COLORS[p->group];
    int labelW = MeasureText(p->title, 37);

    // Dark transparent glass panels over the 3D render.
    DrawRectangleGradientV(0, 0, w, 194, (Color){5, 10, 22, 250}, (Color){6, 12, 25, 220});
    DrawRectangle(0, 191, w, 2, Fade(accent, 0.75f));
    DrawRectangleGradientV(0, h - 181, w, 181, (Color){6, 14, 28, 210}, (Color){3, 8, 19, 252});
    DrawRectangle(0, h - 181, w, 2, Fade(accent, 0.55f));

    // Identity header.
    DrawRectangle(31, 28, 5, 25, accent);
    DrawText("MEATCORPS  /  R3D", 48, 29, 20, RAYWHITE);
    DrawText("EFFECTS ATLAS  //  VISUAL VALIDATION LAB", 315, 32, 16, (Color){140, 164, 190, 255});
    DrawText("FXAA", w - 170, 32, 17, accent);
    DrawText(TextFormat("%02d / %02d", selected + 1, PRESET_COUNT), w - 96, 31, 18, RAYWHITE);

    // Category tabs.
    int tabX = 32;
    for (int i = 0; i < GROUP_COUNT; i++)
    {
        int tw = MeasureText(GROUP_NAMES[i], 16) + 34;
        Rectangle rect = box_rect((float)tabX, 80.0f, (float)tw, 34.0f);
        bool on = p->group == (Group)i;
        DrawRectangleRounded(rect, 0.22f, 6, on ? Fade(accent, 0.20f) : (Color){15, 24, 42, 214});
        DrawRectangleRoundedLinesEx(rect, 0.22f, 6, 1.0f, Fade(on ? accent : LIGHTGRAY, on ? 0.8f : 0.21f));
        DrawText(GROUP_NAMES[i], tabX + 17, 89, 16, on ? RAYWHITE : (Color){115, 130, 154, 255});
        tabX += tw + 10;
    }

    // Selected effect heading.
    float entrance = 1.0f - clamp01(transition / 0.26f);
    int headingX = 49 + (int)((1.0f - entrance) * 12.0f);
    DrawText(p->title, headingX, 128, 37, accent);
    DrawText("SELECTED PRESET", headingX + labelW + 25, 147, 14, (Color){132, 157, 183, 255});
    DrawText(p->detail, w - MeasureText(p->detail, 17) - 46, 142, 17, RAYWHITE);

    // Small technical labels for the three permanent subjects.
    const char* names[3] = {"01  PROCEDURAL CUBE", "02  ANIMATED CHARACTER", "03  DAMAGED HELMET"};
    const char* meta[3] = {"TEXTURE / TESSELLATED", "SKINNED / ANIMATION", "IMPORTED / PBR"};
    for (int i = 0; i < 3; i++)
    {
        int midX = (int)((float)w * (0.30f + 0.20f * i));
        int nameWidth = MeasureText(names[i], 17);
        int metaWidth = MeasureText(meta[i], 13);
        DrawText(names[i], midX - nameWidth / 2, h - 172, 17, RAYWHITE);
        DrawText(meta[i], midX - metaWidth / 2, h - 145, 13, (Color){126, 146, 170, 255});
        DrawRectangle(midX - 105, h - 121, 210, 2, (Color){47, 64, 85, 175});
        DrawRectangle(midX - 105, h - 121, 210, 2, Fade(accent, 0.42f + 0.15f * sinf(seconds * 1.8f + (float)i)));
    }

    // Effect description and controls.
    DrawText(p->description, 62, h - 101, 20, (Color){206, 219, 235, 255});
    DrawText("LEFT / RIGHT  MODE      TAB  CATEGORY      SPACE  ROTATE      A  PARAM SWEEP      B  OBSTACLE      R  RESET SWEEP", 62, h - 58, 15, (Color){130, 155, 183, 255});
    DrawText(TextFormat("%s  |  %s  |  %s", rotating ? "ROTATING" : "PAUSED", animate ? "SWEEP ON" : "SWEEP OFF", blocker ? "OBSTACLE ON" : "OBSTACLE OFF"), w - 375, h - 58, 15, accent);

    // Navigator side buttons.
    Rectangle left = box_rect(16.0f, (float)h * 0.50f - 36.0f, 52.0f, 72.0f);
    Rectangle right = box_rect((float)w - 68.0f, (float)h * 0.50f - 36.0f, 52.0f, 72.0f);
    Vector2 mouse = GetMousePosition();
    draw_arrow(left, false, accent, CheckCollisionPointRec(mouse, left));
    draw_arrow(right, true, accent, CheckCollisionPointRec(mouse, right));

    // Page progress ticks (one per preset).
    int barLeft = 31;
    float step = (float)(w - 62) / (float)PRESET_COUNT;
    for (int i = 0; i < PRESET_COUNT; i++)
    {
        Rectangle tick = box_rect(barLeft + (float)i * step, h - 8.0f, fmaxf(2.0f, step - 3.0f), 4.0f);
        Color color = i == selected ? accent : (i < selected ? Fade(accent, 0.45f) : (Color){44, 60, 80, 255});
        DrawRectangleRec(tick, color);
    }
}

static void navigate_mouse(const Showroom* s, int* selection, int w, int h)
{
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    Vector2 mouse = GetMousePosition();
    Rectangle left = box_rect(16.0f, (float)h * 0.50f - 36.0f, 52.0f, 72.0f);
    Rectangle right = box_rect((float)w - 68.0f, (float)h * 0.50f - 36.0f, 52.0f, 72.0f);
    if (CheckCollisionPointRec(mouse, left)) *selection = (*selection + PRESET_COUNT - 1) % PRESET_COUNT;
    if (CheckCollisionPointRec(mouse, right)) *selection = (*selection + 1) % PRESET_COUNT;
    int tabX = 32;
    for (int i = 0; i < GROUP_COUNT; i++)
    {
        int tw = MeasureText(GROUP_NAMES[i], 16) + 34;
        if (CheckCollisionPointRec(mouse, box_rect((float)tabX, 80.0f, (float)tw, 34.0f)))
            *selection = first_of_group(s, (Group)i);
        tabX += tw + 10;
    }
}

static void destroy_showroom(Showroom* s)
{
    for (int i = 0; i < PRESET_COUNT; i++) R3D_UnloadBuiltRenderMode(&s->presets[i].built);
    R3D_UnloadBuiltRenderMode(&s->stencilWriter);
    R3D_UnloadShadowMap(s->shadow);
    R3D_UnloadAnimationPlayer(s->player);
    R3D_UnloadAnimationLib(s->animations);
    R3D_UnloadModel(s->helmet, true);
    R3D_UnloadModel(s->character, true);
    R3D_UnloadMesh(s->cube);
    R3D_UnloadMesh(s->floorMesh);
    R3D_UnloadMesh(s->pedestal);
    R3D_UnloadMesh(s->rimBar);
    R3D_UnloadMesh(s->backdrop);
    R3D_UnloadMesh(s->blocker);
    R3D_UnloadMesh(s->stencilSphere);
    R3D_UnloadTexture(s->decal);
    R3D_UnloadAmbientMap(s->ambient);
    R3D_UnloadCubemap(s->hdri);
}

int main(void)
{
    InitWindow(SCREEN_W, SCREEN_H, "[r3d] Meatcorps - Effects Atlas");
    SetTargetFPS(60);

    if (!R3D_Init(SCREEN_W, SCREEN_H))
    {
        CloseWindow();
        return 1;
    }

    setup_environment();
    R3D_SetTextureFilter(TEXTURE_FILTER_ANISOTROPIC_4X);

    Showroom s = {0};
    s.decal = R3D_LoadTextureEx(RESOURCES_PATH "images/decal.png", TEXTURE_WRAP_REPEAT, TEXTURE_FILTER_BILINEAR, true);
    s.helmet = R3D_LoadModel(RESOURCES_PATH "models/DamagedHelmet.glb");
    s.character = R3D_LoadModel(RESOURCES_PATH "models/CesiumMan.glb");

    if (!s.decal.id || !s.helmet.meshes || !s.character.meshes)
    {
        TraceLog(LOG_ERROR, "Effects Atlas: missing decal, DamagedHelmet or CesiumMan resource");
        if (s.helmet.meshes) R3D_UnloadModel(s.helmet, true);
        if (s.character.meshes) R3D_UnloadModel(s.character, true);
        if (s.decal.id) R3D_UnloadTexture(s.decal);
        R3D_Close();
        CloseWindow();
        return 1;
    }

    s.animations = R3D_LoadAnimationLib(RESOURCES_PATH "models/CesiumMan.glb");
    s.player = R3D_LoadAnimationPlayer(s.character.skeleton, s.animations);
    R3D_SetAnimationLoop(&s.player, 0, true);
    R3D_PlayAnimation(&s.player, 0);

    s.hdri = R3D_LoadCubemap(RESOURCES_PATH "panorama/indoor.hdr", R3D_CUBEMAP_LAYOUT_AUTO_DETECT);
    s.ambient = R3D_GenAmbientMap(s.hdri, R3D_AMBIENT_ILLUMINATION | R3D_AMBIENT_REFLECTION);
    R3D_ENVIRONMENT_SET(ambient.map, s.ambient);

    studio_init(&s);
    make_presets(&s);
    s.stencilWriter = R3D_RenderModeBuildStencilMask(0x01);

    Camera3D camera = {
        .position = {0.0f, 2.5f, 13.7f},
        .target = {0.0f, -0.10f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 42.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    int selected = P_HOLO_FRESNEL;
    int previous = selected;
    bool rotating = true;
    bool animate = true;
    bool blocker = true;
    float phase = 0.0f;
    float rotation = 0.0f;
    float transition = 0.0f;

    while (!WindowShouldClose())
    {
        float delta = fminf(GetFrameTime(), 0.05f);

        if (IsKeyPressed(KEY_RIGHT)) selected = (selected + 1) % PRESET_COUNT;
        if (IsKeyPressed(KEY_LEFT)) selected = (selected + PRESET_COUNT - 1) % PRESET_COUNT;
        if (IsKeyPressed(KEY_TAB))
        {
            int offset = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ? GROUP_COUNT - 1 : 1;
            Group current = s.presets[selected].group;
            selected = first_of_group(&s, (Group)((current + offset) % GROUP_COUNT));
        }
        if (IsKeyPressed(KEY_SPACE)) rotating = !rotating;
        if (IsKeyPressed(KEY_A)) animate = !animate;
        if (IsKeyPressed(KEY_B)) blocker = !blocker;
        if (IsKeyPressed(KEY_R)) phase = 0.0f;

        navigate_mouse(&s, &selected, GetScreenWidth(), GetScreenHeight());
        if (selected != previous)
        {
            previous = selected;
            transition = 0.26f;
        }
        transition = fmaxf(0.0f, transition - delta);

        if (animate) phase += delta;
        if (rotating) rotation += delta;
        R3D_UpdateAnimationPlayer(&s.player, delta);
        update_parameters(&s, phase, animate);

        BeginDrawing();
        ClearBackground((Color){4, 7, 16, 255});
        R3D_Begin(camera);

        R3D_PushLightEx(s.key, s.shadow, true);
        R3D_PushLight(s.fill);
        R3D_PushLight(s.rim);
        draw_studio(&s, GROUP_COLORS[s.presets[selected].group]);
        draw_subjects(&s, selected, rotation, blocker);

        R3D_End();
        draw_hud(&s, selected, phase, transition, rotating, animate, blocker);
        EndDrawing();
    }

    destroy_showroom(&s);
    R3D_Close();
    CloseWindow();
    return 0;
}
