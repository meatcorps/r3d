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

static r3d_list_t* effectPackedBytes = NULL;

static bool captureCacheValid = false;
static int captureCacheFirst = 0;
static int captureCacheCount = 0;

static GLint effectBufferAlignment = 1;

static bool effectRangeBound = false;
static uint32_t effectBoundOffset = 0;
static uint32_t effectBoundSize = 0;

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

    r3d_effect_invalidate_resource(effect);

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
    if (effectPackedBytes) R3D_LIST_DESTROY(effectPackedBytes);

    activeEffects = NULL;
    capturedEffects = NULL;
    parameterBytes = NULL;
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
    if (effectPackedBytes) R3D_LIST_CLEAR(effectPackedBytes);

    captureCacheValid = false;
    effectRangeBound = false;
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
    captureCacheValid = false;
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

static void effect_write_uniform_macros(r3d_rshade_writer_t* writer, const R3D_SurfaceEffect* effect, int slot, bool define)
{
    for (int i = 0; i < effect->uniformCount; i++)
    {
        const char* name = effect->uniforms.entries[i].name;

        if (define)
            r3d_rshade_writer_printf(writer, "#define %s r3dFx%d_%s\n", name, slot, name);
        else
            r3d_rshade_writer_printf(writer, "#undef %s\n", name);
    }
}

static void effect_write_function(r3d_rshade_writer_t* writer, const R3D_SurfaceEffect* effect, int slot, bool vertex)
{
    const r3d_rshade_parsed_function_t* function =
        vertex ? &effect->vertex : &effect->fragment;

    if (!function->bodyStart) return;

    effect_write_uniform_macros(writer, effect, slot, true);

    r3d_rshade_writer_printf(
        writer,
        "void r3dFx%d_%s() ",
        slot,
        vertex ? "vertex" : "fragment"
    );

    r3d_rshade_writer_write(
        writer,
        function->bodyStart,
        (size_t)(function->bodyEnd - function->bodyStart)
    );

    r3d_rshade_writer_putc(writer, '\n');

    effect_write_uniform_macros(writer, effect, slot, false);
}

static bool effect_build_variant(r3d_effect_variant_t* variant, R3D_SurfaceShader* base)
{
    R3D_SurfaceShader* shader = r3d_shader_custom_alloc();
    if (!shader) return false;

    r3d_rshade_writer_t writer = {
        .ptr = shader->program->userCode,
        .remaining = sizeof(shader->program->userCode),
        .overflow = false
    };

    const char* baseCode = base ? base->program->userCode : NULL;
    bool baseVertex = baseCode && strstr(baseCode, "void vertex()");
    bool baseFragment = baseCode && strstr(baseCode, "void fragment()");

    if (baseCode)
    {
        r3d_rshade_writer_printf(&writer, "#define vertex r3dBaseVertex\n");
        r3d_rshade_writer_printf(&writer, "#define fragment r3dBaseFragment\n");
        r3d_rshade_writer_write(&writer, baseCode, strlen(baseCode));
        r3d_rshade_writer_printf(&writer, "\n#undef vertex\n#undef fragment\n");
    }

    int uniformOffset = 0;
    bool hasUniforms = false;

    for (int slot = 0; slot < variant->count; slot++)
    {
        if (variant->effects[slot]->uniformCount > 0)
            hasUniforms = true;
    }

    if (hasUniforms)
    {
        r3d_rshade_writer_printf(&writer, "\nlayout(std140) uniform EffectBlock {\n");

        for (int slot = 0; slot < variant->count; slot++)
        {
            const R3D_SurfaceEffect* effect = variant->effects[slot];

            for (int i = 0; i < effect->uniformCount; i++)
            {
                const r3d_rshade_uniform_t* uniform = &effect->uniforms.entries[i];

                uniformOffset = r3d_align_offset(
                    uniformOffset,
                    r3d_rshade_get_std140_alignment(uniform->size)
                );

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
        TraceLog(LOG_ERROR, "R3D: Composed effect uniform block exceeds 16 KB");
        r3d_shader_custom_free(shader);
        return false;
    }

    r3d_rshade_writer_printf(&writer, "\n#ifdef STAGE_VERT\n");

    for (int slot = 0; slot < variant->count; slot++)
        effect_write_function(&writer, variant->effects[slot], slot, true);

    r3d_rshade_writer_printf(&writer, "\nvoid vertex() {\n");

    if (baseVertex)
        r3d_rshade_writer_printf(&writer, "    r3dBaseVertex();\n");

    r3d_rshade_writer_printf(&writer, "}\n");

    r3d_rshade_writer_printf(
        &writer,
        "\n#define R3D_SURFACE_EFFECT_POST_MORPH\n"
        "void r3dEffectPostMorph() {\n"
    );

    for (int slot = 0; slot < variant->count; slot++)
    {
        if (variant->effects[slot]->vertex.bodyStart)
            r3d_rshade_writer_printf(&writer, "    r3dFx%d_vertex();\n", slot);
    }

    r3d_rshade_writer_printf(&writer, "}\n#endif\n");

    r3d_rshade_writer_printf(&writer, "\n#ifdef STAGE_FRAG\n");

    for (int slot = 0; slot < variant->count; slot++)
        effect_write_function(&writer, variant->effects[slot], slot, false);

    r3d_rshade_writer_printf(&writer, "\nvoid fragment() {\n");

    if (baseFragment)
        r3d_rshade_writer_printf(&writer, "    r3dBaseFragment();\n");

    for (int slot = 0; slot < variant->count; slot++)
    {
        if (variant->effects[slot]->fragment.bodyStart)
            r3d_rshade_writer_printf(&writer, "    r3dFx%d_fragment();\n", slot);
    }

    r3d_rshade_writer_printf(&writer, "}\n#endif\n");

    if (writer.overflow)
    {
        TraceLog(LOG_ERROR, "R3D: Composed surface shader source exceeds limit");
        r3d_shader_custom_free(shader);
        return false;
    }

    *writer.ptr = '\0';

    if (base)
    {
        memcpy(shader->data.samplers, base->data.samplers, sizeof(shader->data.samplers));
        memcpy(shader->data.uniforms.entries, base->data.uniforms.entries, sizeof(shader->data.uniforms.entries));
        memcpy(shader->data.uniforms.buffer, base->data.uniforms.buffer, sizeof(shader->data.uniforms.buffer));

        if (base->data.uniforms.bufferSize > 0)
            r3d_shader_custom_init_uniforms(shader, base->data.uniforms.bufferSize);
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

    memcpy(
        composed->data.samplers,
        base->data.samplers,
        sizeof(base->data.samplers)
    );
}

R3D_SurfaceShader* r3d_effect_resolve_shader(R3D_SurfaceShader* base, int first, int count)
{
    if (count <= 0 || !effectVariants) return base;

    r3d_effect_variant_t* existing = effect_find_variant(base, first, count);

    if (existing)
    {
        effect_sync_base_state(existing->shader, base);
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
        r3d_free(variant.effects);
        return base;
    }

    R3D_LIST_PUSH(effectVariants, variant);

    effect_sync_base_state(variant.shader, base);

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