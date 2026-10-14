/* r3d_effect.h -- Internal surface effect state. */

#ifndef R3D_MODULE_EFFECT_H
#define R3D_MODULE_EFFECT_H

#include <r3d/r3d_surface_effect.h>
#include <r3d_config.h>
#include <stdint.h>

#include "../common/r3d_rshade.h"

/* Loaded effect resource. */
struct R3D_SurfaceEffect {
    char* source;

    r3d_rshade_uniform_buffer_t uniforms;
    int uniformCount;

    r3d_rshade_parsed_function_t vertex;
    r3d_rshade_parsed_function_t fragment;
};

/* Immutable reference to parameters copied into the frame's byte storage. */
typedef struct r3d_effect_snapshot {
    R3D_SurfaceEffect* effect;
    uint32_t parameterOffset;
    uint32_t parameterSize;
} r3d_effect_snapshot_t;

void r3d_effect_init(void);
void r3d_effect_quit(void);
void r3d_effect_clear(void);

void r3d_effect_push(R3D_SurfaceEffect* effect);
void r3d_effect_pop(void);
bool r3d_effect_has_active(void);

void r3d_effect_capture(int* first, int* count);

#endif // R3D_MODULE_EFFECT_H