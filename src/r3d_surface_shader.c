/* r3d_surface_shader.c -- R3D Surface Shader Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_surface_shader.h>
#include <r3d_config.h>
#include <raylib.h>
#include <string.h>
#include <ctype.h>
#include <glad.h>

#include "./modules/r3d_shader.h"
#include "./modules/r3d_effect.h"
#include "./common/r3d_rshade.h"
#include "./common/r3d_helper.h"

// ========================================
// INTERNAL ENUMS
// ========================================

typedef uint32_t usage_hint_t;

#define USAGE_HINT_OPAQUE  (1 << 0)   //< Build: geometry
#define USAGE_HINT_BLEND   (1 << 2)   //< Build: forward
#define USAGE_HINT_UNLIT   (1 << 3)   //< Build: unlit
#define USAGE_HINT_SHADOW  (1 << 4)   //< Build: depth, depthCube
#define USAGE_HINT_DECAL   (1 << 5)   //< Build: decal
#define USAGE_HINT_PROBE   (1 << 6)   //< Build: probeForward, probeUnlit

// ========================================
// INTERNAL FUNCTIONS
// ========================================

/* Parse usage pragma to decide wich shader to pre-compile */
static usage_hint_t parse_pragma_usage(const char** ptr);

/* Returns a static string containing the list of usage hints */
static const char* get_usage_hint_string(usage_hint_t hints);

/* Compile all needed variants accordingly to the usage hints specified in pragma */
static bool compile_shader_variants(R3D_SurfaceShader* shader, usage_hint_t usage);

// ========================================
// PUBLIC API
// ========================================

R3D_SurfaceShader* R3D_LoadSurfaceShader(const char* filePath)
{
    char* code = LoadFileText(filePath);
    if (code == NULL)
    {
        R3D_TRACELOG(LOG_ERROR, "Failed to load surface shader; Unable to load shader file");
        return NULL;
    }

    R3D_SurfaceShader* shader = R3D_LoadSurfaceShaderFromMemory(code);
    UnloadFileText(code);

    return shader;
}

R3D_SurfaceShader* R3D_LoadSurfaceShaderFromMemory(const char* code)
{
    size_t userCodeLen = strlen(code);
    if (userCodeLen > R3D_MAX_SHADER_CODE_LENGTH)
    {
        R3D_TRACELOG(LOG_ERROR, "Failed to load surface shader; User code too long");
        return NULL;
    }

    R3D_SurfaceShader* shader = r3d_shader_custom_alloc();
    if (!shader)
    {
        R3D_TRACELOG(LOG_ERROR, "Bad alloc during surface shader loading");
        return NULL;
    }

    r3d_rshade_source_t parsed = {0};

    if (!r3d_rshade_parse_source(code, &parsed))
    {
        R3D_TRACELOG(LOG_ERROR, "Failed to load surface shader; Invalid Ray-Shade source");
        r3d_shader_custom_free(shader);
        return NULL;
    }

    memcpy(shader->data.samplers, parsed.samplers, sizeof(parsed.samplers));
    memcpy(&shader->data.uniforms, &parsed.uniforms, sizeof(parsed.uniforms));

    int uniformCount = parsed.uniformCount;
    int samplerCount = parsed.samplerCount;
    int varyingCount = parsed.varyingCount;
    int currentOffset = parsed.uniformSize;

    r3d_rshade_varying_t* varyings = parsed.varyings;
    r3d_rshade_parsed_function_t vertexFunc = parsed.vertex;
    r3d_rshade_parsed_function_t fragmentFunc = parsed.fragment;

    usage_hint_t usage = 0;

    // Usage pragmas are material-shader compilation hints.
    // Temporary effects may contain them, but do not need them.
    const char* ptr = code;

    while (*ptr)
    {
        r3d_rshade_skip_whitespace_and_comments(&ptr);

        if (!*ptr) break;

        if (strncmp(ptr, "#pragma", 7) == 0 &&
            isspace((unsigned char)ptr[7]))
        {
            usage |= parse_pragma_usage(&ptr);
        }
        else
        {
            r3d_rshade_skip_to_end_of_line(&ptr);
        }
    }

    /* --- PHASE 2: Generate transformed shader code --- */

    r3d_rshade_writer_t writer = {
        .ptr       = shader->program->userCode,
        .remaining = R3D_MAX_SHADER_CODE_LENGTH,
        .overflow  = false,
    };

    // Write uniform block and samplers
    r3d_rshade_write_uniform_block(&writer, shader->data.uniforms.entries, uniformCount);
    r3d_rshade_write_samplers(&writer, shader->data.samplers, samplerCount);

    // Copy global code (excluding pragma, comments, uniforms, varyings, entry points)
    r3d_rshade_copy_global_code(&writer, code, true, &vertexFunc, &fragmentFunc);

    // Write vertex stage section
    r3d_rshade_writer_printf(&writer, "\n#ifdef STAGE_VERT\n\n");
    r3d_rshade_write_varyings(&writer, "out", varyings, varyingCount);
    r3d_rshade_write_shader_function(&writer, "vertex", &vertexFunc);
    r3d_rshade_writer_printf(&writer, "\n#endif\n\n");

    // Write fragment stage section
    r3d_rshade_writer_printf(&writer, "#ifdef STAGE_FRAG\n\n");
    r3d_rshade_write_varyings(&writer, "in", varyings, varyingCount);
    r3d_rshade_write_shader_function(&writer, "fragment", &fragmentFunc);
    r3d_rshade_writer_printf(&writer, "\n#endif\n");

    if (writer.overflow)
    {
        R3D_TRACELOG(LOG_ERROR, "Failed to load surface shader; Transformed code too long");
        r3d_free(shader);
        return NULL;
    }

    *writer.ptr = '\0';

    /* --- PHASE 3: Pre-compile needed shader variants --- */

    if (!compile_shader_variants(shader, usage))
    {
        R3D_UnloadSurfaceShader(shader);
        return NULL;
    }

    /* --- PHASE 4: Initialize uniform buffer --- */

    r3d_shader_custom_init_uniforms(shader, currentOffset);

    R3D_TRACELOG(LOG_INFO, "Surface shader loaded successfully");
    R3D_TRACELOG(LOG_INFO, "    > Usage hints: %s", get_usage_hint_string(usage));
    R3D_TRACELOG(LOG_INFO, "    > Varying count: %i", varyingCount);
    R3D_TRACELOG(LOG_INFO, "    > Sampler count: %i", samplerCount);
    R3D_TRACELOG(LOG_INFO, "    > Uniform count: %i", uniformCount);

    return shader;
}

R3D_SurfaceShader* R3D_LoadSurfaceShaderAlias(R3D_SurfaceShader* shader)
{
    R3D_SurfaceShader* alias = r3d_shader_custom_clone(shader);
    if (!alias)
    {
        R3D_TRACELOG(LOG_ERROR, "Bad alloc during surface shader alias loading");
        return NULL;
    }
    return alias;
}

void R3D_UnloadSurfaceShader(R3D_SurfaceShader* shader)
{
    if (!shader) return;

    r3d_effect_invalidate_base(shader);
    r3d_shader_custom_free(shader);
}

void R3D_SetSurfaceShaderUniform(R3D_SurfaceShader* shader, const char* name, const void* value)
{
    if (!shader)
    {
        R3D_TRACELOG(LOG_WARNING, "Cannot set uniform '%s' on NULL surface shader", name);
        return;
    }

    if (!r3d_shader_custom_set_uniform(shader, name, value))
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to set custom uniform '%s'", name);
    }
}

void R3D_SetSurfaceShaderSampler(R3D_SurfaceShader* shader, const char* name, Texture texture)
{
    if (!shader)
    {
        R3D_TRACELOG(LOG_WARNING, "Cannot set sampler '%s' on NULL surface shader", name);
        return;
    }

    if (!r3d_shader_custom_set_sampler(shader, name, texture))
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to set custom sampler '%s'", name);
    }
}

// ========================================
// INTERNAL FUNCTIONS
// ========================================

usage_hint_t parse_pragma_usage(const char** ptr)
{
    usage_hint_t result = 0;

    // Skip "#pragma"
    *ptr += 7;  // strlen("#pragma")
    r3d_rshade_skip_whitespace(ptr);

    // Check for "usage" keyword
    if (strncmp(*ptr, "usage", 5) != 0 || !isspace((*ptr)[5]))
    {
        r3d_rshade_skip_to_end_of_line(ptr);
        return 0;
    }

    *ptr += 5;  // strlen("usage")
    r3d_rshade_skip_whitespace(ptr);

    // Parse usage hints until end of line (CR, LF, or CRLF)
    while (**ptr && **ptr != '\n' && **ptr != '\r')
    {
        r3d_rshade_skip_whitespace(ptr);
        if (!**ptr || **ptr == '\n' || **ptr == '\r') break;

        // Match usage hint keywords
        static const struct {
            const char* name;
            size_t len;
            usage_hint_t flag;
        } hints[] = {
            {"opaque", 6, USAGE_HINT_OPAQUE},
            {"blend",  5, USAGE_HINT_BLEND},
            {"unlit",  5, USAGE_HINT_UNLIT},
            {"shadow", 6, USAGE_HINT_SHADOW},
            {"decal",  5, USAGE_HINT_DECAL},
            {"probe",  5, USAGE_HINT_PROBE}
        };

        bool matched = false;
        for (int i = 0; i < (int)R3D_ARRAY_SIZE(hints); i++)
        {
            if (strncmp(*ptr, hints[i].name, hints[i].len) == 0 &&
                (isspace((*ptr)[hints[i].len]) ||
                 (*ptr)[hints[i].len] == '\n' ||
                 (*ptr)[hints[i].len] == '\r' ||
                 (*ptr)[hints[i].len] == '\0'))
            {
                result |= hints[i].flag;
                *ptr += hints[i].len;
                matched = true;
                break;
            }
        }

        if (!matched)
        {
            // Unknown hint, skip to next whitespace or end of line
            while (**ptr && !isspace(**ptr)) (*ptr)++;
        }
    }

    r3d_rshade_skip_to_end_of_line(ptr);
    return result;
}

const char* get_usage_hint_string(usage_hint_t hints)
{
    static char buffer[128];
    char *p = buffer;
    
    if (hints == 0)
    {
        return "None";
    }

    *p = '\0';
    
    #define APPEND(str) do { \
        if (p != buffer) { *p++ = ','; *p++ = ' '; } \
        const char *s = str; \
        while (*s) *p++ = *s++; \
    } while(0)

    if (hints & USAGE_HINT_OPAQUE) APPEND("Opaque");
    if (hints & USAGE_HINT_BLEND)  APPEND("Blend");
    if (hints & USAGE_HINT_UNLIT)  APPEND("Unlit");
    if (hints & USAGE_HINT_SHADOW) APPEND("Shadow");
    if (hints & USAGE_HINT_DECAL)  APPEND("Decal");
    if (hints & USAGE_HINT_PROBE)  APPEND("Probe");

    #undef APPEND
    *p = '\0';
    return buffer;
}

bool compile_shader_variants(R3D_SurfaceShader* shader, usage_hint_t usage)
{
    if (usage == 0)
    {
        bool ok = R3D_MOD_SHADER_LOADER.scene.geometry(shader);
        if (!ok)
        {
            R3D_TRACELOG(LOG_ERROR, "Failed to compile surface shader");
        }
        return ok;
    }

    const struct {
        const char* name;
        usage_hint_t condition;
        r3d_shader_loader_func func;
    } variants[] = {
        {"geometry",      USAGE_HINT_OPAQUE, R3D_MOD_SHADER_LOADER.scene.geometry},
        {"forward",       USAGE_HINT_BLEND , R3D_MOD_SHADER_LOADER.scene.forward},
        {"unlit",         USAGE_HINT_UNLIT,  R3D_MOD_SHADER_LOADER.scene.unlit},
        {"depth",         USAGE_HINT_SHADOW, R3D_MOD_SHADER_LOADER.scene.depth},
        {"depth-cube",    USAGE_HINT_SHADOW, R3D_MOD_SHADER_LOADER.scene.depthCube},
        {"decal",         USAGE_HINT_DECAL,  R3D_MOD_SHADER_LOADER.scene.decal},
        {"probe-forward", USAGE_HINT_PROBE,  R3D_MOD_SHADER_LOADER.scene.probeForward},
        {"probe-unlit",   USAGE_HINT_PROBE,  R3D_MOD_SHADER_LOADER.scene.probeUnlit},
    };

    for (int i = 0; i < (int)R3D_ARRAY_SIZE(variants); i++)
    {
        if (R3D_BIT_ANY(usage, variants[i].condition))
        {
            if (!variants[i].func(shader))
            {
                R3D_TRACELOG(LOG_ERROR, "Failed to compile surface shader (variant: '%s')", variants[i].name);
                return false;
            }
        }
    }

    return true;
}
