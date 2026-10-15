#include <r3d/r3d_render_mode_builder.h>

#include "./modules/r3d_render.h"
#include <math.h>

void R3D_PushBuiltRenderMode(R3D_BuiltRenderMode built)
{
    r3d_render_built_mode_push(built);
}

void R3D_PopBuiltRenderMode(void)
{
    r3d_render_built_mode_pop();
}

void R3D_UnloadBuiltRenderMode(R3D_BuiltRenderMode* built)
{
    if (!built) return;

    if (built->effect)
        R3D_UnloadSurfaceEffect(built->effect);

    *built = (R3D_BuiltRenderMode){0};
}

R3D_BuiltRenderMode R3D_RenderModeBuildDefault(void)
{
    return (R3D_BuiltRenderMode){0};
}

R3D_BuiltRenderMode R3D_RenderModeBuildUnlit(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask = R3D_RENDER_MODE_OVERRIDE_UNLIT;
    built.mode.unlit = true;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildAlphaBlend(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ALPHA;
    built.kind = R3D_BUILT_MODE_ALPHA_BLEND;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildAdditive(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ADDITIVE;
    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildMultiply(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_MULTIPLY;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildDoubleSided(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask = R3D_RENDER_MODE_OVERRIDE_CULL;
    built.mode.cullMode = R3D_CULL_NONE;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildShadowOnly(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_SHADOW |
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY;

    built.mode.shadowCastMode = R3D_SHADOW_CAST_ONLY_AUTO;
    built.mode.transparencyMode = R3D_TRANSPARENCY_OPAQUE;

    return built;
}

static float r3d_mode_clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

/*
 * Color operations work on the surface's linear-space ALBEDO.
 * Emission is added separately and may exceed 1.0 for HDR.
 *
 * Color alpha is intentionally not used here. Transparency is
 * managed by the existing opacity/transparency effects.
 */
static const char R3D_COLOR_EFFECT_SOURCE[] =
    "uniform vec3 effectColor = vec3(1.0);\n"
    "uniform float effectStrength = 1.0;\n"
    "uniform float effectIntensity = 1.0;\n"
    "uniform int effectOperation = 0;\n"
    "void fragment() {\n"
    "    if (effectOperation == 0) {\n"
    "        ALBEDO = effectColor;\n"
    "    } else if (effectOperation == 1) {\n"
    "        ALBEDO *= effectColor;\n"
    "    } else if (effectOperation == 2) {\n"
    "        ALBEDO = mix(ALBEDO, effectColor, effectStrength);\n"
    "    } else if (effectOperation == 3) {\n"
    "        ALBEDO = clamp(ALBEDO + effectColor, 0.0, 1.0);\n"
    "    } else if (effectOperation == 4) {\n"
    "        vec3 base = clamp(ALBEDO, 0.0, 1.0);\n"
    "        ALBEDO = 1.0 - (1.0 - base) * (1.0 - effectColor);\n"
    "    } else if (effectOperation == 5) {\n"
    "        EMISSION += effectColor * effectIntensity;\n"
    "    }\n"
    "}\n";

/* Convert the public raylib Color from sRGB to linear RGB. */
static float r3d_mode_color_channel(unsigned char channel)
{
    float value = channel / 255.0f;

    if (value <= 0.04045f)
        return value / 12.92f;

    return powf((value + 0.055f) / 1.055f, 2.4f);
}

static Vector3 r3d_mode_color_linear(Color color)
{
    return (Vector3){
        r3d_mode_color_channel(color.r),
        r3d_mode_color_channel(color.g),
        r3d_mode_color_channel(color.b)
    };
}

static R3D_BuiltRenderMode r3d_mode_build_color(Color color, R3D_BuiltRenderModeKind kind, int operation, float strength, float intensity)
{
    R3D_BuiltRenderMode built = {0};

    built.kind = kind;
    built.effect = R3D_LoadSurfaceEffectFromMemory(R3D_COLOR_EFFECT_SOURCE);

    if (!built.effect)
        return built;

    Vector3 linearColor = r3d_mode_color_linear(color);

    strength = isfinite(strength) ? r3d_mode_clamp01(strength) : 0.0f;
    intensity = isfinite(intensity) ? fmaxf(intensity, 0.0f) : 0.0f;

    R3D_SetSurfaceEffectUniform(built.effect, "effectColor", &linearColor);
    R3D_SetSurfaceEffectUniform(built.effect, "effectStrength", &strength);
    R3D_SetSurfaceEffectUniform(built.effect, "effectIntensity", &intensity);
    R3D_SetSurfaceEffectUniform(built.effect, "effectOperation", &operation);

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildColorReplace(Color color)
{
    return r3d_mode_build_color(color, R3D_BUILT_MODE_COLOR_REPLACE, 0, 1.0f, 1.0f);
}

R3D_BuiltRenderMode R3D_RenderModeBuildColorMultiply(Color color)
{
    return r3d_mode_build_color(color, R3D_BUILT_MODE_COLOR_TINT, 1, 1.0f, 1.0f);
}

R3D_BuiltRenderMode R3D_RenderModeBuildColorTint(Color color)
{
    return R3D_RenderModeBuildColorMultiply(color);
}

R3D_BuiltRenderMode R3D_RenderModeBuildColorMix(Color color, float strength)
{
    return r3d_mode_build_color(color, R3D_BUILT_MODE_COLOR_MIX, 2, strength, 1.0f);
}

R3D_BuiltRenderMode R3D_RenderModeBuildColorAdd(Color color)
{
    return r3d_mode_build_color(color, R3D_BUILT_MODE_COLOR_ADD, 3, 1.0f, 1.0f);
}

R3D_BuiltRenderMode R3D_RenderModeBuildColorScreen(Color color)
{
    return r3d_mode_build_color(color, R3D_BUILT_MODE_COLOR_SCREEN, 4, 1.0f, 1.0f);
}

R3D_BuiltRenderMode R3D_RenderModeBuildEmissionColor(Color color, float intensity)
{
    return r3d_mode_build_color(color, R3D_BUILT_MODE_EMISSION_COLOR, 5, 1.0f, intensity);
}

/*
 * Surface texture injection.
 *
 * effectTarget:
 *   0 = ALBEDO
 *   1 = EMISSION
 *
 * effectBlend matches R3D_TextureBlendMode.
 *
 * REPLACE overrides RGB directly. For the other blend modes,
 * texture alpha controls how much of the texture layer is applied.
 *
 * Emission is never upper-clamped, preserving HDR values.
 */
static const char R3D_TEXTURE_EFFECT_SOURCE[] =
    "uniform sampler2D effectTexture;\n"
    "uniform vec3 effectTint = vec3(1.0);\n"
    "uniform float effectStrength = 1.0;\n"
    "uniform float effectIntensity = 1.0;\n"
    "uniform int effectBlend = 0;\n"
    "uniform int effectTarget = 0;\n"
    "void fragment() {\n"
    "    vec4 texel = texture(effectTexture, TEXCOORD);\n"
    "    vec3 layer = texel.rgb * effectTint;\n"
    "    float amount = clamp(effectStrength, 0.0, 1.0) * texel.a;\n"
    "    if (effectTarget == 0) {\n"
    "        vec3 base = ALBEDO;\n"
    "        if (effectBlend == 0) {\n"
    "            ALBEDO = layer;\n"
    "        } else if (effectBlend == 1) {\n"
    "            ALBEDO = mix(base, base * layer, amount);\n"
    "        } else if (effectBlend == 2) {\n"
    "            ALBEDO = mix(base, layer, amount);\n"
    "        } else if (effectBlend == 3) {\n"
    "            ALBEDO = clamp(base + layer * amount, 0.0, 1.0);\n"
    "        } else if (effectBlend == 4) {\n"
    "            vec3 a = clamp(base, 0.0, 1.0);\n"
    "            vec3 b = clamp(layer, 0.0, 1.0);\n"
    "            vec3 screened = 1.0 - (1.0 - a) * (1.0 - b);\n"
    "            ALBEDO = mix(base, screened, amount);\n"
    "        }\n"
    "    } else {\n"
    "        layer *= effectIntensity;\n"
    "        vec3 base = EMISSION;\n"
    "        if (effectBlend == 0) {\n"
    "            EMISSION = layer;\n"
    "        } else if (effectBlend == 1) {\n"
    "            EMISSION = mix(base, base * layer, amount);\n"
    "        } else if (effectBlend == 2) {\n"
    "            EMISSION = mix(base, layer, amount);\n"
    "        } else if (effectBlend == 3) {\n"
    "            EMISSION = base + layer * amount;\n"
    "        } else if (effectBlend == 4) {\n"
    "            EMISSION = base + (1.0 - clamp(base, 0.0, 1.0))\n"
    "                * layer * amount;\n"
    "        }\n"
    "    }\n"
    "}\n";

static R3D_BuiltRenderMode r3d_mode_build_texture(Texture2D texture, R3D_TextureBlendMode blendMode, Color tint, float blendStrength, float hdrIntensity, bool emission)
{
    R3D_BuiltRenderMode built = {0};

    if (texture.id == 0)
    {
        TraceLog(LOG_WARNING, "R3D: Texture render mode requires a valid texture");
        return built;
    }

    int operation = (int)blendMode;

    if (operation < R3D_TEXTURE_BLEND_REPLACE ||
        operation > R3D_TEXTURE_BLEND_SCREEN)
    {
        TraceLog(LOG_WARNING, "R3D: Invalid texture blend mode");
        return built;
    }

    built.kind = emission
        ? R3D_BUILT_MODE_EMISSION_TEXTURE
        : R3D_BUILT_MODE_ALBEDO_TEXTURE;

    built.effect = R3D_LoadSurfaceEffectFromMemory(R3D_TEXTURE_EFFECT_SOURCE);

    if (!built.effect)
        return built;

    Vector3 linearTint = r3d_mode_color_linear(tint);

    float strength = isfinite(blendStrength)
        ? r3d_mode_clamp01(blendStrength)
        : 0.0f;

    float intensity = isfinite(hdrIntensity)
        ? fmaxf(hdrIntensity, 0.0f)
        : 0.0f;

    int target = emission ? 1 : 0;

    R3D_SetSurfaceEffectSampler(built.effect, "effectTexture", texture);
    R3D_SetSurfaceEffectUniform(built.effect, "effectTint", &linearTint);
    R3D_SetSurfaceEffectUniform(built.effect, "effectStrength", &strength);
    R3D_SetSurfaceEffectUniform(built.effect, "effectIntensity", &intensity);
    R3D_SetSurfaceEffectUniform(built.effect, "effectBlend", &operation);
    R3D_SetSurfaceEffectUniform(built.effect, "effectTarget", &target);

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildAlbedoTexture(Texture2D texture, R3D_TextureBlendMode blendMode, Color tint, float blendStrength)
{
    return r3d_mode_build_texture(texture, blendMode, tint, blendStrength, 1.0f, false);
}

R3D_BuiltRenderMode R3D_RenderModeBuildEmissionTexture(Texture2D texture, R3D_TextureBlendMode blendMode, Color tint, float blendStrength, float hdrIntensity)
{
    return r3d_mode_build_texture(texture, blendMode, tint, blendStrength, hdrIntensity, true);
}

/*
 * Rim Glow:
 *
 * Uses the world-space geometric normal and view direction.
 * The contribution is strongest at grazing angles.
 *
 * Lit surfaces receive HDR emission.
 * Unlit surfaces receive the contribution directly in ALBEDO,
 * because the unlit output does not use the emission channel.
 *
 * Depth and shadow passes do not evaluate the view-dependent effect.
 */
static const char R3D_RIM_GLOW_SOURCE[] =
    "uniform vec3 rimColor = vec3(1.0);\n"
    "uniform float rimIntensity = 1.0;\n"
    "uniform float rimPower = 3.0;\n"
    "void fragment() {\n"
    "#if defined(GEOMETRY) || defined(FORWARD) || defined(PROBE_FORWARD) || defined(UNLIT)\n"
    "    vec3 cameraPosition;\n"
    "#if defined(GEOMETRY) || defined(UNLIT)\n"
    "    cameraPosition = uView.position;\n"
    "#else\n"
    "    cameraPosition = uViewPosition;\n"
    "#endif\n"
    "    vec3 V = normalize(cameraPosition - POSITION);\n"
    "    vec3 N = normalize(NORMAL);\n"
    "    float facing = clamp(abs(dot(N, V)), 0.0, 1.0);\n"
    "    float rim = pow(1.0 - facing, max(rimPower, 0.01));\n"
    "    vec3 glow = rimColor * rimIntensity * rim;\n"
    "#if defined(UNLIT)\n"
    "    ALBEDO += glow;\n"
    "#else\n"
    "    EMISSION += glow;\n"
    "#endif\n"
    "#endif\n"
    "}\n";

R3D_BuiltRenderMode R3D_RenderModeBuildRimGlow(Color color, float intensity, float power)
{
    R3D_BuiltRenderMode built = {0};

    built.effect = R3D_LoadSurfaceEffectFromMemory(R3D_RIM_GLOW_SOURCE);

    if (!built.effect)
        return built;

    built.kind = R3D_BUILT_MODE_RIM_GLOW;

    Vector3 linearColor = r3d_mode_color_linear(color);

    intensity = isfinite(intensity) ? fmaxf(intensity, 0.0f) : 0.0f;
    power = isfinite(power) ? fmaxf(power, 0.01f) : 3.0f;

    R3D_SetSurfaceEffectUniform(built.effect, "rimColor", &linearColor);
    R3D_SetSurfaceEffectUniform(built.effect, "rimIntensity", &intensity);
    R3D_SetSurfaceEffectUniform(built.effect, "rimPower", &power);

    return built;
}

/*
 * Fresnel Transparency:
 *
 * frontOpacity controls surfaces facing the camera.
 * edgeOpacity controls grazing-angle surfaces.
 * power controls the transition.
 *
 * The existing material alpha remains a multiplier.
 * fresnelOpacity is a separate overall multiplier used by
 * R3D_RenderModeSetOpacity().
 */
static const char R3D_FRESNEL_TRANSPARENCY_SOURCE[] =
    "uniform float fresnelFrontOpacity = 0.1;\n"
    "uniform float fresnelEdgeOpacity = 0.9;\n"
    "uniform float fresnelPower = 2.0;\n"
    "uniform float fresnelOpacity = 1.0;\n"
    "void fragment() {\n"
    "#if defined(FORWARD) || defined(PROBE_FORWARD) || defined(UNLIT)\n"
    "    vec3 cameraPosition;\n"
    "#if defined(UNLIT)\n"
    "    cameraPosition = uView.position;\n"
    "#else\n"
    "    cameraPosition = uViewPosition;\n"
    "#endif\n"
    "    vec3 V = normalize(cameraPosition - POSITION);\n"
    "    vec3 N = normalize(NORMAL);\n"
    "    float facing = clamp(abs(dot(N, V)), 0.0, 1.0);\n"
    "    float fresnel = pow(1.0 - facing, max(fresnelPower, 0.01));\n"
    "    float opacity = mix(fresnelFrontOpacity, fresnelEdgeOpacity, fresnel);\n"
    "    ALPHA *= opacity * fresnelOpacity;\n"
    "#endif\n"
    "}\n";

R3D_BuiltRenderMode R3D_RenderModeBuildFresnelTransparency(float frontOpacity, float edgeOpacity, float power)
{
    R3D_BuiltRenderMode built = {0};

    built.effect = R3D_LoadSurfaceEffectFromMemory(R3D_FRESNEL_TRANSPARENCY_SOURCE);

    if (!built.effect)
        return built;

    built.kind = R3D_BUILT_MODE_FRESNEL_TRANSPARENCY;

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND |
        R3D_RENDER_MODE_OVERRIDE_SHADOW;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ALPHA;
    built.mode.shadowCastMode = R3D_SHADOW_CAST_DISABLED;

    frontOpacity = isfinite(frontOpacity) ? r3d_mode_clamp01(frontOpacity) : 0.0f;
    edgeOpacity = isfinite(edgeOpacity) ? r3d_mode_clamp01(edgeOpacity) : 1.0f;
    power = isfinite(power) ? fmaxf(power, 0.01f) : 2.0f;

    R3D_SetSurfaceEffectUniform(built.effect, "fresnelFrontOpacity", &frontOpacity);
    R3D_SetSurfaceEffectUniform(built.effect, "fresnelEdgeOpacity", &edgeOpacity);
    R3D_SetSurfaceEffectUniform(built.effect, "fresnelPower", &power);

    return built;
}

/*
 * Screen-space silhouette builders.
 *
 * These have no surface shader and do not change their source material.
 * The renderer captures their parameters and selected draw submissions.
 *
 * Blur radius: 0..24 pixels.
 * Spread: 0..8 pixels.
 */
R3D_BuiltRenderMode R3D_RenderModeBuildOuterGlow(Color color, float radius, float spread, float intensity)
{
    R3D_BuiltRenderMode built = {0};

    built.kind = R3D_BUILT_MODE_OUTER_GLOW;
    built.silhouette.color = color;
    built.silhouette.radius = isfinite(radius) ? fmaxf(0.0f, fminf(radius, 24.0f)) : 0.0f;
    built.silhouette.spread = isfinite(spread) ? fmaxf(0.0f, fminf(spread, 8.0f)) : 0.0f;
    built.silhouette.intensity = isfinite(intensity) ? fmaxf(0.0f, intensity) : 0.0f;
    built.silhouette.opacity = 1.0f;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildDropShadow(Color color, Vector2 offset, float blurRadius, float spread, float opacity)
{
    R3D_BuiltRenderMode built = {0};

    built.kind = R3D_BUILT_MODE_DROP_SHADOW;
    built.silhouette.color = color;
    built.silhouette.offset = (Vector2){
        isfinite(offset.x) ? offset.x : 0.0f,
        isfinite(offset.y) ? offset.y : 0.0f
    };
    built.silhouette.radius = isfinite(blurRadius) ? fmaxf(0.0f, fminf(blurRadius, 24.0f)) : 0.0f;
    built.silhouette.spread = isfinite(spread) ? fmaxf(0.0f, fminf(spread, 8.0f)) : 0.0f;
    built.silhouette.intensity = 1.0f;
    built.silhouette.opacity = isfinite(opacity) ? r3d_mode_clamp01(opacity) : 0.0f;

    return built;
}

static R3D_BuiltRenderMode r3d_mode_build_colored_forward(Color color, float opacity, R3D_CompareMode depthCompare)
{
    R3D_BuiltRenderMode built = {0};

    color.a = (unsigned char)(r3d_mode_clamp01(opacity) * 255.0f + 0.5f);

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND |
        R3D_RENDER_MODE_OVERRIDE_DEPTH_COMPARE |
        R3D_RENDER_MODE_OVERRIDE_UNLIT |
        R3D_RENDER_MODE_OVERRIDE_ALBEDO |
        R3D_RENDER_MODE_OVERRIDE_SHADOW;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ALPHA;
    built.mode.depthCompare = depthCompare;
    built.mode.unlit = true;
    built.mode.albedoColor = color;
    built.mode.shadowCastMode = R3D_SHADOW_CAST_DISABLED;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildXRay(Color color, float opacity)
{
    R3D_BuiltRenderMode built = r3d_mode_build_colored_forward(color, opacity, R3D_COMPARE_ALWAYS);
    built.kind = R3D_BUILT_MODE_XRAY;
    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildOccludedOnly(Color color, float opacity)
{
    R3D_BuiltRenderMode built = r3d_mode_build_colored_forward(color, opacity, R3D_COMPARE_GREATER);
    built.kind = R3D_BUILT_MODE_OCCLUDED_ONLY;
    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildWireframe(Color color)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_WIRE_COLOR |
        R3D_RENDER_MODE_OVERRIDE_CULL;

    built.mode.renderPass = R3D_RENDER_PASS_WIREFRAME;
    built.mode.wireColor = color;
    built.mode.cullMode = R3D_CULL_NONE;
    built.kind = R3D_BUILT_MODE_WIREFRAME;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildWireframeOverlay(Color color)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_WIRE_COLOR;

    built.mode.renderPass = R3D_RENDER_PASS_WIREFRAME_OVERLAY;
    built.mode.wireColor = color;
    built.kind = R3D_BUILT_MODE_WIREFRAME_OVERLAY;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildSilhouette(Color color)
{
    static const char source[] =
        "uniform vec3 silhouetteColor = vec3(1.0, 1.0, 1.0);\n"
        "void fragment() {\n"
        "    ALBEDO = silhouetteColor;\n"
        "}\n";

    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_UNLIT |
        R3D_RENDER_MODE_OVERRIDE_ALBEDO |
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY;

    built.mode.unlit = true;
    built.mode.albedoColor = color;
    built.mode.transparencyMode = color.a == 255 ? R3D_TRANSPARENCY_OPAQUE : R3D_TRANSPARENCY_BLEND;
    built.kind = R3D_BUILT_MODE_SILHOUETTE;

    built.effect = R3D_LoadSurfaceEffectFromMemory(source);

    if (built.effect)
    {
        Vector3 rgb = {
            color.r / 255.0f,
            color.g / 255.0f,
            color.b / 255.0f
        };

        R3D_SetSurfaceEffectUniform(built.effect, "silhouetteColor", &rgb);
    }

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildScreen(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_SCREEN;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildOverlay(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_DEPTH_COMPARE |
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.renderPass = R3D_RENDER_PASS_OVERLAY;
    built.mode.depthCompare = R3D_COMPARE_ALWAYS;
    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ALPHA;
    built.kind = R3D_BUILT_MODE_OVERLAY;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildDepthOnly(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_DEPTH_WRITE;

    built.mode.renderPass = R3D_RENDER_PASS_DEPTH_ONLY;
    built.mode.depthWrite = true;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildDitheredTransparency(float coverage)
{
    static const char source[] =
        "uniform float coverage = 1.0;\n"
        "void fragment() {\n"
        "    int x = int(mod(floor(gl_FragCoord.x), 4.0));\n"
        "    int y = int(mod(floor(gl_FragCoord.y), 4.0));\n"
        "    const float bayer[16] = float[16](\n"
        "        0.0, 8.0, 2.0, 10.0,\n"
        "        12.0, 4.0, 14.0, 6.0,\n"
        "        3.0, 11.0, 1.0, 9.0,\n"
        "        15.0, 7.0, 13.0, 5.0);\n"
        "    if (coverage <= (bayer[x + y * 4] + 0.5) / 16.0)\n"
        "        discard;\n"
        "}\n";

    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask = R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY;
    built.mode.transparencyMode = R3D_TRANSPARENCY_OPAQUE;
    built.kind = R3D_BUILT_MODE_DITHERED;

    built.effect = R3D_LoadSurfaceEffectFromMemory(source);

    if (built.effect)
    {
        coverage = r3d_mode_clamp01(coverage);
        R3D_SetSurfaceEffectUniform(built.effect, "coverage", &coverage);
    }

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildStencilMask(uint8_t reference)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_STENCIL |
        R3D_RENDER_MODE_OVERRIDE_STENCIL_ENABLE |
        R3D_RENDER_MODE_OVERRIDE_STENCIL_WRITE |
        R3D_RENDER_MODE_OVERRIDE_DEPTH_COMPARE;

    built.mode.renderPass = R3D_RENDER_PASS_STENCIL_MASK;
    built.mode.depthCompare = R3D_COMPARE_ALWAYS;

    built.mode.stencilEnabled = true;
    built.mode.stencilWriteMask = 0xFF;

    built.mode.stencil.mode = R3D_COMPARE_ALWAYS;
    built.mode.stencil.ref = reference;
    built.mode.stencil.mask = 0xFF;
    built.mode.stencil.opFail = R3D_STENCIL_KEEP;
    built.mode.stencil.opZFail = R3D_STENCIL_KEEP;
    built.mode.stencil.opPass = R3D_STENCIL_REPLACE;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildStencilTest(uint8_t reference)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_STENCIL |
        R3D_RENDER_MODE_OVERRIDE_STENCIL_ENABLE |
        R3D_RENDER_MODE_OVERRIDE_STENCIL_WRITE;

    built.mode.renderPass = R3D_RENDER_PASS_STENCIL_TEST;
    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;

    built.mode.stencilEnabled = true;
    built.mode.stencilWriteMask = 0x00;

    built.mode.stencil.mode = R3D_COMPARE_EQUAL;
    built.mode.stencil.ref = reference;
    built.mode.stencil.mask = 0xFF;
    built.mode.stencil.opFail = R3D_STENCIL_KEEP;
    built.mode.stencil.opZFail = R3D_STENCIL_KEEP;
    built.mode.stencil.opPass = R3D_STENCIL_KEEP;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildOutline(Color color, float width, bool throughWalls)
{
    R3D_BuiltRenderMode built = {0};

    if (width < 1.0f) width = 1.0f;
    if (width > 12.0f) width = 12.0f;

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_PASS |
        R3D_RENDER_MODE_OVERRIDE_OUTLINE;

    built.mode.renderPass = R3D_RENDER_PASS_OUTLINE;
    built.mode.outlineColor = color;
    built.mode.outlineWidth = width;
    built.mode.outlineThroughWalls = throughWalls;
    built.kind = R3D_BUILT_MODE_OUTLINE;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildCustomBlend(
    R3D_CustomBlendState state)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_CUSTOM_BLEND |
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.customBlend = state;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildCustomStencil(
    R3D_StencilState state,
    bool enabled,
    uint8_t writeMask)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_STENCIL |
        R3D_RENDER_MODE_OVERRIDE_STENCIL_ENABLE |
        R3D_RENDER_MODE_OVERRIDE_STENCIL_WRITE;

    built.mode.stencil = state;
    built.mode.stencilEnabled = enabled;
    built.mode.stencilWriteMask = writeMask;

    return built;
}

static unsigned char r3d_mode_opacity_byte(float opacity)
{
    opacity = r3d_mode_clamp01(opacity);
    return (unsigned char)(opacity * 255.0f + 0.5f);
}

bool R3D_RenderModeSetColor(R3D_BuiltRenderMode* built, Color color)
{
    if (!built) return false;

    switch (built->kind)
    {
        case R3D_BUILT_MODE_XRAY:
        case R3D_BUILT_MODE_OCCLUDED_ONLY:
            color.a = built->mode.albedoColor.a;
            built->mode.albedoColor = color;
            return true;

        case R3D_BUILT_MODE_WIREFRAME:
        case R3D_BUILT_MODE_WIREFRAME_OVERLAY:
            color.a = built->mode.wireColor.a;
            built->mode.wireColor = color;
            return true;

        case R3D_BUILT_MODE_SILHOUETTE:
        {
            if (!built->effect) return false;

            color.a = built->mode.albedoColor.a;
            built->mode.albedoColor = color;

            Vector3 rgb = {
                color.r / 255.0f,
                color.g / 255.0f,
                color.b / 255.0f
            };

            R3D_SetSurfaceEffectUniform(built->effect, "silhouetteColor", &rgb);
            return true;
        }

        case R3D_BUILT_MODE_OUTLINE:
            color.a = built->mode.outlineColor.a;
            built->mode.outlineColor = color;
            return true;

        case R3D_BUILT_MODE_COLOR_REPLACE:
        case R3D_BUILT_MODE_COLOR_TINT:
        case R3D_BUILT_MODE_COLOR_MIX:
        case R3D_BUILT_MODE_COLOR_ADD:
        case R3D_BUILT_MODE_COLOR_SCREEN:
        case R3D_BUILT_MODE_EMISSION_COLOR:
        {
            if (!built->effect) return false;

            Vector3 linearColor = r3d_mode_color_linear(color);
            R3D_SetSurfaceEffectUniform(built->effect, "effectColor", &linearColor);

            return true;
        }

        case R3D_BUILT_MODE_ALBEDO_TEXTURE:
        case R3D_BUILT_MODE_EMISSION_TEXTURE:
        {
            if (!built->effect) return false;

            Vector3 linearTint = r3d_mode_color_linear(color);
            R3D_SetSurfaceEffectUniform(built->effect, "effectTint", &linearTint);

            return true;
        }
        case R3D_BUILT_MODE_RIM_GLOW:
        {
            if (!built->effect) return false;

            Vector3 linearColor = r3d_mode_color_linear(color);
            R3D_SetSurfaceEffectUniform(built->effect, "rimColor", &linearColor);

            return true;
        }

        case R3D_BUILT_MODE_OUTER_GLOW:
        case R3D_BUILT_MODE_DROP_SHADOW:
            built->silhouette.color = color;
            return true;

        default:
            return false;
    }
}

bool R3D_RenderModeSetCoverage(R3D_BuiltRenderMode* built, float coverage)
{
    if (!built || !built->effect ||
        built->kind != R3D_BUILT_MODE_DITHERED ||
        !isfinite(coverage))
    {
        return false;
    }

    coverage = r3d_mode_clamp01(coverage);
    R3D_SetSurfaceEffectUniform(built->effect, "coverage", &coverage);

    return true;
}

bool R3D_RenderModeSetOpacity(R3D_BuiltRenderMode* built, float opacity)
{
    if (!built || !isfinite(opacity)) return false;

    switch (built->kind)
    {
        case R3D_BUILT_MODE_XRAY:
        case R3D_BUILT_MODE_OCCLUDED_ONLY:
            built->mode.albedoColor.a = r3d_mode_opacity_byte(opacity);
            return true;

        case R3D_BUILT_MODE_SILHOUETTE:
        {
            built->mode.albedoColor.a = r3d_mode_opacity_byte(opacity);

            built->mode.transparencyMode =
                built->mode.albedoColor.a == 255
                    ? R3D_TRANSPARENCY_OPAQUE
                    : R3D_TRANSPARENCY_BLEND;

            return true;
        }

        case R3D_BUILT_MODE_ALPHA_BLEND:
        case R3D_BUILT_MODE_OVERLAY:
        {
            if (!built->effect)
            {
                static const char source[] =
                    "uniform float opacity = 1.0;\n"
                    "void fragment() {\n"
                    "    ALPHA *= opacity;\n"
                    "}\n";

                built->effect = R3D_LoadSurfaceEffectFromMemory(source);

                if (!built->effect) return false;
            }

            opacity = r3d_mode_clamp01(opacity);
            R3D_SetSurfaceEffectUniform(built->effect, "opacity", &opacity);

            return true;
        }

        case R3D_BUILT_MODE_FRESNEL_TRANSPARENCY:
        {
            if (!built->effect) return false;

            opacity = r3d_mode_clamp01(opacity);
            R3D_SetSurfaceEffectUniform(built->effect, "fresnelOpacity", &opacity);

            return true;
        }
        case R3D_BUILT_MODE_OUTER_GLOW:
        case R3D_BUILT_MODE_DROP_SHADOW:
            built->silhouette.opacity = r3d_mode_clamp01(opacity);
            return true;

        default:
            return false;
    }
}

bool R3D_RenderModeSetWidth(R3D_BuiltRenderMode* built, float width)
{
    if (!built ||
        built->kind != R3D_BUILT_MODE_OUTLINE ||
        !isfinite(width))
    {
        return false;
    }

    if (width < 1.0f) width = 1.0f;
    if (width > 12.0f) width = 12.0f;

    built->mode.outlineWidth = width;

    return true;
}

bool R3D_RenderModeSetThroughWalls(R3D_BuiltRenderMode* built, bool enabled)
{
    if (!built || built->kind != R3D_BUILT_MODE_OUTLINE)
        return false;

    built->mode.outlineThroughWalls = enabled;

    return true;
}