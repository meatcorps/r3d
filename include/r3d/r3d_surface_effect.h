/* r3d_surface_effect.h -- Temporary surface effect module. */

#ifndef R3D_SURFACE_EFFECT_H
#define R3D_SURFACE_EFFECT_H

#include "./r3d_platform.h"

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

    /* Push captures the effect's current parameter values. */
    R3DAPI void R3D_PushSurfaceEffect(R3D_SurfaceEffect* effect);

    /* Pop restores the previously active effect stack. */
    R3DAPI void R3D_PopSurfaceEffect(void);

#ifdef __cplusplus
}
#endif

#endif // R3D_SURFACE_EFFECT_H