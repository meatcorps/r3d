/* r3d_effect.h -- Internal surface effect state. */

#ifndef R3D_MODULE_EFFECT_H
#define R3D_MODULE_EFFECT_H

#include <r3d/r3d_surface_effect.h>
#include <r3d/r3d_surface_shader.h>
#include <r3d_config.h>
#include <stdint.h>

#include "../common/r3d_rshade.h"

#define R3D_EFFECT_MAX_SYMBOLS 64

/* Loaded Ray-Shade effect resource. */
struct R3D_SurfaceEffect {
    char* source;
    char* globals;

    r3d_rshade_uniform_buffer_t uniforms;
    r3d_rshade_sampler_t samplers[R3D_MAX_SHADER_SAMPLERS];
    r3d_rshade_varying_t varyings[R3D_RSHADE_MAX_VARYINGS];

    int uniformCount;
    int samplerCount;
    int varyingCount;

    r3d_rshade_parsed_function_t vertex;
    r3d_rshade_parsed_function_t fragment;

    /* Effect-owned globals, helpers, types, and macros. */
    char symbols[R3D_EFFECT_MAX_SYMBOLS][R3D_RSHADE_MAX_VAR_NAME_LENGTH];
    int symbolCount;
};

/* Immutable reference to parameters copied into the frame's byte storage. */
typedef struct r3d_effect_snapshot {
    R3D_SurfaceEffect* effect;
    uint32_t parameterOffset;
    uint32_t parameterSize;
    uint32_t samplerOffset;

    // Only used by the first snapshot in a captured scope.
    uint32_t frameOffset;
    uint32_t frameSize;
} r3d_effect_snapshot_t;

void r3d_effect_init(void);
void r3d_effect_quit(void);
void r3d_effect_clear(void);

void r3d_effect_push(R3D_SurfaceEffect* effect);
void r3d_effect_pop(void);
bool r3d_effect_has_active(void);

void r3d_effect_capture(int* first, int* count);
void r3d_effect_upload_frame(void);

R3D_SurfaceShader* r3d_effect_resolve_shader(R3D_SurfaceShader* base, int first, int count);
void r3d_effect_bind_shader(R3D_SurfaceShader* shader, R3D_SurfaceShader* base, int first, int count, unsigned int program);
void r3d_effect_invalidate_resource(R3D_SurfaceEffect* effect);
void r3d_effect_invalidate_base(R3D_SurfaceShader* shader);

#endif // R3D_MODULE_EFFECT_H