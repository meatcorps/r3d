/* r3d_surface_effect.h -- Temporary surface effect module. */

#ifndef R3D_SURFACE_EFFECT_H
#define R3D_SURFACE_EFFECT_H

#include "./r3d_platform.h"
#include <raylib.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct R3D_SurfaceEffect R3D_SurfaceEffect;

    /* Load a reusable surface effect definition. */
    R3DAPI R3D_SurfaceEffect* R3D_LoadSurfaceEffect(const char* filePath);
    R3DAPI R3D_SurfaceEffect* R3D_LoadSurfaceEffectFromMemory(const char* code);
    R3DAPI void R3D_UnloadSurfaceEffect(R3D_SurfaceEffect* effect);

    /* Change the default value used by subsequent Push calls. */
    R3DAPI void R3D_SetSurfaceEffectUniform(R3D_SurfaceEffect* effect, const char* name, const void* value);
    /* Sets the texture captured by subsequent Push calls. */
    R3DAPI void R3D_SetSurfaceEffectSampler(R3D_SurfaceEffect* effect, const char* name, Texture texture);

    /* Push captures the effect's current parameter values. */
    R3DAPI void R3D_PushSurfaceEffect(R3D_SurfaceEffect* effect);

    /* Pop restores the previously active effect stack. */
    R3DAPI void R3D_PopSurfaceEffect(void);

    /*
     * Scoped multiplicative tint for subsequent draw submissions.
     * Nesting composes tints in push order.
     * Push/Pop must be balanced inside an R3D rendering session.
     */
    R3DAPI void R3D_PushColor(Color color);
    R3DAPI void R3D_PopColor(void);

#ifdef __cplusplus
}
#endif

#endif // R3D_SURFACE_EFFECT_H