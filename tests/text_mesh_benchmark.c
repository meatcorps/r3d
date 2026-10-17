/*
 * Phase 5 - Text Mesh CPU Benchmark
 *
 * Measures:
 *  - Font loading
 *  - Cold generation
 *  - Full glyph preloading
 *  - Repeated generation
 *  - Changing text
 *  - Rounded geometry generation
 *
 * Use --stress to increase the iteration counts.
 */

#include <r3d/r3d_text_mesh.h>
#include <raylib.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double benchmark_time_ms(void)
{
    struct timespec ts = {0};

    if (timespec_get(&ts, TIME_UTC) != TIME_UTC)
        return 0.0;

    return (double)ts.tv_sec * 1000.0 +
           (double)ts.tv_nsec / 1000000.0;
}

static R3D_TextMeshSettings benchmark_settings(bool smooth)
{
    R3D_TextMeshSettings settings =
        R3D_DefaultTextMeshSettings();

    settings.fontSize = 0.86f;
    settings.depth = 0.28f;
    settings.curveTolerance = 0.003f;
    settings.letterSpacing = 0.015f;
    settings.lineSpacing = 1.2f;
    settings.alignment = R3D_TEXT_MESH_ALIGN_CENTER;

    if (smooth)
    {
        settings.bevelSmoothness = 0.55f;
        settings.bevelSubdivisions = 2;
    }

    return settings;
}

static bool benchmark_once(
    R3D_TextFont* font,
    const char* label,
    const char* text,
    R3D_TextMeshSettings settings)
{
    double start = benchmark_time_ms();

    R3D_MeshData data = R3D_GenMeshDataText(
        font, text, settings
    );

    double elapsed = benchmark_time_ms() - start;

    bool valid =
        R3D_IsMeshDataValid(data) &&
        data.indexCount > 0 &&
        data.indexCount % 3 == 0;

    printf(
        "%-24s | %9.3f ms | %7d vertices | %7d triangles | %s\n",
        label,
        elapsed,
        data.vertexCount,
        data.indexCount / 3,
        valid ? "OK" : "FAILED"
    );

    R3D_UnloadMeshData(data);
    return valid;
}

static bool benchmark_series(
    R3D_TextFont* font,
    const char* label,
    R3D_TextMeshSettings settings,
    int iterations,
    bool changingText)
{
    static const char* strings[] = {
        "B8Oa@ 2026\nABC 012",
        "MEATCORPS\nPHASE FIVE",
        "PROCEDURAL\nGEOMETRY",
        "GAME OVER\n0123456789"
    };

    double total = 0.0;
    double minimum = 1.0e30;
    double maximum = 0.0;

    int maximumVertices = 0;
    int maximumTriangles = 0;
    int successful = 0;

    for (int i = 0; i < iterations; i++)
    {
        const char* text = changingText
            ? strings[i % 4]
            : strings[0];

        double start = benchmark_time_ms();

        R3D_MeshData data = R3D_GenMeshDataText(
            font, text, settings
        );

        double elapsed = benchmark_time_ms() - start;

        bool valid =
            R3D_IsMeshDataValid(data) &&
            data.indexCount > 0 &&
            data.indexCount % 3 == 0;

        if (!valid)
        {
            fprintf(
                stderr,
                "Benchmark failed: %s, iteration %d\n",
                label, i
            );

            R3D_UnloadMeshData(data);
            return false;
        }

        if (elapsed < minimum)
            minimum = elapsed;

        if (elapsed > maximum)
            maximum = elapsed;

        if (data.vertexCount > maximumVertices)
            maximumVertices = data.vertexCount;

        int triangles = data.indexCount / 3;

        if (triangles > maximumTriangles)
            maximumTriangles = triangles;

        total += elapsed;
        successful++;

        // Every generated buffer is released, including
        // during the longer stress benchmark.
        R3D_UnloadMeshData(data);
    }

    printf(
        "%-24s | n=%4d | avg=%9.3f ms | "
        "min=%9.3f | max=%9.3f | "
        "maxVerts=%d | maxTris=%d\n",
        label,
        successful,
        total / successful,
        minimum,
        maximum,
        maximumVertices,
        maximumTriangles
    );

    return true;
}

static bool benchmark_font(
    const char* path,
    const char* name,
    bool stress)
{
    printf("\n=== %s ===\n", name);

    double start = benchmark_time_ms();

    R3D_TextFont font = R3D_LoadTextFontFromFile(path);

    double loadMs = benchmark_time_ms() - start;

    if (!R3D_IsTextFontValid(font))
    {
        fprintf(stderr, "Failed to load font: %s\n", path);
        return false;
    }

    printf("Font loading: %.3f ms\n", loadMs);

    R3D_TextMeshSettings hard =
        benchmark_settings(false);

    R3D_TextMeshSettings soft =
        benchmark_settings(true);

    // First generation uses an initially empty glyph cache.
    bool ok = benchmark_once(
        &font,
        "Cold geometry",
        "B8Oa@ 2026\nABC 012",
        hard
    );

    start = benchmark_time_ms();

    bool preloaded = R3D_PreloadTextFontAllGlyphs(&font);

    double preloadMs = benchmark_time_ms() - start;

    printf(
        "Full glyph preload: %.3f ms (%s)\n",
        preloadMs,
        preloaded ? "OK" : "FAILED"
    );

    if (!preloaded)
        ok = false;

    if (ok)
    {
        int regularIterations = stress ? 200 : 24;
        int smoothIterations = stress ? 40 : 8;

        if (!benchmark_series(
                &font,
                "Warm, repeated",
                hard,
                regularIterations,
                false))
        {
            ok = false;
        }

        if (!benchmark_series(
                &font,
                "Warm, changing text",
                hard,
                regularIterations,
                true))
        {
            ok = false;
        }

        if (!benchmark_series(
                &font,
                "Smooth, repeated",
                soft,
                smoothIterations,
                false))
        {
            ok = false;
        }

        if (!benchmark_series(
                &font,
                "Smooth, changing text",
                soft,
                smoothIterations,
                true))
        {
            ok = false;
        }
    }

    // Release cached outlines and owned font bytes.
    R3D_UnloadTextFont(font);

    return ok;
}

int main(int argc, char** argv)
{
    bool stress =
        argc > 1 && strcmp(argv[1], "--stress") == 0;

    printf("R3D Text Mesh Benchmark\n");
    printf("Mode: %s\n", stress ? "STRESS" : "NORMAL");
    printf("All generation timings are CPU-side wall time.\n");

    bool block = benchmark_font(
        RESOURCES_PATH "fonts/Darkbyte-4nly6.ttf",
        "Darkbyte",
        stress
    );

    bool curved = benchmark_font(
        RESOURCES_PATH "fonts/Scabber-q2Mn0.ttf",
        "Scabber",
        stress
    );

    printf(
        "\nBenchmark result: %s\n",
        block && curved ? "PASS" : "FAIL"
    );

    return block && curved ? 0 : 1;
}