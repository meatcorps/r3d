/* r3d_surface_effect.c -- Temporary surface effect resources and snapshots. */

#include <r3d/r3d_surface_effect.h>
#include <r3d_config.h>
#include <raylib.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "./modules/r3d_effect.h"
#include "./modules/r3d_shader.h"
#include "./common/r3d_helper.h"
#include "./common/r3d_list.h"

#define R3D_EFFECT_BLOCK_SLOT 7
#define R3D_EFFECT_BLOCK_LIMIT 16384

typedef struct {
    const r3d_shader_custom_program_t* baseProgram;
    R3D_SurfaceEffect** effects;
    R3D_SurfaceShader* shader;
    int count;
    int uniformSize;
    GLuint boundPrograms[8];
    bool boundProgramHasBlock[8];
    int boundProgramCount;
} r3d_effect_variant_t;

static r3d_list_t* effectVariants = NULL;
static GLuint effectUniformBuffer = 0;

static r3d_list_t* activeEffects = NULL;
static r3d_list_t* capturedEffects = NULL;
static r3d_list_t* parameterBytes = NULL;
static r3d_list_t* samplerTextures = NULL;

static r3d_list_t* effectPackedBytes = NULL;

static bool captureCacheValid = false;
static int captureCacheFirst = 0;
static int captureCacheCount = 0;

static GLint effectBufferAlignment = 1;

static bool effectRangeBound = false;
static uint32_t effectBoundOffset = 0;
static uint32_t effectBoundSize = 0;

static bool effect_identifier_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static bool effect_identifier_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static bool effect_add_symbol(R3D_SurfaceEffect* effect, const char* start, size_t length)
{
    if (length == 0 || length >= R3D_RSHADE_MAX_VAR_NAME_LENGTH)
        return false;

    for (int i = 0; i < effect->symbolCount; i++)
    {
        if (strlen(effect->symbols[i]) == length &&
            strncmp(effect->symbols[i], start, length) == 0)
        {
            return true;
        }
    }

    if (effect->symbolCount >= R3D_EFFECT_MAX_SYMBOLS)
        return false;

    char* destination = effect->symbols[effect->symbolCount++];
    memcpy(destination, start, length);
    destination[length] = '\0';

    return true;
}

static bool effect_owns_identifier(const R3D_SurfaceEffect* effect, const char* start, size_t length)
{
    for (int i = 0; i < effect->uniformCount; i++)
    {
        const char* name = effect->uniforms.entries[i].name;

        if (strlen(name) == length && strncmp(name, start, length) == 0)
            return true;
    }

    for (int i = 0; i < effect->samplerCount; i++)
    {
        const char* name = effect->samplers[i].name;

        if (strlen(name) == length && strncmp(name, start, length) == 0)
            return true;
    }

    for (int i = 0; i < effect->varyingCount; i++)
    {
        const char* name = effect->varyings[i].name;

        if (strlen(name) == length && strncmp(name, start, length) == 0)
            return true;
    }

    for (int i = 0; i < effect->symbolCount; i++)
    {
        const char* name = effect->symbols[i];

        if (strlen(name) == length && strncmp(name, start, length) == 0)
            return true;
    }

    return false;
}

static void effect_reset_declaration(char* previous, char* current, bool* assignment, bool* function)
{
    previous[0] = '\0';
    current[0] = '\0';
    *assignment = false;
    *function = false;
}

/*
 * Collect effect-owned top-level identifiers.
 *
 * Ray-Shade's uniforms, samplers, and varyings are already parsed
 * separately. This discovers helper functions, global variables,
 * struct type names, and preprocessor definitions.
 */
static bool effect_collect_symbols(R3D_SurfaceEffect* effect)
{
    const char* ptr = effect->globals;
    int braces = 0;
    int parentheses = 0;
    bool assignment = false;
    bool function = false;
    bool expectStruct = false;

    char previous[R3D_RSHADE_MAX_VAR_NAME_LENGTH] = {0};
    char current[R3D_RSHADE_MAX_VAR_NAME_LENGTH] = {0};

    while (*ptr)
    {
        r3d_rshade_skip_whitespace_and_comments(&ptr);
        if (!*ptr) break;

        if (*ptr == '#')
        {
            ptr++;
            r3d_rshade_skip_whitespace(&ptr);

            if (strncmp(ptr, "define", 6) == 0 && !effect_identifier_char(ptr[6]))
            {
                ptr += 6;
                r3d_rshade_skip_whitespace(&ptr);

                const char* start = ptr;

                while (effect_identifier_char(*ptr)) ptr++;

                if (!effect_add_symbol(effect, start, (size_t)(ptr - start)))
                    return false;
            }

            r3d_rshade_skip_to_end_of_line(&ptr);
            continue;
        }

        if (effect_identifier_start(*ptr))
        {
            const char* start = ptr;
            while (effect_identifier_char(*ptr)) ptr++;

            size_t length = (size_t)(ptr - start);

            if (braces == 0 && parentheses == 0)
            {
                if (expectStruct)
                {
                    if (!effect_add_symbol(effect, start, length))
                        return false;

                    expectStruct = false;
                }

                if (length == 6 && strncmp(start, "struct", 6) == 0)
                    expectStruct = true;

                memcpy(previous, current, sizeof(previous));

                if (length < sizeof(current))
                {
                    memcpy(current, start, length);
                    current[length] = '\0';
                }
                else
                {
                    return false;
                }
            }

            continue;
        }

        if (*ptr == '(')
        {
            if (braces == 0 && parentheses == 0 && !assignment &&
                previous[0] != '\0' && current[0] != '\0' &&
                strcmp(current, "layout") != 0)
            {
                if (!effect_add_symbol(effect, current, strlen(current)))
                    return false;

                function = true;
            }

            parentheses++;
        }
        else if (*ptr == ')' && parentheses > 0)
        {
            parentheses--;
        }
        else if (*ptr == '=' && braces == 0 && parentheses == 0)
        {
            if (!assignment && !function && previous[0] && current[0])
            {
                if (!effect_add_symbol(effect, current, strlen(current)))
                    return false;
            }

            assignment = true;
        }
        else if (*ptr == '{')
        {
            braces++;
        }
        else if (*ptr == '}')
        {
            if (braces > 0) braces--;

            if (braces == 0)
                effect_reset_declaration(previous, current, &assignment, &function);
        }
        else if (*ptr == ';' && braces == 0 && parentheses == 0)
        {
            if (!assignment && !function && previous[0] && current[0])
            {
                if (!effect_add_symbol(effect, current, strlen(current)))
                    return false;
            }

            effect_reset_declaration(previous, current, &assignment, &function);
        }

        ptr++;
    }

    return true;
}

/*
 * Copies GLSL while renaming effect-owned identifiers.
 *
 * Comments and strings remain untouched. Struct member access is
 * also left alone; declarations belonging to the effect are renamed
 * consistently across its global code and both entry functions.
 */
static void effect_write_renamed(r3d_rshade_writer_t* writer, const R3D_SurfaceEffect* effect, int slot, const char* source, size_t length)
{
    size_t i = 0;
    char previousSignificant = '\0';

    while (i < length && !writer->overflow)
    {
        if (i + 1 < length && source[i] == '/' && source[i + 1] == '/')
        {
            size_t start = i;
            i += 2;

            while (i < length && source[i] != '\n') i++;

            r3d_rshade_writer_write(writer, source + start, i - start);
            continue;
        }

        if (i + 1 < length && source[i] == '/' && source[i + 1] == '*')
        {
            size_t start = i;
            i += 2;

            while (i + 1 < length &&
                   !(source[i] == '*' && source[i + 1] == '/'))
            {
                i++;
            }

            if (i + 1 < length) i += 2;
            else i = length;

            r3d_rshade_writer_write(writer, source + start, i - start);
            continue;
        }

        if (source[i] == '"' || source[i] == '\'')
        {
            char quote = source[i];
            size_t start = i++;

            while (i < length)
            {
                if (source[i] == '\\' && i + 1 < length)
                {
                    i += 2;
                    continue;
                }

                if (source[i++] == quote) break;
            }

            r3d_rshade_writer_write(writer, source + start, i - start);
            continue;
        }

        if (effect_identifier_start(source[i]))
        {
            size_t start = i++;

            while (i < length && effect_identifier_char(source[i]))
                i++;

            size_t wordLength = i - start;

            if (previousSignificant != '.' &&
                effect_owns_identifier(effect, source + start, wordLength))
            {
                r3d_rshade_writer_printf(writer, "r3dFx%d_", slot);
            }

            r3d_rshade_writer_write(writer, source + start, wordLength);
            previousSignificant = 'i';
            continue;
        }

        char c = source[i++];

        r3d_rshade_writer_putc(writer, c);

        if (!isspace((unsigned char)c))
            previousSignificant = c;
    }
}

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

    if (length == 0 || length >= R3D_MAX_SHADER_CODE_LENGTH)
        return NULL;

    R3D_SurfaceEffect* effect = r3d_zalloc(sizeof(*effect));

    effect->source = r3d_zalloc(length + 1);
    memcpy(effect->source, code, length + 1);

    r3d_rshade_source_t parsed = {0};

    if (!r3d_rshade_parse_source(effect->source, &parsed))
    {
        TraceLog(LOG_ERROR, "R3D: Invalid surface effect Ray-Shade source");
        R3D_UnloadSurfaceEffect(effect);
        return NULL;
    }

    memcpy(&effect->uniforms, &parsed.uniforms, sizeof(parsed.uniforms));
    memcpy(effect->samplers, parsed.samplers, sizeof(parsed.samplers));
    memcpy(effect->varyings, parsed.varyings, sizeof(parsed.varyings));

    effect->uniformCount = parsed.uniformCount;
    effect->samplerCount = parsed.samplerCount;
    effect->varyingCount = parsed.varyingCount;

    effect->uniforms.bufferSize = parsed.uniformSize;

    effect->vertex = parsed.vertex;
    effect->fragment = parsed.fragment;

    // Preserve all global Ray-Shade code except metadata declarations
    // and the two entry functions. It will be namespaced during composition.
    effect->globals = r3d_zalloc(length + 1);

    r3d_rshade_writer_t writer = {
        .ptr = effect->globals,
        .remaining = length + 1,
        .overflow = false
    };

    r3d_rshade_copy_global_code(
        &writer,
        effect->source,
        true,
        &effect->vertex,
        &effect->fragment
    );

    if (writer.overflow)
    {
        TraceLog(LOG_ERROR, "R3D: Failed to extract effect global source");
        R3D_UnloadSurfaceEffect(effect);
        return NULL;
    }

    *writer.ptr = '\0';

    if (!effect_collect_symbols(effect))
    {
        TraceLog(LOG_ERROR, "R3D: Failed to namespace surface effect symbols");
        R3D_UnloadSurfaceEffect(effect);
        return NULL;
    }

    return effect;
}

void R3D_UnloadSurfaceEffect(R3D_SurfaceEffect* effect)
{
    if (!effect) return;

    r3d_effect_invalidate_resource(effect);

    r3d_free(effect->globals);
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

void R3D_SetSurfaceEffectSampler(R3D_SurfaceEffect* effect, const char* name, Texture texture)
{
    if (!effect || !name) return;

    for (int i = 0; i < effect->samplerCount; i++)
    {
        if (strcmp(effect->samplers[i].name, name) == 0)
        {
            effect->samplers[i].texture = texture.id;
            return;
        }
    }

    TraceLog(LOG_WARNING, "R3D: Unknown surface effect sampler '%s'", name);
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
    samplerTextures = R3D_LIST_CREATE(GLuint, 64);
    effectPackedBytes = R3D_LIST_CREATE(uint8_t, 4096);
    effectVariants = R3D_LIST_CREATE(r3d_effect_variant_t, 8);

    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &effectBufferAlignment);

    if (effectBufferAlignment < 1)
        effectBufferAlignment = 1;

    glGenBuffers(1, &effectUniformBuffer);

    captureCacheValid = false;
    effectRangeBound = false;
}

void r3d_effect_quit(void)
{
    if (effectVariants)
    {
        for (size_t i = 0; i < R3D_LIST_LENGTH(effectVariants); i++)
        {
            r3d_effect_variant_t* variant =
                &R3D_LIST_GET(effectVariants, r3d_effect_variant_t, i);

            r3d_shader_custom_free(variant->shader);
            r3d_free(variant->effects);
        }

        R3D_LIST_DESTROY(effectVariants);
    }

    if (effectUniformBuffer)
        glDeleteBuffers(1, &effectUniformBuffer);

    if (activeEffects) R3D_LIST_DESTROY(activeEffects);
    if (capturedEffects) R3D_LIST_DESTROY(capturedEffects);
    if (parameterBytes) R3D_LIST_DESTROY(parameterBytes);
    if (samplerTextures) R3D_LIST_DESTROY(samplerTextures);
    if (effectPackedBytes) R3D_LIST_DESTROY(effectPackedBytes);

    activeEffects = NULL;
    capturedEffects = NULL;
    parameterBytes = NULL;
    samplerTextures = NULL;
    effectPackedBytes = NULL;
    effectVariants = NULL;
    effectUniformBuffer = 0;

    captureCacheValid = false;
    effectRangeBound = false;
}

void r3d_effect_clear(void)
{
    if (activeEffects) R3D_LIST_CLEAR(activeEffects);
    if (capturedEffects) R3D_LIST_CLEAR(capturedEffects);
    if (parameterBytes) R3D_LIST_CLEAR(parameterBytes);
    if (samplerTextures) R3D_LIST_CLEAR(samplerTextures);
    if (effectPackedBytes) R3D_LIST_CLEAR(effectPackedBytes);

    captureCacheValid = false;
    effectRangeBound = false;
}

bool r3d_effect_has_active(void)
{
    return activeEffects && !R3D_LIST_EMPTY(activeEffects);
}

bool r3d_effect_push(R3D_SurfaceEffect* effect)
{
    if (!effect || !activeEffects || !parameterBytes || !samplerTextures)
    {
        TraceLog(LOG_ERROR, "R3D: Surface effect state is not initialized");
        return false;
    }

    size_t offset = R3D_LIST_LENGTH(parameterBytes);
    size_t size = (size_t)effect->uniforms.bufferSize;

    size_t samplerOffset = R3D_LIST_LENGTH(samplerTextures);

    if (samplerOffset > UINT32_MAX ||
        (size_t)effect->samplerCount > UINT32_MAX - samplerOffset)
    {
        TraceLog(LOG_ERROR, "R3D: Surface effect sampler snapshot limit reached");
        return false;
    }

    if (offset > UINT32_MAX || size > UINT32_MAX - offset)
    {
        TraceLog(LOG_ERROR, "R3D: Surface effect parameter storage limit reached");
        return false;
    }

    r3d_effect_snapshot_t snapshot = {
        .effect = effect,
        .parameterOffset = (uint32_t)offset,
        .parameterSize = (uint32_t)size,
        .samplerOffset = (uint32_t)samplerOffset
    };

    if (size > 0)
    {
        R3D_LIST_PUSH_ALL(
            parameterBytes,
            effect->uniforms.buffer,
            size
        );
    }

    for (int i = 0; i < effect->samplerCount; i++)
    {
        R3D_LIST_PUSH(samplerTextures, effect->samplers[i].texture);
    }

    R3D_LIST_PUSH(activeEffects, snapshot);
    captureCacheValid = false;
    return true;
}

void r3d_effect_pop(void)
{
    if (!r3d_effect_has_active())
    {
        TraceLog(LOG_WARNING, "R3D: Surface effect stack underflow");
        return;
    }

    R3D_LIST_POP(activeEffects, NULL);
    captureCacheValid = false;
}

static bool effect_pack_active(uint32_t* outOffset, uint32_t* outSize)
{
    int blockSize = 0;
    size_t activeCount = R3D_LIST_LENGTH(activeEffects);

    // Calculate the combined std140 layout.
    // This must match the layout generated by effect_build_variant().
    for (size_t i = 0; i < activeCount; i++)
    {
        const r3d_effect_snapshot_t* snapshot =
            &R3D_LIST_GET(activeEffects, r3d_effect_snapshot_t, i);

        const R3D_SurfaceEffect* effect = snapshot->effect;

        for (int j = 0; j < effect->uniformCount; j++)
        {
            const r3d_rshade_uniform_t* uniform = &effect->uniforms.entries[j];

            int alignment = r3d_rshade_get_std140_alignment(uniform->size);
            blockSize = r3d_align_offset(blockSize, alignment);

            if (blockSize > R3D_EFFECT_BLOCK_LIMIT - uniform->size)
            {
                TraceLog(LOG_ERROR, "R3D: Surface effect block exceeds maximum size");
                return false;
            }

            blockSize += uniform->size;
        }
    }

    blockSize = r3d_align_offset(blockSize, 16);

    // Effects without parameters don't need GPU uniform storage.
    if (blockSize == 0)
    {
        *outOffset = 0;
        *outSize = 0;
        return true;
    }

    // Each UBO range must begin at the GPU's required alignment.
    size_t current = R3D_LIST_LENGTH(effectPackedBytes);
    size_t alignment = (size_t)effectBufferAlignment;
    size_t padding = (alignment - current % alignment) % alignment;

    if (current > (size_t)INT_MAX ||
        padding > (size_t)INT_MAX - current ||
        (size_t)blockSize > (size_t)INT_MAX - current - padding)
    {
        TraceLog(LOG_ERROR, "R3D: Surface effect frame buffer limit reached");
        return false;
    }

    size_t offset = current + padding;

    // Resize also zero-fills the alignment gaps and padding.
    R3D_LIST_RESIZE(effectPackedBytes, offset + (size_t)blockSize);

    uint8_t* destination = (uint8_t*)effectPackedBytes->elements + offset;
    int cursor = 0;

    for (size_t i = 0; i < activeCount; i++)
    {
        const r3d_effect_snapshot_t* snapshot =
            &R3D_LIST_GET(activeEffects, r3d_effect_snapshot_t, i);

        const R3D_SurfaceEffect* effect = snapshot->effect;
        const uint8_t* parameters =
            (const uint8_t*)parameterBytes->elements + snapshot->parameterOffset;

        for (int j = 0; j < effect->uniformCount; j++)
        {
            const r3d_rshade_uniform_t* uniform = &effect->uniforms.entries[j];

            cursor = r3d_align_offset(
                cursor,
                r3d_rshade_get_std140_alignment(uniform->size)
            );

            memcpy(
                destination + cursor,
                parameters + uniform->offset,
                (size_t)uniform->size
            );

            cursor += uniform->size;
        }
    }

    *outOffset = (uint32_t)offset;
    *outSize = (uint32_t)blockSize;

    return true;
}

void r3d_effect_capture(int* first, int* count)
{
    if (!first || !count) return;

    *first = 0;
    *count = 0;

    if (!r3d_effect_has_active() || !capturedEffects)
        return;

    // Repeated draws under the same scope reuse the captured data.
    if (captureCacheValid)
    {
        *first = captureCacheFirst;
        *count = captureCacheCount;
        return;
    }

    size_t activeCount = R3D_LIST_LENGTH(activeEffects);
    size_t capturedCount = R3D_LIST_LENGTH(capturedEffects);

    if (activeCount > INT_MAX ||
        capturedCount > (size_t)INT_MAX - activeCount)
    {
        TraceLog(LOG_ERROR, "R3D: Surface effect snapshot limit reached");
        return;
    }

    uint32_t frameOffset = 0;
    uint32_t frameSize = 0;

    if (!effect_pack_active(&frameOffset, &frameSize))
        return;

    *first = (int)capturedCount;
    *count = (int)activeCount;

    R3D_LIST_PUSH_ALL(
        capturedEffects,
        activeEffects->elements,
        activeCount
    );

    // The first snapshot identifies the packed block for this scope.
    r3d_effect_snapshot_t* primary =
        &R3D_LIST_GET(capturedEffects, r3d_effect_snapshot_t, capturedCount);

    primary->frameOffset = frameOffset;
    primary->frameSize = frameSize;

    captureCacheFirst = *first;
    captureCacheCount = *count;
    captureCacheValid = true;
}

void r3d_effect_upload_frame(void)
{
    effectRangeBound = false;

    if (!effectUniformBuffer || !effectPackedBytes ||
        R3D_LIST_EMPTY(effectPackedBytes))
    {
        return;
    }

    GLsizeiptr size = (GLsizeiptr)R3D_LIST_LENGTH(effectPackedBytes);

    glBindBuffer(GL_UNIFORM_BUFFER, effectUniformBuffer);

    // Replace the previous frame's storage.
    // This avoids modifying individual ranges during drawing.
    glBufferData(
        GL_UNIFORM_BUFFER,
        size,
        effectPackedBytes->elements,
        GL_STREAM_DRAW
    );

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

static r3d_effect_variant_t* effect_find_variant(R3D_SurfaceShader* base, int first, int count)
{
    const r3d_shader_custom_program_t* baseProgram = base ? base->program : NULL;

    for (size_t i = 0; i < R3D_LIST_LENGTH(effectVariants); i++)
    {
        r3d_effect_variant_t* variant =
            &R3D_LIST_GET(effectVariants, r3d_effect_variant_t, i);

        if (variant->baseProgram != baseProgram || variant->count != count)
            continue;

        bool matches = true;

        for (int j = 0; j < count; j++)
        {
            const r3d_effect_snapshot_t* snapshot =
                &R3D_LIST_GET(capturedEffects, r3d_effect_snapshot_t, first + j);

            if (variant->effects[j] != snapshot->effect)
            {
                matches = false;
                break;
            }
        }

        if (matches) return variant;
    }

    return NULL;
}

static const char* effect_sampler_type(GLenum target)
{
    switch (target)
    {
        case GL_TEXTURE_1D:       return "sampler1D";
        case GL_TEXTURE_2D:       return "sampler2D";
        case GL_TEXTURE_3D:       return "sampler3D";
        case GL_TEXTURE_CUBE_MAP: return "samplerCube";
        default:                 return NULL;
    }
}

static void effect_write_varyings(r3d_rshade_writer_t* writer, const R3D_SurfaceEffect* effect, int slot, const char* direction)
{
    for (int i = 0; i < effect->varyingCount; i++)
    {
        const r3d_rshade_varying_t* varying = &effect->varyings[i];

        if (varying->qualifier[0])
        {
            r3d_rshade_writer_printf(
                writer,
                "%s %s %s r3dFx%d_%s;\n",
                varying->qualifier,
                direction,
                varying->type,
                slot,
                varying->name
            );
        }
        else
        {
            r3d_rshade_writer_printf(
                writer,
                "%s %s r3dFx%d_%s;\n",
                direction,
                varying->type,
                slot,
                varying->name
            );
        }
    }
}

static void effect_write_function(r3d_rshade_writer_t* writer, const R3D_SurfaceEffect* effect, int slot, bool vertex)
{
    const r3d_rshade_parsed_function_t* function =
        vertex ? &effect->vertex : &effect->fragment;

    if (!function->bodyStart) return;

    r3d_rshade_writer_printf(
        writer,
        "void r3dFx%d_%s() ",
        slot,
        vertex ? "vertex" : "fragment"
    );

    effect_write_renamed(
        writer,
        effect,
        slot,
        function->bodyStart,
        (size_t)(function->bodyEnd - function->bodyStart)
    );

    r3d_rshade_writer_putc(writer, '\n');
}

static bool effect_build_variant(r3d_effect_variant_t* variant, R3D_SurfaceShader* base)
{
    int totalSamplers = 0;

    if (base)
    {
        while (totalSamplers < R3D_MAX_SHADER_SAMPLERS &&
               base->data.samplers[totalSamplers].name[0])
        {
            totalSamplers++;
        }
    }

    int baseSamplerCount = totalSamplers;

    for (int slot = 0; slot < variant->count; slot++)
    {
        totalSamplers += variant->effects[slot]->samplerCount;
    }

    if (totalSamplers > R3D_MAX_SHADER_SAMPLERS)
    {
        TraceLog(
            LOG_ERROR,
            "R3D: Composed shader requires %d samplers, maximum is %d",
            totalSamplers,
            R3D_MAX_SHADER_SAMPLERS
        );

        return false;
    }

    R3D_SurfaceShader* shader = r3d_shader_custom_alloc();
    if (!shader) return false;

    // Existing material samplers occupy the first slots.
    if (base)
    {
        memcpy(
            shader->data.samplers,
            base->data.samplers,
            sizeof(shader->data.samplers)
        );
    }

    // Effects use the remaining custom sampler slots.
    // Their GLSL identifiers are namespaced per effect occurrence.
    int samplerIndex = baseSamplerCount;

    for (int slot = 0; slot < variant->count; slot++)
    {
        const R3D_SurfaceEffect* effect = variant->effects[slot];

        for (int i = 0; i < effect->samplerCount; i++)
        {
            r3d_rshade_sampler_t* sampler =
                &shader->data.samplers[samplerIndex++];

            int written = snprintf(
                sampler->name,
                sizeof(sampler->name),
                "r3dFx%d_%s",
                slot,
                effect->samplers[i].name
            );

            if (written < 0 || (size_t)written >= sizeof(sampler->name))
            {
                TraceLog(LOG_ERROR, "R3D: Composed sampler name too long");
                r3d_shader_custom_free(shader);
                return false;
            }

            sampler->target = effect->samplers[i].target;
            sampler->texture = 0;
        }
    }

    r3d_rshade_writer_t writer = {
        .ptr = shader->program->userCode,
        .remaining = sizeof(shader->program->userCode),
        .overflow = false
    };

    const char* baseCode = base ? base->program->userCode : NULL;
    bool baseVertex = baseCode && strstr(baseCode, "void vertex()");
    bool baseFragment = baseCode && strstr(baseCode, "void fragment()");

    // Preserve the original material shader and its UserBlock.
    if (baseCode)
    {
        r3d_rshade_writer_printf(&writer, "#define vertex r3dBaseVertex\n");
        r3d_rshade_writer_printf(&writer, "#define fragment r3dBaseFragment\n");

        r3d_rshade_writer_write(&writer, baseCode, strlen(baseCode));

        r3d_rshade_writer_printf(
            &writer,
            "\n#undef vertex\n#undef fragment\n"
        );
    }

    // Create the independent EffectBlock.
    int uniformOffset = 0;
    bool hasUniforms = false;

    for (int slot = 0; slot < variant->count; slot++)
    {
        if (variant->effects[slot]->uniformCount > 0)
            hasUniforms = true;
    }

    if (hasUniforms)
    {
        r3d_rshade_writer_printf(
            &writer,
            "\nlayout(std140) uniform EffectBlock {\n"
        );

        for (int slot = 0; slot < variant->count; slot++)
        {
            const R3D_SurfaceEffect* effect = variant->effects[slot];

            for (int i = 0; i < effect->uniformCount; i++)
            {
                const r3d_rshade_uniform_t* uniform =
                    &effect->uniforms.entries[i];

                uniformOffset = r3d_align_offset(
                    uniformOffset,
                    r3d_rshade_get_std140_alignment(uniform->size)
                );

                if (uniformOffset > R3D_EFFECT_BLOCK_LIMIT - uniform->size)
                {
                    TraceLog(LOG_ERROR, "R3D: EffectBlock size limit exceeded");
                    r3d_shader_custom_free(shader);
                    return false;
                }

                uniformOffset += uniform->size;

                r3d_rshade_writer_printf(
                    &writer,
                    "    %s r3dFx%d_%s;\n",
                    uniform->type,
                    slot,
                    uniform->name
                );
            }
        }

        r3d_rshade_writer_printf(&writer, "};\n");
    }

    variant->uniformSize = r3d_align_offset(uniformOffset, 16);

    if (variant->uniformSize > R3D_EFFECT_BLOCK_LIMIT)
    {
        TraceLog(LOG_ERROR, "R3D: EffectBlock exceeds 16 KB");
        r3d_shader_custom_free(shader);
        return false;
    }

    // Declare all effect-owned texture samplers outside the UBO.
    for (int slot = 0; slot < variant->count; slot++)
    {
        const R3D_SurfaceEffect* effect = variant->effects[slot];

        for (int i = 0; i < effect->samplerCount; i++)
        {
            const char* type =
                effect_sampler_type(effect->samplers[i].target);

            if (!type)
            {
                TraceLog(LOG_ERROR, "R3D: Unsupported effect sampler type");
                r3d_shader_custom_free(shader);
                return false;
            }

            r3d_rshade_writer_printf(
                &writer,
                "uniform %s r3dFx%d_%s;\n",
                type,
                slot,
                effect->samplers[i].name
            );
        }
    }

    // Write each effect's varyings, global code and helpers.
    // Shared identifiers are rewritten to unique slot-qualified names.
    for (int slot = 0; slot < variant->count; slot++)
    {
        const R3D_SurfaceEffect* effect = variant->effects[slot];

        r3d_rshade_writer_printf(&writer, "\n#ifdef STAGE_VERT\n");
        effect_write_varyings(&writer, effect, slot, "out");
        r3d_rshade_writer_printf(&writer, "#endif\n");

        r3d_rshade_writer_printf(&writer, "#ifdef STAGE_FRAG\n");
        effect_write_varyings(&writer, effect, slot, "in");
        r3d_rshade_writer_printf(&writer, "#endif\n");

        effect_write_renamed(
            &writer,
            effect,
            slot,
            effect->globals,
            strlen(effect->globals)
        );

        r3d_rshade_writer_putc(&writer, '\n');
    }

    // The original material vertex hook retains its original timing.
    // Temporary effect vertex hooks run after GPU morphing.
    r3d_rshade_writer_printf(&writer, "\n#ifdef STAGE_VERT\n");

    for (int slot = 0; slot < variant->count; slot++)
    {
        effect_write_function(
            &writer,
            variant->effects[slot],
            slot,
            true
        );
    }

    r3d_rshade_writer_printf(&writer, "void vertex() {\n");

    if (baseVertex)
        r3d_rshade_writer_printf(&writer, "    r3dBaseVertex();\n");

    r3d_rshade_writer_printf(&writer, "}\n");

    r3d_rshade_writer_printf(
        &writer,
        "#define R3D_SURFACE_EFFECT_POST_MORPH\n"
        "void r3dEffectPostMorph() {\n"
    );

    for (int slot = 0; slot < variant->count; slot++)
    {
        if (variant->effects[slot]->vertex.bodyStart)
        {
            r3d_rshade_writer_printf(
                &writer,
                "    r3dFx%d_vertex();\n",
                slot
            );
        }
    }

    r3d_rshade_writer_printf(&writer, "}\n#endif\n");

    // Fragment effects execute after the material's fragment hook.
    // They therefore modify the material output in Push order.
    r3d_rshade_writer_printf(&writer, "\n#ifdef STAGE_FRAG\n");

    for (int slot = 0; slot < variant->count; slot++)
    {
        effect_write_function(
            &writer,
            variant->effects[slot],
            slot,
            false
        );
    }

    r3d_rshade_writer_printf(&writer, "void fragment() {\n");

    if (baseFragment)
        r3d_rshade_writer_printf(&writer, "    r3dBaseFragment();\n");

    for (int slot = 0; slot < variant->count; slot++)
    {
        if (variant->effects[slot]->fragment.bodyStart)
        {
            r3d_rshade_writer_printf(
                &writer,
                "    r3dFx%d_fragment();\n",
                slot
            );
        }
    }

    r3d_rshade_writer_printf(&writer, "}\n#endif\n");

    if (writer.overflow)
    {
        TraceLog(LOG_ERROR, "R3D: Composed Ray-Shade source exceeds size limit");
        r3d_shader_custom_free(shader);
        return false;
    }

    *writer.ptr = '\0';

    // Original material shader uniforms retain the existing UserBlock.
    if (base)
    {
        memcpy(
            shader->data.uniforms.entries,
            base->data.uniforms.entries,
            sizeof(shader->data.uniforms.entries)
        );

        memcpy(
            shader->data.uniforms.buffer,
            base->data.uniforms.buffer,
            sizeof(shader->data.uniforms.buffer)
        );

        if (base->data.uniforms.bufferSize > 0)
        {
            r3d_shader_custom_init_uniforms(
                shader,
                base->data.uniforms.bufferSize
            );
        }
    }

    variant->shader = shader;
    return true;
}

static void effect_sync_base_state(R3D_SurfaceShader* composed, R3D_SurfaceShader* base)
{
    if (!composed || !base || composed == base) return;

    const r3d_rshade_uniform_buffer_t* source = &base->data.uniforms;
    r3d_rshade_uniform_buffer_t* target = &composed->data.uniforms;

    if (source->bufferSize > 0)
    {
        size_t size = (size_t)source->bufferSize;

        if (target->bufferSize == source->bufferSize &&
            memcmp(target->buffer, source->buffer, size) != 0)
        {
            memcpy(target->buffer, source->buffer, size);
            target->dirty = true;
        }
    }
}

static void effect_sync_sampler_state(R3D_SurfaceShader* composed, R3D_SurfaceShader* base, int first, int count)
{
    if (!composed) return;

    int destination = 0;

    // Material sampler values retain their existing live semantics.
    if (base)
    {
        for (int i = 0;
             i < R3D_MAX_SHADER_SAMPLERS &&
             base->data.samplers[i].name[0];
             i++)
        {
            composed->data.samplers[destination++].texture =
                base->data.samplers[i].texture;
        }
    }

    // Effect sampler values are immutable submission snapshots.
    for (int slot = 0; slot < count; slot++)
    {
        const r3d_effect_snapshot_t* snapshot =
            &R3D_LIST_GET(capturedEffects, r3d_effect_snapshot_t, first + slot);

        const R3D_SurfaceEffect* effect = snapshot->effect;

        for (int i = 0; i < effect->samplerCount; i++)
        {
            GLuint texture = R3D_LIST_GET(
                samplerTextures,
                GLuint,
                (size_t)snapshot->samplerOffset + (size_t)i
            );

            composed->data.samplers[destination++].texture = texture;
        }
    }
}

R3D_SurfaceShader* r3d_effect_resolve_shader(R3D_SurfaceShader* base, int first, int count)
{
    if (count <= 0 || !effectVariants) return base;

    r3d_effect_variant_t* existing = effect_find_variant(base, first, count);

    if (existing)
    {
        if (!existing->shader)
            return base;

        effect_sync_base_state(existing->shader, base);
        effect_sync_sampler_state(existing->shader, base, first, count);

        return existing->shader;
    }

    r3d_effect_variant_t variant = {0};
    variant.baseProgram = base ? base->program : NULL;
    variant.count = count;
    variant.effects = r3d_zalloc((size_t)count * sizeof(*variant.effects));

    for (int i = 0; i < count; i++)
    {
        const r3d_effect_snapshot_t* snapshot =
            &R3D_LIST_GET(capturedEffects, r3d_effect_snapshot_t, first + i);

        variant.effects[i] = snapshot->effect;
    }

    if (!effect_build_variant(&variant, base))
    {
        // Cache failed combinations too, to avoid retrying every draw.
        // A diagnostic has already been emitted by the composer.
        R3D_LIST_PUSH(effectVariants, variant);
        return base;
    }

    R3D_LIST_PUSH(effectVariants, variant);

    effect_sync_base_state(variant.shader, base);
    effect_sync_sampler_state(variant.shader, base, first, count);

    return variant.shader;
}

void r3d_effect_bind_shader(R3D_SurfaceShader* shader, R3D_SurfaceShader* base, int first, int count, unsigned int program)
{
    if (!shader || count <= 0 || !effectUniformBuffer || program == 0)
        return;

    if (first < 0 || (size_t)first >= R3D_LIST_LENGTH(capturedEffects))
        return;

    r3d_effect_variant_t* variant = effect_find_variant(base, first, count);

    if (!variant || variant->shader != shader || variant->uniformSize <= 0)
        return;

    const r3d_effect_snapshot_t* snapshot =
        &R3D_LIST_GET(capturedEffects, r3d_effect_snapshot_t, first);

    uint32_t offset = snapshot->frameOffset;
    uint32_t size = snapshot->frameSize;

    if (size == 0 || size != (uint32_t)variant->uniformSize)
    {
        TraceLog(LOG_ERROR, "R3D: Surface effect uniform layout mismatch");
        return;
    }

    // Cache uniform-block setup per compiled GL program.
    // Some passes may optimize EffectBlock away entirely.
    bool hasBlock = false;
    bool foundProgram = false;

    for (int i = 0; i < variant->boundProgramCount; i++)
    {
        if (variant->boundPrograms[i] == program)
        {
            hasBlock = variant->boundProgramHasBlock[i];
            foundProgram = true;
            break;
        }
    }

    if (!foundProgram)
    {
        GLuint index = glGetUniformBlockIndex(program, "EffectBlock");

        hasBlock = (index != GL_INVALID_INDEX);

        if (hasBlock)
        {
            glUniformBlockBinding(
                program,
                index,
                R3D_EFFECT_BLOCK_SLOT
            );
        }

        if (variant->boundProgramCount < 8)
        {
            int slot = variant->boundProgramCount++;

            variant->boundPrograms[slot] = program;
            variant->boundProgramHasBlock[slot] = hasBlock;
        }
    }

    if (!hasBlock)
        return;

    // The contents are already on the GPU.
    // Only change the bound range when necessary.
    if (!effectRangeBound ||
        effectBoundOffset != offset ||
        effectBoundSize != size)
    {
        glBindBufferRange(
            GL_UNIFORM_BUFFER,
            R3D_EFFECT_BLOCK_SLOT,
            effectUniformBuffer,
            (GLintptr)offset,
            (GLsizeiptr)size
        );

        effectBoundOffset = offset;
        effectBoundSize = size;
        effectRangeBound = true;
    }
}

static void effect_remove_variant(size_t index)
{
    r3d_effect_variant_t* variant =
        &R3D_LIST_GET(effectVariants, r3d_effect_variant_t, index);

    /* An invalidated program must not remain in the GL shader cache. */
    r3d_shader_invalidate_cache();

    r3d_shader_custom_free(variant->shader);
    r3d_free(variant->effects);

    R3D_LIST_UNORDERED_REMOVE(effectVariants, index);
}

void r3d_effect_invalidate_resource(R3D_SurfaceEffect* effect)
{
    if (!effectVariants) return;

    for (size_t i = 0; i < R3D_LIST_LENGTH(effectVariants);)
    {
        r3d_effect_variant_t* variant =
            &R3D_LIST_GET(effectVariants, r3d_effect_variant_t, i);

        bool used = false;

        for (int j = 0; j < variant->count; j++)
        {
            if (variant->effects[j] == effect)
            {
                used = true;
                break;
            }
        }

        if (used) effect_remove_variant(i);
        else i++;
    }
}

void r3d_effect_invalidate_base(R3D_SurfaceShader* shader)
{
    if (!effectVariants || !shader || !shader->programOwner) return;

    for (size_t i = 0; i < R3D_LIST_LENGTH(effectVariants);)
    {
        r3d_effect_variant_t* variant =
            &R3D_LIST_GET(effectVariants, r3d_effect_variant_t, i);

        if (variant->baseProgram == shader->program)
            effect_remove_variant(i);
        else
            i++;
    }
}