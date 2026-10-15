#ifndef R3D_RENDER_MODE_BUILDER_H
#define R3D_RENDER_MODE_BUILDER_H

#include "./r3d_draw.h"
#include "./r3d_surface_effect.h"

#ifdef __cplusplus
extern "C" {
#endif

    /*
     * Built-in render mode resource.
     *
     * The mode contains temporary pipeline overrides and may own
     * an optional Ray-Shade surface effect.
     *
     * Build once, Push/Pop during drawing, Unload when finished.
     * Treat the returned value as a unique owner; do not unload copies.
     */
    typedef enum R3D_BuiltRenderModeKind {
        R3D_BUILT_MODE_GENERIC,
        R3D_BUILT_MODE_ALPHA_BLEND,
        R3D_BUILT_MODE_OVERLAY,
        R3D_BUILT_MODE_XRAY,
        R3D_BUILT_MODE_OCCLUDED_ONLY,
        R3D_BUILT_MODE_WIREFRAME,
        R3D_BUILT_MODE_WIREFRAME_OVERLAY,
        R3D_BUILT_MODE_SILHOUETTE,
        R3D_BUILT_MODE_DITHERED,
        R3D_BUILT_MODE_OUTLINE,
        R3D_BUILT_MODE_COLOR_REPLACE,
        R3D_BUILT_MODE_COLOR_TINT,
        R3D_BUILT_MODE_COLOR_MIX,
        R3D_BUILT_MODE_COLOR_ADD,
        R3D_BUILT_MODE_COLOR_SCREEN,
        R3D_BUILT_MODE_EMISSION_COLOR,
        R3D_BUILT_MODE_ALBEDO_TEXTURE,
        R3D_BUILT_MODE_EMISSION_TEXTURE,
        R3D_BUILT_MODE_RIM_GLOW,
        R3D_BUILT_MODE_FRESNEL_TRANSPARENCY,
        R3D_BUILT_MODE_OUTER_GLOW,
        R3D_BUILT_MODE_DROP_SHADOW,
        R3D_BUILT_MODE_HOLOGRAM,
        R3D_BUILT_MODE_DISSOLVE,
        R3D_BUILT_MODE_EMISSION_PULSE,
        R3D_BUILT_MODE_GLITCH,
        R3D_BUILT_MODE_UV_SCROLL,
        R3D_BUILT_MODE_UV_DISTORTION,
        R3D_BUILT_MODE_GRADIENT,
        R3D_BUILT_MODE_PIXELATE,
        R3D_BUILT_MODE_DITHER_FADE,
        R3D_BUILT_MODE_SLICE_DISPLACE,
        R3D_BUILT_MODE_CUT_CLIP
    } R3D_BuiltRenderModeKind;

    /*
     * Texture-to-surface blending, not framebuffer blending.
     * REPLACE ignores blendStrength and fully overrides the target RGB.
     * Other modes use blendStrength and source texture alpha.
     */
    typedef enum R3D_TextureBlendMode {
        R3D_TEXTURE_BLEND_REPLACE,
        R3D_TEXTURE_BLEND_MULTIPLY,
        R3D_TEXTURE_BLEND_MIX,
        R3D_TEXTURE_BLEND_ADD,
        R3D_TEXTURE_BLEND_SCREEN
    } R3D_TextureBlendMode;

    /*
     * Frame-local screen-space silhouette appearance.
     * Parameters are copied at PushBuiltRenderMode time.
     */
    typedef struct R3D_SilhouetteStyle {
        Color color;
        Vector2 offset;
        float radius;
        float spread;
        float intensity;
        float opacity;
    } R3D_SilhouetteStyle;

    typedef struct R3D_BuiltRenderMode {
        R3D_RenderMode mode;
        R3D_SurfaceEffect* effect;
        R3D_BuiltRenderModeKind kind;
        R3D_SilhouetteStyle silhouette;
    } R3D_BuiltRenderMode;

    R3DAPI void R3D_PushBuiltRenderMode(R3D_BuiltRenderMode built);
    R3DAPI void R3D_PopBuiltRenderMode(void);
    R3DAPI void R3D_UnloadBuiltRenderMode(R3D_BuiltRenderMode* built);
    R3DAPI bool R3D_RenderModeSetColor(R3D_BuiltRenderMode* built, Color color);
    R3DAPI bool R3D_RenderModeSetCoverage(R3D_BuiltRenderMode* built, float coverage);
    R3DAPI bool R3D_RenderModeSetOpacity(R3D_BuiltRenderMode* built, float opacity);
    R3DAPI bool R3D_RenderModeSetWidth(R3D_BuiltRenderMode* built, float width);
    R3DAPI bool R3D_RenderModeSetThroughWalls(R3D_BuiltRenderMode* built, bool enabled);
    R3DAPI bool R3D_RenderModeSetProgress(R3D_BuiltRenderMode* built, float progress);
    R3DAPI bool R3D_RenderModeSetStrength(R3D_BuiltRenderMode* built, float strength);
    R3DAPI bool R3D_RenderModeSetCutDistance(R3D_BuiltRenderMode* built, float distance);

    /* Color and appearance: surface operations before lighting. */
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildColorReplace(Color color);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildColorMultiply(Color color);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildColorTint(Color color);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildColorMix(Color color, float strength);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildColorAdd(Color color);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildColorScreen(Color color);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildEmissionColor(Color color, float intensity);

    /*
     * Temporary texture layers.
     * The texture is borrowed, not owned by the built mode.
     * Color textures should be loaded in R3D's configured color space.
     *
     * blendStrength: 0..1, ignored by REPLACE.
     * hdrIntensity: >=0, values above 1 are supported.
     * Texture alpha masks non-REPLACE blend operations.
     * Neither function changes material opacity.
     */
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildAlbedoTexture(Texture2D texture, R3D_TextureBlendMode blendMode, Color tint, float blendStrength);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildEmissionTexture(Texture2D texture, R3D_TextureBlendMode blendMode, Color tint, float blendStrength, float hdrIntensity);

    /*
     * View-dependent appearance effects.
     *
     * Rim Glow adds HDR emission to lit surfaces.
     * intensity >= 0, with values above 1 supported.
     * power controls the sharpness of the rim.
     *
     * Fresnel Transparency uses front/edge opacity in the range 0..1.
     * The source material's alpha is preserved as a multiplier.
     * Uses forward alpha blending without depth writing or shadow casting.
     */
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildRimGlow(Color color, float intensity, float power);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildFresnelTransparency(float frontOpacity, float edgeOpacity, float power);
    /* Screen-space silhouette effects. Parameters use internal pixels. */
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildOuterGlow(Color color, float radius, float spread, float intensity);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildDropShadow(Color color, Vector2 offset, float blurRadius, float spread, float opacity);

    /*
     * Animated Color and Appearance effects.
     *
     * Hologram:
     *   opacity: 0..1
     *   scanlineDensity: lines per local mesh unit
     *   scrollSpeed: scanline movement per second
     *   hdrIntensity: >= 0
     *
     * Dissolve:
     *   progress: 0 = intact, 1 = completely removed
     *   noiseScale: procedural noise frequency in local space
     *   edgeWidth: glowing transition band, 0..1
     *   edgeIntensity: HDR intensity, >= 0
     *
     * Emission Pulse:
     *   intensity: peak HDR emission, >= 0
     *   frequency: pulses per second
     *
     * Glitch:
     *   amplitude: maximum local-space vertex displacement
     *   frequency: pattern updates per second
     *   stripeDensity: horizontal bands per local mesh unit
     */
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildHologram(Color color, float opacity, float scanlineDensity, float scrollSpeed, float hdrIntensity);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildDissolve(Color edgeColor, float progress, float noiseScale, float edgeWidth, float edgeIntensity);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildEmissionPulse(Color color, float minIntensity, float maxIntensity, float frequency);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildGlitch(float amplitude, float frequency, float stripeDensity);
    /*
     * Phase 5: UV and procedural surface effects.
     *
     * UV Scroll:
     *   speed is UV units per second.
     *
     * UV Distortion:
     *   amplitude is UV displacement.
     *   frequency is the number of waves across UV space.
     *   speed controls animation.
     *
     * Gradient:
     *   start/end use local mesh coordinates projected onto axis.
     *   strength 0 = original albedo, 1 = gradient colors.
     *
     * Pixelate:
     *   uvCells is the number of UV blocks per UV unit.
     *   colorLevels >= 2 enables color quantization; 0 disables it.
     *
     * Dither Fade:
     *   progress 0 = visible, 1 = invisible.
     *
     * Displaced Slice:
     *   bandsPerUnit defines horizontal slice density.
     *   displacement is measured in local mesh units.
     *   gap is a fraction of each slice's height.
     *
     * CutClip:
     *   Keeps the positive side of a plane in local mesh space.
     *   Does not generate geometry to cap the cut.
     */
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildUVScroll(Vector2 speed);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildUVDistortion(float amplitude, float frequency, float speed);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildGradient(Color colorA, Color colorB, Vector3 axis, float start, float end, float strength);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildPixelate(float uvCells, float colorLevels);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildDitherFade(float progress, float pixelScale);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildSliceDisplace(float bandsPerUnit, float displacement, float gap, float speed);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildCutClip(Vector3 normal, float distance, Color edgeColor, float edgeWidth, float edgeIntensity);

    /* Built-in state-based modes */
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildDefault(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildUnlit(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildAlphaBlend(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildAdditive(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildMultiply(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildDoubleSided(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildShadowOnly(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildXRay(Color color, float opacity);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildOccludedOnly(Color color, float opacity);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildWireframe(Color color);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildWireframeOverlay(Color color);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildSilhouette(Color color);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildScreen(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildOverlay(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildDepthOnly(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildDitheredTransparency(float coverage);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildStencilMask(uint8_t reference);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildStencilTest(uint8_t reference);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildCustomBlend(R3D_CustomBlendState state);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildCustomStencil(R3D_StencilState state, bool enabled, uint8_t writeMask);
    // Width is in internal-render-resolution pixels, clamped to 1..12.
    // throughWalls controls whether occluding geometry clips the outline.
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildOutline(Color color, float width, bool throughWalls);

#ifdef __cplusplus
}
#endif

#endif