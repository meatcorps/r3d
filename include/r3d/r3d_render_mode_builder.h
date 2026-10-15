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
        R3D_BUILT_MODE_OUTLINE
    } R3D_BuiltRenderModeKind;

    typedef struct R3D_BuiltRenderMode {
        R3D_RenderMode mode;
        R3D_SurfaceEffect* effect;
        R3D_BuiltRenderModeKind kind;
    } R3D_BuiltRenderMode;

    R3DAPI void R3D_PushBuiltRenderMode(R3D_BuiltRenderMode built);
    R3DAPI void R3D_PopBuiltRenderMode(void);
    R3DAPI void R3D_UnloadBuiltRenderMode(R3D_BuiltRenderMode* built);
    R3DAPI bool R3D_RenderModeSetColor(R3D_BuiltRenderMode* built, Color color);
    R3DAPI bool R3D_RenderModeSetCoverage(R3D_BuiltRenderMode* built, float coverage);
    R3DAPI bool R3D_RenderModeSetOpacity(R3D_BuiltRenderMode* built, float opacity);
    R3DAPI bool R3D_RenderModeSetWidth(R3D_BuiltRenderMode* built, float width);
    R3DAPI bool R3D_RenderModeSetThroughWalls(R3D_BuiltRenderMode* built, bool enabled);

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