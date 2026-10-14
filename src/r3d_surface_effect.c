/* r3d_surface_effect.c -- Temporary surface effect resources and snapshots. */

#include <r3d/r3d_surface_effect.h>
#include <r3d_config.h>
#include <raylib.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "./modules/r3d_effect.h"
#include "./common/r3d_helper.h"
#include "./common/r3d_list.h"

static r3d_list_t* activeEffects = NULL;
static r3d_list_t* capturedEffects = NULL;
static r3d_list_t* parameterBytes = NULL;

R3D_SurfaceEffect* R3D_LoadSurfaceEffect(const char* filePath)
{
    if (!filePath) return NULL;

    char* source = LoadFileText(filePath);
    if (!source) return NULL;

    R3D_SurfaceEffect* effect = R3D_LoadSurfaceEffectFromMemory(source);

    UnloadFileText(source);
    return effect;
}

R3D_SurfaceEffect* R3D_LoadSurfaceEffectFromMemory(const char* code)
{
    if (!code) return NULL;

    size_t length = strlen(code);
    if (length == 0 || length >= R3D_MAX_SHADER_CODE_LENGTH) return NULL;

    R3D_SurfaceEffect* effect = r3d_zalloc(sizeof(*effect));
    memset(effect, 0, sizeof(*effect));

    effect->source = r3d_zalloc(length + 1);
    memcpy(effect->source, code, length + 1);

    const char* ptr = effect->source;
    r3d_rshade_sampler_t samplers[R3D_MAX_SHADER_SAMPLERS] = {0};

    int samplerCount = 0;
    int uniformCount = 0;
    int currentOffset = 0;

    while (*ptr)
    {
        r3d_rshade_skip_whitespace_and_comments(&ptr);
        if (!*ptr) break;

        if (r3d_rshade_match_keyword(ptr, "uniform", 7))
        {
            ptr += 7;

            r3d_rshade_parse_uniform(
                &ptr,
                samplers,
                &effect->uniforms,
                &samplerCount,
                &uniformCount,
                &currentOffset,
                R3D_MAX_SHADER_SAMPLERS,
                R3D_MAX_SHADER_UNIFORMS
            );

            continue;
        }

        r3d_rshade_parsed_function_t* entry =
            r3d_rshade_check_shader_entry(
                ptr,
                &effect->vertex,
                &effect->fragment
            );

        if (entry && !entry->bodyStart)
        {
            r3d_rshade_skip_to_brace(&ptr);

            if (*ptr == '{')
            {
                entry->bodyStart = ptr;
                r3d_rshade_skip_to_matching_brace(&ptr);
                entry->bodyEnd = ptr;
                continue;
            }
        }

        /* Other declarations remain in the original source.
         * Shader source validation and composition come next. */
        ptr++;
    }

    if (!effect->vertex.bodyStart && !effect->fragment.bodyStart)
    {
        TraceLog(LOG_WARNING, "R3D: Surface effect has no vertex or fragment hook");
        R3D_UnloadSurfaceEffect(effect);
        return NULL;
    }

    effect->uniformCount = uniformCount;
    effect->uniforms.bufferSize = currentOffset;

    return effect;
}

void R3D_UnloadSurfaceEffect(R3D_SurfaceEffect* effect)
{
    if (!effect) return;

    r3d_free(effect->source);
    r3d_free(effect);
}

void R3D_SetSurfaceEffectUniform(R3D_SurfaceEffect* effect, const char* name, const void* value)
{
    if (!effect || !name || !value) return;

    for (int i = 0; i < effect->uniformCount; i++)
    {
        const r3d_rshade_uniform_t* uniform = &effect->uniforms.entries[i];

        if (strcmp(uniform->name, name) == 0)
        {
            memcpy(
                effect->uniforms.buffer + uniform->offset,
                value,
                (size_t)uniform->size
            );

            return;
        }
    }

    TraceLog(LOG_WARNING, "R3D: Unknown surface effect uniform '%s'", name);
}

void R3D_PushSurfaceEffect(R3D_SurfaceEffect* effect)
{
    r3d_effect_push(effect);
}

void R3D_PopSurfaceEffect(void)
{
    r3d_effect_pop();
}

void r3d_effect_init(void)
{
    activeEffects = R3D_LIST_CREATE(r3d_effect_snapshot_t, 8);
    capturedEffects = R3D_LIST_CREATE(r3d_effect_snapshot_t, 64);
    parameterBytes = R3D_LIST_CREATE(uint8_t, 1024);
}

void r3d_effect_quit(void)
{
    if (activeEffects) R3D_LIST_DESTROY(activeEffects);
    if (capturedEffects) R3D_LIST_DESTROY(capturedEffects);
    if (parameterBytes) R3D_LIST_DESTROY(parameterBytes);

    activeEffects = NULL;
    capturedEffects = NULL;
    parameterBytes = NULL;
}

void r3d_effect_clear(void)
{
    if (activeEffects) R3D_LIST_CLEAR(activeEffects);
    if (capturedEffects) R3D_LIST_CLEAR(capturedEffects);
    if (parameterBytes) R3D_LIST_CLEAR(parameterBytes);
}

bool r3d_effect_has_active(void)
{
    return activeEffects && !R3D_LIST_EMPTY(activeEffects);
}

void r3d_effect_push(R3D_SurfaceEffect* effect)
{
    if (!effect || !activeEffects || !parameterBytes) return;

    size_t offset = R3D_LIST_LENGTH(parameterBytes);
    size_t size = (size_t)effect->uniforms.bufferSize;

    if (offset > UINT32_MAX || size > UINT32_MAX - offset)
    {
        TraceLog(LOG_ERROR, "R3D: Surface effect parameter storage limit reached");
        return;
    }

    r3d_effect_snapshot_t snapshot = {
        .effect = effect,
        .parameterOffset = (uint32_t)offset,
        .parameterSize = (uint32_t)size
    };

    if (size > 0)
    {
        R3D_LIST_PUSH_ALL(
            parameterBytes,
            effect->uniforms.buffer,
            size
        );
    }

    R3D_LIST_PUSH(activeEffects, snapshot);
}

void r3d_effect_pop(void)
{
    if (!r3d_effect_has_active())
    {
        TraceLog(LOG_WARNING, "R3D: Surface effect stack underflow");
        return;
    }

    R3D_LIST_POP(activeEffects, NULL);
}

void r3d_effect_capture(int* first, int* count)
{
    if (!first || !count) return;

    *first = 0;
    *count = 0;

    if (!r3d_effect_has_active() || !capturedEffects) return;

    size_t activeCount = R3D_LIST_LENGTH(activeEffects);
    size_t capturedCount = R3D_LIST_LENGTH(capturedEffects);

    if (activeCount > INT_MAX ||
        capturedCount > (size_t)INT_MAX - activeCount)
    {
        TraceLog(LOG_ERROR, "R3D: Surface effect snapshot limit reached");
        return;
    }

    *first = (int)capturedCount;
    *count = (int)activeCount;

    R3D_LIST_PUSH_ALL(
        capturedEffects,
        activeEffects->elements,
        activeCount
    );
}