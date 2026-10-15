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
    typedef struct R3D_BuiltRenderMode {
        R3D_RenderMode mode;
        R3D_SurfaceEffect* effect;
    } R3D_BuiltRenderMode;

    R3DAPI void R3D_PushBuiltRenderMode(R3D_BuiltRenderMode built);
    R3DAPI void R3D_PopBuiltRenderMode(void);
    R3DAPI void R3D_UnloadBuiltRenderMode(R3D_BuiltRenderMode* built);

    /* Built-in state-based modes */
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildDefault(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildUnlit(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildAlphaBlend(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildAdditive(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildMultiply(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildDoubleSided(void);
    R3DAPI R3D_BuiltRenderMode R3D_RenderModeBuildShadowOnly(void);

#ifdef __cplusplus
}
#endif

#endif