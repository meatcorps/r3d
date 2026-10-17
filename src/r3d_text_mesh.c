/* r3d_text_mesh.c
 * stb_truetype -> Manifold -> R3D_MeshData
 */

#include <r3d/r3d_text_mesh.h>
#include <r3d/r3d_manifold.h>
#include <r3d/r3d_vertex.h>

// Raylib also compiles stb_truetype.
// Keep this copy's symbols local to this source file.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define R3D_TEXT_MAX_CONTOUR_POINTS 32768u
#define R3D_TEXT_MAX_GLYPHS 16384u
#define R3D_TEXT_MAX_FLATTEN_DEPTH 14

typedef struct R3D_TextPoints {
    ManifoldVec2* data;
    size_t count;
    size_t capacity;
} R3D_TextPoints;

typedef struct R3D_TextContours {
    ManifoldSimplePolygon** polygons;
    size_t count;
    size_t capacity;
    R3D_TextPoints current;
} R3D_TextContours;

typedef struct R3D_TextGlyph {
    int glyphIndex;
    int vertexCount;
    stbtt_vertex* vertices;
} R3D_TextGlyph;

typedef struct R3D_TextFontState {
    unsigned char* bytes;
    stbtt_fontinfo font;

    R3D_TextGlyph* glyphs;
    size_t glyphCount;
    size_t glyphCapacity;
} R3D_TextFontState;

static bool r3d_text_append_point(R3D_TextPoints* points, ManifoldVec2 p)
{
    if (!isfinite(p.x) || !isfinite(p.y)) return false;

    if (points->count > 0)
    {
        ManifoldVec2 last = points->data[points->count - 1];
        double dx = last.x - p.x;
        double dy = last.y - p.y;

        if (dx * dx + dy * dy < 1.0e-20) return true;
    }

    if (points->count >= R3D_TEXT_MAX_CONTOUR_POINTS) return false;

    if (points->count == points->capacity)
    {
        size_t capacity = points->capacity == 0 ? 32 : points->capacity * 2;

        if (capacity > R3D_TEXT_MAX_CONTOUR_POINTS)
            capacity = R3D_TEXT_MAX_CONTOUR_POINTS;

        ManifoldVec2* resized = realloc(
            points->data, capacity * sizeof(*resized)
        );

        if (!resized) return false;

        points->data = resized;
        points->capacity = capacity;
    }

    points->data[points->count++] = p;
    return true;
}

static double r3d_text_distance_line_sq(
    ManifoldVec2 p, ManifoldVec2 a, ManifoldVec2 b)
{
    double dx = b.x - a.x;
    double dy = b.y - a.y;
    double lengthSq = dx * dx + dy * dy;

    if (lengthSq < 1.0e-20)
    {
        dx = p.x - a.x;
        dy = p.y - a.y;
        return dx * dx + dy * dy;
    }

    double cross = (p.x - a.x) * dy - (p.y - a.y) * dx;
    return cross * cross / lengthSq;
}

static ManifoldVec2 r3d_text_midpoint(ManifoldVec2 a, ManifoldVec2 b)
{
    return (ManifoldVec2){
        (a.x + b.x) * 0.5,
        (a.y + b.y) * 0.5
    };
}

static bool r3d_text_quadratic(
    R3D_TextPoints* out,
    ManifoldVec2 a,
    ManifoldVec2 b,
    ManifoldVec2 c,
    double toleranceSq,
    int depth)
{
    if (depth == R3D_TEXT_MAX_FLATTEN_DEPTH ||
        r3d_text_distance_line_sq(b, a, c) <= toleranceSq)
    {
        return r3d_text_append_point(out, c);
    }

    ManifoldVec2 ab = r3d_text_midpoint(a, b);
    ManifoldVec2 bc = r3d_text_midpoint(b, c);
    ManifoldVec2 mid = r3d_text_midpoint(ab, bc);

    return r3d_text_quadratic(
               out, a, ab, mid, toleranceSq, depth + 1
           ) &&
           r3d_text_quadratic(
               out, mid, bc, c, toleranceSq, depth + 1
           );
}

static bool r3d_text_cubic(
    R3D_TextPoints* out,
    ManifoldVec2 a,
    ManifoldVec2 b,
    ManifoldVec2 c,
    ManifoldVec2 d,
    double toleranceSq,
    int depth)
{
    if (depth == R3D_TEXT_MAX_FLATTEN_DEPTH ||
        (r3d_text_distance_line_sq(b, a, d) <= toleranceSq &&
         r3d_text_distance_line_sq(c, a, d) <= toleranceSq))
    {
        return r3d_text_append_point(out, d);
    }

    ManifoldVec2 ab = r3d_text_midpoint(a, b);
    ManifoldVec2 bc = r3d_text_midpoint(b, c);
    ManifoldVec2 cd = r3d_text_midpoint(c, d);
    ManifoldVec2 abc = r3d_text_midpoint(ab, bc);
    ManifoldVec2 bcd = r3d_text_midpoint(bc, cd);
    ManifoldVec2 mid = r3d_text_midpoint(abc, bcd);

    return r3d_text_cubic(
               out, a, ab, abc, mid, toleranceSq, depth + 1
           ) &&
           r3d_text_cubic(
               out, mid, bcd, cd, d, toleranceSq, depth + 1
           );
}

static bool r3d_text_finish_contour(R3D_TextContours* contours)
{
    R3D_TextPoints* p = &contours->current;

    // Manifold polygons implicitly close their contours.
    // Avoid submitting a duplicate closing point.
    if (p->count > 1)
    {
        ManifoldVec2 a = p->data[0];
        ManifoldVec2 b = p->data[p->count - 1];

        double dx = a.x - b.x;
        double dy = a.y - b.y;

        if (dx * dx + dy * dy < 1.0e-20)
            p->count--;
    }

    if (p->count < 3)
    {
        p->count = 0;
        return true;
    }

    if (contours->count == contours->capacity)
    {
        size_t capacity = contours->capacity == 0
            ? 4 : contours->capacity * 2;

        if (capacity > SIZE_MAX / sizeof(*contours->polygons))
            return false;

        ManifoldSimplePolygon** resized = realloc(
            contours->polygons, capacity * sizeof(*resized)
        );

        if (!resized) return false;

        contours->polygons = resized;
        contours->capacity = capacity;
    }

    ManifoldSimplePolygon* polygon = manifold_simple_polygon(
        manifold_alloc_simple_polygon(), p->data, p->count
    );

    if (!polygon) return false;

    contours->polygons[contours->count++] = polygon;
    p->count = 0;

    return true;
}

static void r3d_text_free_contours(R3D_TextContours* contours)
{
    for (size_t i = 0; i < contours->count; i++)
        manifold_delete_simple_polygon(contours->polygons[i]);

    free(contours->polygons);
    free(contours->current.data);

    *contours = (R3D_TextContours){0};
}

static bool r3d_text_decode(
    const char** position, const char* end, uint32_t* codepoint)
{
    const unsigned char* ptr = (const unsigned char*)*position;

    if ((const char*)ptr >= end) return false;

    unsigned c0 = *ptr++;

    if (c0 < 0x80)
    {
        *codepoint = c0;
        *position = (const char*)ptr;
        return true;
    }

    unsigned length =
        c0 >= 0xF0 && c0 <= 0xF4 ? 4 :
        c0 >= 0xE0 && c0 <= 0xEF ? 3 :
        c0 >= 0xC2 && c0 <= 0xDF ? 2 : 0;

    if (length == 0 || (size_t)(end - *position) < length)
        goto invalid;

    uint32_t cp = c0 & ((1u << (7 - length)) - 1u);

    for (unsigned i = 1; i < length; i++)
    {
        unsigned byte = ptr[i - 1];

        if ((byte & 0xC0) != 0x80)
            goto invalid;

        cp = (cp << 6) | (byte & 0x3F);
    }

    if ((length == 2 && cp < 0x80) ||
        (length == 3 && cp < 0x800) ||
        (length == 4 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) ||
        cp > 0x10FFFF)
    {
        goto invalid;
    }

    *codepoint = cp;
    *position += length;
    return true;

invalid:
    *codepoint = 0xFFFD;
    (*position)++;
    return true;
}

static R3D_TextGlyph* r3d_text_get_cached_glyph(
    R3D_TextFontState* state, int glyphIndex)
{
    for (size_t i = 0; i < state->glyphCount; i++)
    {
        if (state->glyphs[i].glyphIndex == glyphIndex)
            return &state->glyphs[i];
    }

    if (state->glyphCount >= R3D_TEXT_MAX_GLYPHS)
        return NULL;

    if (state->glyphCount == state->glyphCapacity)
    {
        size_t capacity = state->glyphCapacity
            ? state->glyphCapacity * 2 : 32;

        if (capacity > R3D_TEXT_MAX_GLYPHS)
            capacity = R3D_TEXT_MAX_GLYPHS;

        R3D_TextGlyph* resized = realloc(
            state->glyphs,
            capacity * sizeof(*resized)
        );

        if (!resized)
            return NULL;

        state->glyphs = resized;
        state->glyphCapacity = capacity;
    }

    R3D_TextGlyph entry = {
        .glyphIndex = glyphIndex
    };

    entry.vertexCount = stbtt_GetGlyphShape(
        &state->font,
        glyphIndex,
        &entry.vertices
    );

    if (entry.vertexCount < 0 ||
        (entry.vertexCount > 0 && !entry.vertices))
    {
        if (entry.vertices)
            stbtt_FreeShape(&state->font, entry.vertices);

        return NULL;
    }

    state->glyphs[state->glyphCount] = entry;
    return &state->glyphs[state->glyphCount++];
}

R3D_TextFont R3D_LoadTextFontFromMemory(
    const unsigned char* data, size_t size)
{
    R3D_TextFont result = {0};

    if (!data || size < 12 || size > INT_MAX)
        return result;

    R3D_TextFontState* state = calloc(1, sizeof(*state));

    if (!state)
        return result;

    state->bytes = malloc(size);

    if (!state->bytes)
    {
        free(state);
        return result;
    }

    memcpy(state->bytes, data, size);

    int offset = stbtt_GetFontOffsetForIndex(
        state->bytes, 0
    );

    if (offset < 0 || (size_t)offset >= size ||
        !stbtt_InitFont(&state->font, state->bytes, offset))
    {
        free(state->bytes);
        free(state);
        return result;
    }

    result.internal = state;
    return result;
}

R3D_TextFont R3D_LoadTextFontFromFile(const char* path)
{
    R3D_TextFont result = {0};

    if (!path)
        return result;

    int fileSize = 0;
    unsigned char* bytes = LoadFileData(path, &fileSize);

    if (!bytes)
        return result;

    if (fileSize > 0)
    {
        result = R3D_LoadTextFontFromMemory(
            bytes, (size_t)fileSize
        );
    }

    UnloadFileData(bytes);
    return result;
}

bool R3D_IsTextFontValid(R3D_TextFont font)
{
    return font.internal != NULL;
}

bool R3D_PreloadTextFontGlyphs(
    R3D_TextFont* font, const char* utf8Text)
{
    if (!font || !font->internal || !utf8Text)
        return false;

    R3D_TextFontState* state = font->internal;

    const char* cursor = utf8Text;
    const char* end = utf8Text + strlen(utf8Text);

    uint32_t codepoint;

    while (r3d_text_decode(&cursor, end, &codepoint))
    {
        if (codepoint == '\r' || codepoint == '\n')
            continue;

        if (codepoint == '\t')
            codepoint = ' ';

        int glyphIndex = stbtt_FindGlyphIndex(
            &state->font, (int)codepoint
        );

        if (!r3d_text_get_cached_glyph(state, glyphIndex))
            return false;
    }

    return true;
}

bool R3D_PreloadTextFontAllGlyphs(R3D_TextFont* font)
{
    if (!font || !font->internal)
        return false;

    R3D_TextFontState* state = font->internal;

    const int glyphCount = state->font.numGlyphs;

    if (glyphCount <= 0 ||
        (unsigned int)glyphCount > R3D_TEXT_MAX_GLYPHS)
        return false;

    for (int i = 0; i < glyphCount; i++)
    {
        if (!r3d_text_get_cached_glyph(state, i))
            return false;
    }

    return true;
}

void R3D_UnloadTextFont(R3D_TextFont font)
{
    R3D_TextFontState* state = font.internal;

    if (!state)
        return;

    for (size_t i = 0; i < state->glyphCount; i++)
    {
        if (state->glyphs[i].vertices)
        {
            stbtt_FreeShape(
                &state->font,
                state->glyphs[i].vertices
            );
        }
    }

    free(state->glyphs);
    free(state->bytes);
    free(state);
}

static int r3d_text_glyph_index(
    const stbtt_fontinfo* font, uint32_t codepoint)
{
    return stbtt_FindGlyphIndex(font, (int)codepoint);
}

static double r3d_text_advance(
    const stbtt_fontinfo* font, int glyph, float scale)
{
    int advance = 0;
    stbtt_GetGlyphHMetrics(font, glyph, &advance, NULL);
    return advance * (double)scale;
}

static double r3d_text_measure(
    const stbtt_fontinfo* font,
    const char* start,
    const char* end,
    float scale,
    float tracking)
{
    const char* cursor = start;
    double pen = 0.0;
    int previous = -1;
    uint32_t codepoint;

    while (r3d_text_decode(&cursor, end, &codepoint))
    {
        int glyph = r3d_text_glyph_index(
            font, codepoint == '\t' ? ' ' : codepoint
        );

        if (previous != -1)
            pen += tracking;

        if (previous != -1 && codepoint != '\t')
        {
            pen += stbtt_GetGlyphKernAdvance(
                font, previous, glyph
            ) * (double)scale;
        }

        pen += r3d_text_advance(font, glyph, scale) *
               (codepoint == '\t' ? 4.0 : 1.0);

        previous = codepoint == '\t' ? -1 : glyph;
    }

    return pen;
}

static ManifoldCrossSection* r3d_text_build_glyph(const R3D_TextGlyph* cached, double scale, double toleranceSq, bool* succeeded)
{
    *succeeded = cached != NULL;

    if (!cached)
        return NULL;

    const int vertexCount = cached->vertexCount;
    const stbtt_vertex* shape = cached->vertices;

    // A valid glyph may contain no drawable contours.
    if (vertexCount == 0)
        return NULL;

    if (vertexCount < 0 || !shape)
    {
        *succeeded = false;
        return NULL;
    }

    R3D_TextContours contours = {0};
    bool ok = true;
    ManifoldVec2 previous = {0};
    bool hasContour = false;

    for (int i = 0; i < vertexCount && ok; i++)
    {
        stbtt_vertex v = shape[i];

        ManifoldVec2 endpoint = {
            v.x * scale,
            v.y * scale
        };

        switch (v.type)
        {
            case STBTT_vmove:
                if (hasContour)
                    ok = r3d_text_finish_contour(&contours);

                if (ok)
                    ok = r3d_text_append_point(
                        &contours.current, endpoint
                    );

                previous = endpoint;
                hasContour = true;
                break;

            case STBTT_vline:
                if (!hasContour)
                {
                    ok = false;
                    break;
                }

                ok = r3d_text_append_point(
                    &contours.current, endpoint
                );

                previous = endpoint;
                break;

            case STBTT_vcurve:
                if (!hasContour)
                {
                    ok = false;
                    break;
                }

                ok = r3d_text_quadratic(
                    &contours.current,
                    previous,
                    (ManifoldVec2){v.cx * scale, v.cy * scale},
                    endpoint,
                    toleranceSq,
                    0
                );

                previous = endpoint;
                break;

            case STBTT_vcubic:
                if (!hasContour)
                {
                    ok = false;
                    break;
                }

                ok = r3d_text_cubic(
                    &contours.current,
                    previous,
                    (ManifoldVec2){v.cx * scale, v.cy * scale},
                    (ManifoldVec2){v.cx1 * scale, v.cy1 * scale},
                    endpoint,
                    toleranceSq,
                    0
                );

                previous = endpoint;
                break;

            default:
                ok = false;
                break;
        }
    }

    if (ok && hasContour)
        ok = r3d_text_finish_contour(&contours);

    ManifoldCrossSection* result = NULL;

    if (ok && contours.count > 0)
    {
        ManifoldPolygons* polygons = manifold_polygons(
            manifold_alloc_polygons(),
            contours.polygons,
            contours.count
        );

        if (polygons)
        {
            result = manifold_cross_section_of_polygons(
                manifold_alloc_cross_section(),
                polygons,
                MANIFOLD_FILL_RULE_EVEN_ODD
            );

            manifold_delete_polygons(polygons);
        }
    }

    if (!ok || (contours.count > 0 && !result))
        *succeeded = false;

    r3d_text_free_contours(&contours);
    return result;
}

static bool r3d_text_append_line(
    R3D_TextFontState* state,
    const char* start,
    const char* end,
    float scale,
    R3D_TextMeshSettings settings,
    double baseline,
    ManifoldCrossSectionVec* glyphs,
    size_t* totalGlyphs)
{
    const stbtt_fontinfo* font = &state->font;
    double width = r3d_text_measure(
        font, start, end, scale, settings.letterSpacing
    );

    double pen = settings.alignment == R3D_TEXT_MESH_ALIGN_CENTER
        ? -width * 0.5 :
        settings.alignment == R3D_TEXT_MESH_ALIGN_RIGHT
        ? -width : 0.0;

    int previous = -1;
    uint32_t cp;
    const char* cursor = start;

    while (r3d_text_decode(&cursor, end, &cp))
    {
        if (++(*totalGlyphs) > R3D_TEXT_MAX_GLYPHS)
            return false;

        int glyph = r3d_text_glyph_index(
            font, cp == '\t' ? ' ' : cp
        );

        if (previous != -1)
            pen += settings.letterSpacing;

        if (previous != -1 && cp != '\t')
        {
            pen += stbtt_GetGlyphKernAdvance(
                font, previous, glyph
            ) * (double)scale;
        }

        if (cp != '\t' && cp != ' ')
        {
            bool succeeded = false;

            R3D_TextGlyph* cached = r3d_text_get_cached_glyph(
                state, glyph
            );

            ManifoldCrossSection* shape = r3d_text_build_glyph(
                cached,
                scale,
                (double)settings.curveTolerance *
                settings.curveTolerance,
                &succeeded
            );

            if (!succeeded)
                return false;

            if (shape)
            {
                if (!manifold_cross_section_is_empty(shape))
                {
                    ManifoldCrossSection* moved =
                        manifold_cross_section_translate(
                            manifold_alloc_cross_section(),
                            shape,
                            pen,
                            baseline
                        );

                    if (!moved)
                    {
                        manifold_delete_cross_section(shape);
                        return false;
                    }

                    manifold_cross_section_vec_push_back(
                        glyphs, moved
                    );

                    manifold_delete_cross_section(moved);
                }

                manifold_delete_cross_section(shape);
            }
        }

        pen += r3d_text_advance(font, glyph, scale) *
               (cp == '\t' ? 4.0 : 1.0);

        previous = cp == '\t' ? -1 : glyph;
    }

    return true;
}

R3D_TextMeshSettings R3D_DefaultTextMeshSettings(void)
{
    return (R3D_TextMeshSettings){
        .fontSize = 1.0f,
        .depth = 0.2f,
        .curveTolerance = 0.005f,
        .letterSpacing = 0.0f,
        .lineSpacing = 1.0f,
        .alignment = R3D_TEXT_MESH_ALIGN_LEFT
    };
}

R3D_MeshData R3D_GenMeshDataText(
    R3D_TextFont* fontResource,
    const char* utf8Text,
    R3D_TextMeshSettings settings)
{
    R3D_MeshData result = {0};

    if (!fontResource || !fontResource->internal || !utf8Text ||
        !isfinite(settings.fontSize) || settings.fontSize <= 0.0f ||
        !isfinite(settings.depth) || settings.depth <= 0.0f ||
        !isfinite(settings.curveTolerance) ||
        settings.curveTolerance <= 0.0f ||
        !isfinite(settings.letterSpacing) ||
        !isfinite(settings.lineSpacing) ||
        settings.lineSpacing <= 0.0f ||
        settings.alignment < R3D_TEXT_MESH_ALIGN_LEFT ||
        settings.alignment > R3D_TEXT_MESH_ALIGN_RIGHT)
    {
        return result;
    }

    R3D_TextFontState* state = fontResource->internal;
    const stbtt_fontinfo* font = &state->font;

    // Warm the cache first. Already loaded glyphs are reused.
    if (!R3D_PreloadTextFontGlyphs(fontResource, utf8Text))
        return result;

    int ascent, descent, lineGap;

    stbtt_GetFontVMetrics(
        font, &ascent, &descent, &lineGap
    );

    if (ascent <= descent)
        return result;

    float scale = stbtt_ScaleForPixelHeight(
        font, settings.fontSize
    );

    if (!isfinite(scale) || scale <= 0.0f)
        return result;

    double lineAdvance =
        (ascent - descent + lineGap) *
        (double)scale * settings.lineSpacing;

    if (!isfinite(lineAdvance) || lineAdvance <= 0.0)
        return result;

    ManifoldCrossSectionVec* glyphs =
        manifold_cross_section_empty_vec(
            manifold_alloc_cross_section_vec()
        );

    if (!glyphs)
        return result;

    const char* start = utf8Text;
    const char* cursor = utf8Text;

    double baseline = 0.0;
    size_t glyphCount = 0;
    bool ok = true;

    for (;;)
    {
        if (*cursor == '\n' ||
            *cursor == '\r' ||
            *cursor == '\0')
        {
            ok = r3d_text_append_line(
                state,
                start,
                cursor,
                scale,
                settings,
                baseline,
                glyphs,
                &glyphCount
            );

            if (!ok || *cursor == '\0')
                break;

            // CRLF is one line break.
            if (*cursor == '\r' && cursor[1] == '\n')
                cursor++;

            cursor++;
            start = cursor;

            baseline -= lineAdvance;

            if (!isfinite(baseline))
            {
                ok = false;
                break;
            }
        }
        else
        {
            cursor++;
        }
    }

    if (ok && manifold_cross_section_vec_length(glyphs) > 0)
    {
        // Union FILLED glyphs, not raw contours.
        ManifoldCrossSection* united =
            manifold_cross_section_batch_boolean(
                manifold_alloc_cross_section(),
                glyphs,
                MANIFOLD_ADD
            );

        if (united)
        {
            if (!manifold_cross_section_is_empty(united))
            {
                ManifoldPolygons* outlines =
                    manifold_cross_section_to_polygons(
                        manifold_alloc_polygons(),
                        united
                    );

                if (outlines)
                {
                    ManifoldManifold* solid = manifold_extrude(
                        manifold_alloc_manifold(),
                        outlines,
                        settings.depth,
                        1,
                        0.0,
                        1.0,
                        1.0
                    );

                    if (solid)
                    {
                        if (manifold_status(solid) ==
                                MANIFOLD_NO_ERROR &&
                            !manifold_is_empty(solid))
                        {
                            ManifoldManifold* normalSolid =
                                manifold_calculate_normals(
                                    manifold_alloc_manifold(),
                                    solid,
                                    0,
                                    45.0
                                );

                            if (normalSolid)
                            {
                                if (manifold_status(normalSolid) ==
                                    MANIFOLD_NO_ERROR)
                                {
                                    ManifoldMeshGL* output =
                                        manifold_get_meshgl(
                                            manifold_alloc_meshgl(),
                                            normalSolid
                                        );

                                    if (output)
                                    {
                                        result =
                                            R3D_ManifoldMeshGLToMeshData(
                                                output
                                            );

                                        manifold_delete_meshgl(output);
                                    }
                                }

                                manifold_delete_manifold(normalSolid);
                            }
                        }

                        manifold_delete_manifold(solid);
                    }

                    manifold_delete_polygons(outlines);
                }
            }

            manifold_delete_cross_section(united);
        }
    }

    manifold_delete_cross_section_vec(glyphs);

    if (R3D_IsMeshDataValid(result))
    {
        // Front/back: planar XY UV.
        // Sides: local tangent + depth UV.
        // Normals distinguish caps from side faces.
        for (int i = 0; i < result.vertexCount; i++)
        {
            R3D_Vertex* v = &result.vertices[i];

            Vector3 n = R3D_UnpackNormal(v->normal);
            Vector3 p = v->position;

            Vector2 uv = fabsf(n.z) >= 0.707f
                ? (Vector2){p.x, p.y}
                : (Vector2){
                    p.x * n.y - p.y * n.x,
                    p.z
                };

            R3D_PackTexCoord(v->texcoord, uv);
        }

        R3D_GenMeshDataTangents(
            &result, R3D_PRIMITIVE_TRIANGLES
        );
    }

    return result;
}