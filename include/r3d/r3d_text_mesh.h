#ifndef R3D_TEXT_MESH_H
#define R3D_TEXT_MESH_H

#include "./r3d_mesh_data.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum R3D_TextMeshAlign {
        R3D_TEXT_MESH_ALIGN_LEFT,
        R3D_TEXT_MESH_ALIGN_CENTER,
        R3D_TEXT_MESH_ALIGN_RIGHT
    } R3D_TextMeshAlign;

    typedef struct R3D_TextMeshSettings {
        float fontSize;
        float depth;
        float curveTolerance;
        float letterSpacing;
        float lineSpacing;

        float bevelSmoothness;   // 0 = disabled, 0..1 = smoothing strength
        int bevelSubdivisions;   // 2..4 when smoothing is enabled

        R3D_TextMeshAlign alignment;
    } R3D_TextMeshSettings;

    // Owns font bytes and cached, unscaled glyph outlines.
    // Treat as a resource handle: do not independently unload copies.
    typedef struct R3D_TextFont {
        void* internal;
    } R3D_TextFont;

    // Copies the supplied font bytes.
    R3DAPI R3D_TextFont R3D_LoadTextFontFromMemory(const unsigned char* data, size_t size);

    // Reads and owns a font from a file.
    R3DAPI R3D_TextFont R3D_LoadTextFontFromFile(const char* path);

    R3DAPI bool R3D_IsTextFontValid(R3D_TextFont font);

    // Optionally preload glyph outlines for the supplied UTF-8 text.
    // Other glyphs are cached automatically when first used.
    R3DAPI bool R3D_PreloadTextFontGlyphs(R3D_TextFont* font, const char* utf8Text);

    // Preloads every glyph contained in the font.
    // Useful when generated text is not known in advance.
    // Returns false if loading fails or the cache limit is exceeded.
    R3DAPI bool R3D_PreloadTextFontAllGlyphs(R3D_TextFont* font);

    // Releases font bytes, glyph cache and stb_truetype shapes.
    R3DAPI void R3D_UnloadTextFont(R3D_TextFont font);

    R3DAPI R3D_TextMeshSettings R3D_DefaultTextMeshSettings(void);

    // Generates independent caller-owned mesh data.
    // The font must remain loaded during this call, but can be
    // unloaded after generation completes.
    R3DAPI R3D_MeshData R3D_GenMeshDataText(R3D_TextFont* font, const char* utf8Text, R3D_TextMeshSettings settings);

#ifdef __cplusplus
}
#endif

#endif // R3D_TEXT_MESH_H