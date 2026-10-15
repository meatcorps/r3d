#include <r3d/r3d_render_mode_builder.h>

#include "./modules/r3d_render.h"

void R3D_PushBuiltRenderMode(R3D_BuiltRenderMode built)
{
    r3d_render_built_mode_push(built);
}

void R3D_PopBuiltRenderMode(void)
{
    r3d_render_built_mode_pop();
}

void R3D_UnloadBuiltRenderMode(R3D_BuiltRenderMode* built)
{
    if (!built) return;

    if (built->effect)
        R3D_UnloadSurfaceEffect(built->effect);

    *built = (R3D_BuiltRenderMode){0};
}

R3D_BuiltRenderMode R3D_RenderModeBuildDefault(void)
{
    return (R3D_BuiltRenderMode){0};
}

R3D_BuiltRenderMode R3D_RenderModeBuildUnlit(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask = R3D_RENDER_MODE_OVERRIDE_UNLIT;
    built.mode.unlit = true;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildAlphaBlend(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ALPHA;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildAdditive(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_ADDITIVE;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildMultiply(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY |
        R3D_RENDER_MODE_OVERRIDE_BLEND;

    built.mode.transparencyMode = R3D_TRANSPARENCY_BLEND;
    built.mode.blendMode = R3D_BLEND_MULTIPLY;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildDoubleSided(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask = R3D_RENDER_MODE_OVERRIDE_CULL;
    built.mode.cullMode = R3D_CULL_NONE;

    return built;
}

R3D_BuiltRenderMode R3D_RenderModeBuildShadowOnly(void)
{
    R3D_BuiltRenderMode built = {0};

    built.mode.overrideMask =
        R3D_RENDER_MODE_OVERRIDE_SHADOW |
        R3D_RENDER_MODE_OVERRIDE_TRANSPARENCY;

    built.mode.shadowCastMode = R3D_SHADOW_CAST_ONLY_AUTO;
    built.mode.transparencyMode = R3D_TRANSPARENCY_OPAQUE;

    return built;
}