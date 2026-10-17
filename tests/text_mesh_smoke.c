#include <r3d/r3d_text_mesh.h>
#include <r3d/r3d_manifold.h>
#include <raylib.h>

#include <stdio.h>

static bool test_one(
    R3D_TextFont* font,
    const char* label,
    const char* text,
    int expectedGenus)
{
    R3D_TextMeshSettings settings =
        R3D_DefaultTextMeshSettings();

    settings.fontSize = 1.0f;
    settings.depth = 0.3f;
    settings.alignment = R3D_TEXT_MESH_ALIGN_CENTER;

    R3D_MeshData mesh = R3D_GenMeshDataText(
        font, text, settings
    );

    if (!R3D_IsMeshDataValid(mesh) ||
        mesh.indexCount <= 0 ||
        mesh.indexCount % 3 != 0)
    {
        fprintf(stderr,
            "Mesh generation failed: %s / %s\n",
            label, text
        );

        R3D_UnloadMeshData(mesh);
        return false;
    }

    ManifoldMeshGL* converted =
        R3D_MeshDataToManifoldMeshGL(&mesh);

    ManifoldManifold* solid = converted
        ? manifold_of_meshgl(
            manifold_alloc_manifold(), converted
          )
        : NULL;

    bool valid =
        solid &&
        manifold_status(solid) == MANIFOLD_NO_ERROR &&
        !manifold_is_empty(solid) &&
        manifold_volume(solid) > 0.0 &&
        (expectedGenus < 0 ||
         manifold_genus(solid) == expectedGenus);

    if (valid)
    {
        printf(
            "%s / %s: vertices=%d triangles=%d "
            "volume=%.6f genus=%d\n",
            label,
            text,
            mesh.vertexCount,
            mesh.indexCount / 3,
            manifold_volume(solid),
            manifold_genus(solid)
        );
    }
    else
    {
        fprintf(
            stderr,
            "Invalid solid: %s / %s\n",
            label, text
        );
    }

    if (solid)
        manifold_delete_manifold(solid);

    if (converted)
        manifold_delete_meshgl(converted);

    R3D_UnloadMeshData(mesh);
    return valid;
}

int main(void)
{
    const char* blockPath =
        RESOURCES_PATH "fonts/Darkbyte-4nly6.ttf";

    const char* roundedPath =
        RESOURCES_PATH "fonts/Scabber-q2Mn0.ttf";

    // Method 1: load directly from a file.
    R3D_TextFont block = R3D_LoadTextFontFromFile(
        blockPath
    );

    // Method 2: load from memory.
    int byteCount = 0;
    unsigned char* bytes = LoadFileData(
        roundedPath, &byteCount
    );

    R3D_TextFont rounded = {0};

    if (bytes && byteCount > 0)
    {
        rounded = R3D_LoadTextFontFromMemory(
            bytes, (size_t)byteCount
        );
    }

    // The memory loader made its own copy.
    if (bytes)
        UnloadFileData(bytes);

    bool ok =
        R3D_IsTextFontValid(block) &&
        R3D_IsTextFontValid(rounded);

    if (!ok)
    {
        fprintf(stderr, "Font loading failed\n");

        R3D_UnloadTextFont(block);
        R3D_UnloadTextFont(rounded);

        return 1;
    }

    const char* preload =
        "B8Oa@ 2026 ABC 012\n"
        "\xC3\xA9 \xCE\xA9";

    // Exercise explicit preloading.
    ok =
        R3D_PreloadTextFontGlyphs(&block, preload) &&
        R3D_PreloadTextFontGlyphs(&rounded, preload);

    if (ok)
    {
        // Reuse the same cached fonts for multiple meshes.
        if (!test_one(&block, "Darkbyte", "O", 1))
            ok = false;

        if (!test_one(&rounded, "Scabber", "O", 1))
            ok = false;

        if (!test_one(&block, "Darkbyte", "B8Oa@ 2026", -1))
            ok = false;

        if (!test_one(&rounded, "Scabber", "B8Oa@ 2026", -1))
            ok = false;

        if (!test_one(&block, "Darkbyte", "AB\nBA", -1))
            ok = false;

        if (!test_one(&rounded, "Scabber", "AB\nBA", -1))
            ok = false;

        if (!test_one(
                &block, "Darkbyte",
                "\xC3\xA9 \xCE\xA9", -1))
        {
            ok = false;
        }

        if (!test_one(
                &rounded, "Scabber",
                "\xC3\xA9 \xCE\xA9", -1))
        {
            ok = false;
        }
    }

    R3D_UnloadTextFont(block);
    R3D_UnloadTextFont(rounded);

    return ok ? 0 : 1;
}