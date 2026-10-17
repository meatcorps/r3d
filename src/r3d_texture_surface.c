#include <r3d/r3d_texture_surface.h>
#include <r3d/r3d.h>
#include <r3d/r3d_vertex.h>
#include <raymath.h>

#include <math.h>
#include <stddef.h>

// ============================================================
// SHARED RESOURCES
// ============================================================

static R3D_Mesh textureQuadMesh = {0};
static R3D_SurfaceShader* textureQuadShader = NULL;

// Specialized Ray-Shade surface shader.
//
// R3D_NO_AUTO_FETCH prevents duplicate material sampling.
// The shader uses the existing rendering pipeline:
//
// UNLIT / DEPTH: Albedo + alpha only.
// LIT: Albedo, emission, normals and ORM.
//
// Emission is handled through R3D's existing material channel.
static const char* textureSurfaceSource =
    "#pragma usage opaque blend unlit shadow probe\n"
    "#define R3D_NO_AUTO_FETCH\n"
    "\n"
    "void fragment()\n"
    "{\n"
    "#if defined(UNLIT) || defined(PROBE_UNLIT) || defined(DEPTH) || defined(DEPTH_CUBE)\n"
    "    vec4 pixel = SampleAlbedo(TEXCOORD);\n"
    "    ALBEDO = pixel.rgb;\n"
    "    ALPHA = pixel.a;\n"
    "#else\n"
    "    FetchMaterial(TEXCOORD);\n"
    "#endif\n"
    "}\n";

R3D_SurfaceShader* R3D_LoadTextureSurfaceShader(void)
{
    return R3D_LoadSurfaceShaderFromMemory(textureSurfaceSource);
}

void R3D_UnloadTexture3DResources(void)
{
    if (R3D_IsMeshValid(textureQuadMesh))
        R3D_UnloadMesh(textureQuadMesh);

    textureQuadMesh = (R3D_Mesh){0};

    if (textureQuadShader)
        R3D_UnloadSurfaceShader(textureQuadShader);

    textureQuadShader = NULL;
}

bool R3D_InitTexture3DResources(void)
{
    if (R3D_IsMeshValid(textureQuadMesh) && textureQuadShader)
        return true;

    R3D_UnloadTexture3DResources();

    // Four vertices, two triangles.
    // UV: top-left (0,0), bottom-right (1,1).
    R3D_MeshData data = R3D_LoadMeshData(4, 6);

    if (!data.vertices || !data.indices)
    {
        R3D_UnloadMeshData(data);
        return false;
    }

    const Vector3 normal = {0.0f, 0.0f, 1.0f};
    const Vector4 tangent = {1.0f, 0.0f, 0.0f, 1.0f};

    data.vertices[0] = R3D_MakeVertex((Vector3){-0.5f, -0.5f, 0.0f}, (Vector2){0.0f, 1.0f}, normal, tangent, WHITE);
    data.vertices[1] = R3D_MakeVertex((Vector3){ 0.5f, -0.5f, 0.0f}, (Vector2){1.0f, 1.0f}, normal, tangent, WHITE);
    data.vertices[2] = R3D_MakeVertex((Vector3){ 0.5f,  0.5f, 0.0f}, (Vector2){1.0f, 0.0f}, normal, tangent, WHITE);
    data.vertices[3] = R3D_MakeVertex((Vector3){-0.5f,  0.5f, 0.0f}, (Vector2){0.0f, 0.0f}, normal, tangent, WHITE);

    data.indices[0] = 0;
    data.indices[1] = 1;
    data.indices[2] = 2;
    data.indices[3] = 0;
    data.indices[4] = 2;
    data.indices[5] = 3;

    data.vertexCount = 4;
    data.indexCount = 6;

    textureQuadMesh = R3D_LoadMesh(
        R3D_PRIMITIVE_TRIANGLES, data, NULL
    );

    R3D_UnloadMeshData(data);

    if (!R3D_IsMeshValid(textureQuadMesh))
        return false;

    textureQuadMesh.shadowCastMode =
        R3D_SHADOW_CAST_DISABLED;

    textureQuadShader = R3D_LoadTextureSurfaceShader();

    if (!textureQuadShader)
    {
        R3D_UnloadTexture3DResources();
        return false;
    }

    return true;
}

// ============================================================
// MATERIAL HELPERS
// ============================================================

R3D_Material R3D_CreateTextureSurfaceMaterial(R3D_SurfaceShader* shader)
{
    R3D_Material material = R3D_GetDefaultMaterial();

    material.shader = shader;
    material.unlit = true;
    material.albedo.color = WHITE;
    material.emission.energy = 0.0f;
    material.transparencyMode = R3D_TRANSPARENCY_OPAQUE;
    material.cullMode = R3D_CULL_BACK;
    material.uvScale = (Vector2){1.0f, 1.0f};
    material.uvOffset = (Vector2){0.0f, 0.0f};

    return material;
}

void R3D_SetTextureSurfaceTexture(R3D_Material* material, Texture2D texture, R3D_TextureSurfaceChannel channel)
{
    if (!material) return;

    // Keep albedo texture in all modes so the source alpha is
    // available even when the visible color comes from emission.
    material->albedo.texture = texture;
    material->emission.texture = (Texture2D){0};

    switch (channel)
    {
        case R3D_TEXTURE_SURFACE_EMISSION:
            material->albedo.color = (Color){0, 0, 0, 255};
            material->emission.texture = texture;
            material->emission.color = WHITE;
            material->emission.energy = 1.0f;
            material->unlit = false;
            break;

        case R3D_TEXTURE_SURFACE_BOTH:
            material->albedo.color = WHITE;
            material->emission.texture = texture;
            material->emission.color = WHITE;
            material->emission.energy = 1.0f;
            material->unlit = false;
            break;

        case R3D_TEXTURE_SURFACE_ALBEDO:
        default:
            material->albedo.color = WHITE;
            material->emission.energy = 0.0f;
            material->unlit = true;
            break;
    }
}

void R3D_SetTextureSurfaceRegion(R3D_Material* material, Rectangle source, int textureWidth, int textureHeight, bool flipY)
{
    if (!material || textureWidth <= 0 || textureHeight <= 0)
        return;

    float invWidth = 1.0f / (float)textureWidth;
    float invHeight = 1.0f / (float)textureHeight;

    material->uvScale.x = source.width * invWidth;
    material->uvOffset.x = source.x * invWidth;

    if (flipY)
    {
        material->uvScale.y = -source.height * invHeight;
        material->uvOffset.y = 1.0f - source.y * invHeight;
    }
    else
    {
        material->uvScale.y = source.height * invHeight;
        material->uvOffset.y = source.y * invHeight;
    }
}

void R3D_SetTextureSurfaceRenderTarget(R3D_Material* material, RenderTexture2D target, R3D_TextureSurfaceChannel channel)
{
    if (!material) return;

    R3D_SetTextureSurfaceTexture(
        material, target.texture, channel
    );

    R3D_SetTextureSurfaceRegion(
        material,
        (Rectangle){
            0.0f, 0.0f,
            (float)target.texture.width,
            (float)target.texture.height
        },
        target.texture.width,
        target.texture.height,
        true
    );
}

void R3D_SetTextureSurfaceHDR(R3D_Material* material, float intensity)
{
    if (!material) return;

    material->emission.energy = fmaxf(intensity, 0.0f);
}

void R3D_SetTextureSurfaceNormal(R3D_Material* material, Texture2D normal, float strength)
{
    if (!material) return;

    material->normal.texture = normal;
    material->normal.scale = strength;
}

void R3D_SetTextureSurfaceORM(R3D_Material* material, Texture2D orm)
{
    if (!material) return;

    material->orm.texture = orm;
}

// ============================================================
// DRAW OPTIONS
// ============================================================

R3D_TextureDrawOptions R3D_DefaultTextureDrawOptions(void)
{
    R3D_Material material = R3D_GetDefaultMaterial();

    return (R3D_TextureDrawOptions){
        .aspectMode = R3D_TEXTURE_ASPECT_CONTAIN,
        .channel = R3D_TEXTURE_SURFACE_ALBEDO,
        .billboardMode = R3D_BILLBOARD_DISABLED,
        .transparencyMode = R3D_TRANSPARENCY_OPAQUE,
        .cullMode = R3D_CULL_BACK,
        .depth = material.depth,
        .tint = WHITE,
        .emissionIntensity = 1.0f,
        .pixelsPerUnit = 100.0f
    };
}

// ============================================================
// QUAD DRAWING
// ============================================================

static void r3d_draw_texture_quad(
    Texture2D texture,
    Rectangle source,
    Vector3 position,
    Quaternion rotation,
    Vector2 size,
    Vector2 origin,
    R3D_TextureDrawOptions options,
    bool renderTarget)
{
    if (!texture.id || texture.width <= 0 || texture.height <= 0)
        return;

    if (!R3D_InitTexture3DResources())
        return;

    if (source.width == 0.0f || source.height == 0.0f)
    {
        source = (Rectangle){
            0.0f, 0.0f,
            (float)texture.width,
            (float)texture.height
        };
    }

    float sourceWidth = fabsf(source.width);
    float sourceHeight = fabsf(source.height);

    if (sourceWidth <= 0.0f || sourceHeight <= 0.0f)
        return;

    float aspect = sourceWidth / sourceHeight;

    if (options.aspectMode == R3D_TEXTURE_ASPECT_NATIVE)
    {
        if (options.pixelsPerUnit <= 0.0f)
            return;

        size.x = sourceWidth / options.pixelsPerUnit;
        size.y = sourceHeight / options.pixelsPerUnit;
    }
    else
    {
        if (size.x <= 0.0f || size.y <= 0.0f)
            return;

        if (options.aspectMode == R3D_TEXTURE_ASPECT_CONTAIN)
        {
            float boxAspect = size.x / size.y;

            if (boxAspect > aspect)
                size.x = size.y * aspect;
            else
                size.y = size.x / aspect;
        }
        else if (options.aspectMode == R3D_TEXTURE_ASPECT_COVER)
        {
            float boxAspect = size.x / size.y;

            if (aspect > boxAspect)
            {
                // Crop horizontal source area.
                float croppedWidth = sourceHeight * boxAspect;
                float direction = source.width < 0.0f ? -1.0f : 1.0f;

                source.x += (sourceWidth - croppedWidth) * 0.5f * direction;
                source.width = croppedWidth * direction;
            }
            else
            {
                // Crop vertical source area.
                float croppedHeight = sourceWidth / boxAspect;
                float direction = source.height < 0.0f ? -1.0f : 1.0f;

                source.y += (sourceHeight - croppedHeight) * 0.5f * direction;
                source.height = croppedHeight * direction;
            }
        }
    }

    R3D_Material material =
        R3D_CreateTextureSurfaceMaterial(textureQuadShader);

    R3D_SetTextureSurfaceTexture(
        &material, texture, options.channel
    );

    R3D_SetTextureSurfaceRegion(
        &material,
        source,
        texture.width,
        texture.height,
        renderTarget
    );

    if (options.channel == R3D_TEXTURE_SURFACE_EMISSION)
    {
        material.albedo.color =
            (Color){0, 0, 0, options.tint.a};
    }
    else
    {
        material.albedo.color = options.tint;
    }

    if (options.channel != R3D_TEXTURE_SURFACE_ALBEDO)
    {
        material.emission.color = options.tint;
        material.emission.energy =
            fmaxf(options.emissionIntensity, 0.0f);
    }

    material.billboardMode = options.billboardMode;
    material.transparencyMode = options.transparencyMode;
    material.cullMode = options.cullMode;
    material.depth = options.depth;

    // Normalized pivot:
    // (0,0) top-left, (0.5,0.5) center, (1,1) bottom-right.
    Vector3 localOffset = {
        (0.5f - origin.x) * size.x,
        (origin.y - 0.5f) * size.y,
        0.0f
    };

    Vector3 rotatedOffset =
        Vector3RotateByQuaternion(localOffset, rotation);

    Vector3 meshPosition =
        Vector3Add(position, rotatedOffset);

    Matrix transform = R3D_MatrixFromTRS(
        meshPosition,
        rotation,
        (Vector3){size.x, size.y, 1.0f}
    );

    R3D_DrawMeshPro(
        textureQuadMesh,
        material,
        transform
    );
}

// ============================================================
// TEXTURE DRAWING
// ============================================================

void R3D_DrawTexture3D(Texture2D texture, Vector3 position, float height)
{
    if (texture.width <= 0 || texture.height <= 0 || height <= 0.0f)
        return;

    float width =
        height * (float)texture.width / (float)texture.height;

    R3D_TextureDrawOptions options =
        R3D_DefaultTextureDrawOptions();

    R3D_DrawTexture3DPro(
        texture,
        (Rectangle){0, 0, (float)texture.width, (float)texture.height},
        position,
        QuaternionIdentity(),
        (Vector2){width, height},
        (Vector2){0.5f, 0.5f},
        options
    );
}

void R3D_DrawTexture3DEx(Texture2D texture, Vector3 position, Quaternion rotation, Vector2 size, Color tint)
{
    R3D_TextureDrawOptions options =
        R3D_DefaultTextureDrawOptions();

    options.aspectMode = R3D_TEXTURE_ASPECT_STRETCH;
    options.tint = tint;

    R3D_DrawTexture3DPro(
        texture,
        (Rectangle){0, 0, (float)texture.width, (float)texture.height},
        position,
        rotation,
        size,
        (Vector2){0.5f, 0.5f},
        options
    );
}

void R3D_DrawTexture3DPro(Texture2D texture, Rectangle source, Vector3 position, Quaternion rotation, Vector2 size, Vector2 origin, R3D_TextureDrawOptions options)
{
    r3d_draw_texture_quad(
        texture, source, position, rotation,
        size, origin, options, false
    );
}

void R3D_DrawTexture3DNative(Texture2D texture, Vector3 position, float pixelsPerUnit)
{
    R3D_TextureDrawOptions options =
        R3D_DefaultTextureDrawOptions();

    options.aspectMode = R3D_TEXTURE_ASPECT_NATIVE;
    options.pixelsPerUnit = pixelsPerUnit;

    R3D_DrawTexture3DPro(
        texture,
        (Rectangle){0, 0, (float)texture.width, (float)texture.height},
        position,
        QuaternionIdentity(),
        (Vector2){1.0f, 1.0f},
        (Vector2){0.5f, 0.5f},
        options
    );
}

// ============================================================
// RENDER TEXTURE DRAWING
// ============================================================

void R3D_DrawRenderTexture3D(RenderTexture2D target, Vector3 position, float height)
{
    if (target.texture.width <= 0 || target.texture.height <= 0 || height <= 0.0f)
        return;

    float width =
        height * (float)target.texture.width /
        (float)target.texture.height;

    R3D_DrawRenderTexture3DEx(
        target,
        position,
        QuaternionIdentity(),
        (Vector2){width, height},
        WHITE
    );
}

void R3D_DrawRenderTexture3DEx(RenderTexture2D target, Vector3 position, Quaternion rotation, Vector2 size, Color tint)
{
    R3D_TextureDrawOptions options =
        R3D_DefaultTextureDrawOptions();

    options.aspectMode = R3D_TEXTURE_ASPECT_STRETCH;
    options.tint = tint;

    R3D_DrawRenderTexture3DPro(
        target,
        (Rectangle){
            0, 0,
            (float)target.texture.width,
            (float)target.texture.height
        },
        position,
        rotation,
        size,
        (Vector2){0.5f, 0.5f},
        options
    );
}

void R3D_DrawRenderTexture3DPro(RenderTexture2D target, Rectangle source, Vector3 position, Quaternion rotation, Vector2 size, Vector2 origin, R3D_TextureDrawOptions options)
{
    r3d_draw_texture_quad(
        target.texture, source, position, rotation,
        size, origin, options, true
    );
}