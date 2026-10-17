# Manifold + R3D — Native Geometry Cookbook

The one-line R3D Boolean methods already do the obvious stuff. This guide focuses on **Manifold's native C API** when you want custom cap materials, intricate cutters, multiple operations with a single GPU upload, or procedural mesh deformation.

Applicable with CMAKE optional `R3D_SUPPORT_MANIFOLD=ON` and Manifold 3.5.2. These are recipes, not an additional mesh abstraction.

```c
#include <r3d/r3d.h>
#include <r3d/r3d_manifold.h>
#include <r3d/r3d_vertex.h>
#include <raymath.h>
#include <math.h>
#include <string.h>
```

## 1. Custom cut: brick outside, concrete inside

R3D uses one material per mesh draw call. **Changing cap UVs alone cannot assign a second texture.** Let Manifold create the capped geometry, then split triangles into *exterior* and *cut surface* mesh data. Draw each with its own material, using the same transform.

This example classifies triangles whose **three vertices lie on the slice plane**. That's robust for an interior planar cut, though if an original face was already coplanar with the plane, that face also qualifies. For those special cases, track face provenance instead.

```c
// Outputs are ordinary GPU meshes, no persistent wrapper type.
static bool BuildCutMaterials(ManifoldManifold* half, Vector3 planeNormal, float offset, float tilesPerUnit, R3D_Mesh* outsideMesh, R3D_Mesh* capMesh)
{
    if (!half || !outsideMesh || !capMesh || tilesPerUnit <= 0) return false;
    *outsideMesh = (R3D_Mesh){0};
    *capMesh = (R3D_Mesh){0};
    bool ok = false;
    ManifoldManifold* normals = NULL;
    ManifoldMeshGL* gl = NULL;
    R3D_MeshData raw = {0}, shell = {0}, cap = {0};

    normals = manifold_calculate_normals(manifold_alloc_manifold(), half, 0, 52.5);
    if (!normals || manifold_status(normals) != MANIFOLD_NO_ERROR) goto cleanup;
    gl = manifold_get_meshgl(manifold_alloc_meshgl(), normals);
    if (!gl) goto cleanup;
    raw = R3D_ManifoldMeshGLToMeshData(gl);
    if (!raw.vertices || !raw.indices || raw.indexCount % 3) goto cleanup;

    shell = R3D_LoadMeshData(raw.vertexCount, raw.indexCount);
    cap = R3D_LoadMeshData(raw.vertexCount, raw.indexCount);
    if (!shell.vertices || !shell.indices || !cap.vertices || !cap.indices) goto cleanup;
    memcpy(shell.vertices, raw.vertices, raw.vertexCount * sizeof(R3D_Vertex));
    memcpy(cap.vertices, raw.vertices, raw.vertexCount * sizeof(R3D_Vertex));
    shell.vertexCount = cap.vertexCount = raw.vertexCount;

    planeNormal = Vector3Normalize(planeNormal);
    Vector3 reference = fabsf(planeNormal.y) < 0.9f ? (Vector3){0, 1, 0} : (Vector3){0, 0, 1};
    Vector3 axisU = Vector3Normalize(Vector3CrossProduct(reference, planeNormal));
    Vector3 axisV = Vector3CrossProduct(planeNormal, axisU);
    BoundingBox bbox = R3D_CalculateMeshDataBoundingBox(raw);
    Vector3 extent = Vector3Subtract(bbox.max, bbox.min);
    float eps = 0.0001f * fmaxf(1.0f, fmaxf(extent.x, fmaxf(extent.y, extent.z)));

    for (int i = 0; i < raw.indexCount; i += 3)
    {
        uint32_t ids[3] = {raw.indices[i], raw.indices[i + 1], raw.indices[i + 2]};
        bool isCap = true;
        for (int j = 0; j < 3; j++)
        {
            Vector3 p = raw.vertices[ids[j]].position;
            if (fabsf(Vector3DotProduct(p, planeNormal) - offset) > eps) isCap = false;
        }

        R3D_MeshData* destination = isCap ? &cap : &shell;
        for (int j = 0; j < 3; j++) destination->indices[destination->indexCount++] = ids[j];
        if (isCap)
        {
            for (int j = 0; j < 3; j++)
            {
                R3D_Vertex* vertex = &cap.vertices[ids[j]];
                Vector2 uv = {
                    tilesPerUnit * Vector3DotProduct(vertex->position, axisU),
                    tilesPerUnit * Vector3DotProduct(vertex->position, axisV)
                };
                R3D_PackTexCoord(vertex->texcoord, uv);
                vertex->color = WHITE;  // Essential: cutter faces may inherit alpha=0.
            }
        }
    }

    if (!shell.indexCount || !cap.indexCount) goto cleanup;
    R3D_GenMeshDataTangents(&cap, R3D_PRIMITIVE_TRIANGLES);
    *outsideMesh = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, shell, NULL);
    *capMesh = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, cap, NULL);
    ok = R3D_IsMeshValid(*outsideMesh) && R3D_IsMeshValid(*capMesh);

cleanup:
    if (!ok)
    {
        if (R3D_IsMeshValid(*outsideMesh)) R3D_UnloadMesh(*outsideMesh);
        if (R3D_IsMeshValid(*capMesh)) R3D_UnloadMesh(*capMesh);
        *outsideMesh = (R3D_Mesh){0};
        *capMesh = (R3D_Mesh){0};
    }
    R3D_UnloadMeshData(cap);
    R3D_UnloadMeshData(shell);
    R3D_UnloadMeshData(raw);
    if (gl) manifold_delete_meshgl(gl);
    if (normals) manifold_delete_manifold(normals);
    return ok;
}
```

Using the helper with a native plane split:

```c
R3D_MeshData sourceData = R3D_GenMeshDataCube(3, 2, 0.5f);
ManifoldMeshGL* sourceGL = R3D_MeshDataToManifoldMeshGL(&sourceData);
ManifoldManifold* solid = sourceGL ? manifold_of_meshgl(manifold_alloc_manifold(), sourceGL) : NULL;
R3D_Mesh topShell = {0}, topInterior = {0}, bottomShell = {0}, bottomInterior = {0};

if (solid && manifold_status(solid) == MANIFOLD_NO_ERROR)
{
    ManifoldManifoldPair halves = manifold_split_by_plane(manifold_alloc_manifold(), manifold_alloc_manifold(), solid, 0, 1, 0, 0);
    if (halves.first && manifold_status(halves.first) == MANIFOLD_NO_ERROR)
        BuildCutMaterials(halves.first, (Vector3){0, 1, 0}, 0, 2.0f, &topShell, &topInterior);
    if (halves.second && manifold_status(halves.second) == MANIFOLD_NO_ERROR)
        BuildCutMaterials(halves.second, (Vector3){0, 1, 0}, 0, 2.0f, &bottomShell, &bottomInterior);
    if (halves.first) manifold_delete_manifold(halves.first);
    if (halves.second) manifold_delete_manifold(halves.second);
}
if (solid) manifold_delete_manifold(solid);
if (sourceGL) manifold_delete_meshgl(sourceGL);
R3D_UnloadMeshData(sourceData);
```

During your existing R3D render pass:

```c
// Both meshes belonging to one half MUST use the same transform.
if (R3D_IsMeshValid(topShell)) R3D_DrawMesh(topShell, brickMaterial, (Vector3){0, 0.2f, 0}, 1);
if (R3D_IsMeshValid(topInterior)) R3D_DrawMesh(topInterior, cementMaterial, (Vector3){0, 0.2f, 0}, 1);
if (R3D_IsMeshValid(bottomShell)) R3D_DrawMesh(bottomShell, brickMaterial, (Vector3){0, -0.2f, 0}, 1);
if (R3D_IsMeshValid(bottomInterior)) R3D_DrawMesh(bottomInterior, cementMaterial, (Vector3){0, -0.2f, 0}, 1);
```

When the scene is destroyed, call `R3D_UnloadMesh()` on each valid result. No extra CPU-data buffers are retained after uploading. This separation duplicates the vertex array; it is deliberately straightforward rather than memory-optimal.

For an animated chunk, treat its *shell* and *interior* meshes as two render layers of **one** rigid body or transform. Manifold computes geometry, not physical objects.

## 2. A wall drilled by 15 cutters, one GPU upload

Instead of 15 convenience subtractions (and 15 R3D reuploads), generate a compound Manifold cutter, subtract once, upload once.

```c
static R3D_Mesh MakePerforatedWall(void)
{
    R3D_Mesh result = {0};
    ManifoldManifold* wall = manifold_cube(manifold_alloc_manifold(), 4, 2, 0.35, 1);
    ManifoldManifold* drill = manifold_cylinder(manifold_alloc_manifold(), 1.0, 0.20, 0.20, 24, 1);
    ManifoldManifoldVec* cutters = manifold_manifold_empty_vec(manifold_alloc_manifold_vec());
    ManifoldManifold* combined = NULL;
    ManifoldManifold* cut = NULL;
    if (!wall || !drill || !cutters) goto cleanup;

    for (int x = -2; x <= 2; x++)
    for (int y = -1; y <= 1; y++)
    {
        ManifoldManifold* moved = manifold_translate(manifold_alloc_manifold(), drill, 0.65*x, 0.55*y, 0);
        if (!moved) goto cleanup;
        manifold_manifold_vec_push_back(cutters, moved);
        manifold_delete_manifold(moved);
    }

    combined = manifold_batch_boolean(manifold_alloc_manifold(), cutters, MANIFOLD_ADD);
    if (!combined || manifold_status(combined) != MANIFOLD_NO_ERROR) goto cleanup;
    cut = manifold_difference(manifold_alloc_manifold(), wall, combined);
    if (cut && manifold_status(cut) == MANIFOLD_NO_ERROR && !manifold_is_empty(cut))
        R3D_UpdateMeshFromManifold(&result, cut);

cleanup:
    if (cut) manifold_delete_manifold(cut);
    if (combined) manifold_delete_manifold(combined);
    if (cutters) manifold_delete_manifold_vec(cutters);
    if (drill) manifold_delete_manifold(drill);
    if (wall) manifold_delete_manifold(wall);
    return result;
}
```

`manifold_cylinder()` is oriented along Z, convenient for this Z-thin wall. `R3D_UpdateMeshFromManifold()` handles conversion and upload into the zero-initialized GPU mesh. Native primitives normally do not include custom artist UVs: plan your UV processing accordingly. Run this at an event or at scene load—not every frame.

Variations: union rotated boxes for jagged breaches; use `manifold_sphere()` for craters; use `manifold_intersection()` for clipped sculpture pieces. Keep intermediate solids native until the last step.

## 3. Refine + warp = bent or melted geometry

Manifold provides higher-level mesh transformations beyond subtract/union. Refine geometry first; then bend it with a callback.

```c
static ManifoldVec3 BendWarp(double x, double y, double z, void* user)
{
    const double strength = *(const double*)user;
    return (ManifoldVec3){x, y + strength * sin(x * 5.0 + z), z};
}

static R3D_Mesh CreateBentMetal(void)
{
    R3D_Mesh gpu = {0};
    double strength = 0.18;
    ManifoldManifold* box = manifold_cube(manifold_alloc_manifold(), 3, 2, 0.4, 1);
    ManifoldManifold* refined = box ? manifold_refine_to_length(manifold_alloc_manifold(), box, 0.18) : NULL;
    ManifoldManifold* warped = refined ? manifold_warp(manifold_alloc_manifold(), refined, BendWarp, &strength) : NULL;
    if (warped && manifold_status(warped) == MANIFOLD_NO_ERROR && !manifold_is_empty(warped))
        R3D_UpdateMeshFromManifold(&gpu, warped);
    if (warped) manifold_delete_manifold(warped);
    if (refined) manifold_delete_manifold(refined);
    if (box) manifold_delete_manifold(box);
    return gpu;
}
```

Watch for stretched UVs or self-intersections with aggressive warps. For physically broken panels, add a native Boolean cut *after* the warp. This is CPU modeling, not a GPU shader.

## 4. Native operation → custom R3D vertex processing → upload

Use this whenever Manifold's topology is right but you want artistic control of UVs, color/alpha, roughness masks, or deliberately nonstandard surface mapping.

```c
// 'finished' is an existing valid ManifoldManifold* from native operations.
ManifoldManifold* normalSolid = manifold_calculate_normals(manifold_alloc_manifold(), finished, 0, 52.5);
ManifoldMeshGL* gl = normalSolid ? manifold_get_meshgl(manifold_alloc_meshgl(), normalSolid) : NULL;
R3D_MeshData data = gl ? R3D_ManifoldMeshGLToMeshData(gl) : (R3D_MeshData){0};

if (R3D_IsMeshDataValid(data))
{
    for (int i = 0; i < data.vertexCount; i++) data.vertices[i].color = WHITE;
    R3D_GenMeshDataUVsPlanar(&data, (Vector2){1,1}, (Vector3){0,0,1});
    R3D_GenMeshDataTangents(&data, R3D_PRIMITIVE_TRIANGLES);
}

R3D_Mesh gpu = R3D_IsMeshDataValid(data) ? R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, data, NULL) : (R3D_Mesh){0};
R3D_UnloadMeshData(data);
if (gl) manifold_delete_meshgl(gl);
if (normalSolid) manifold_delete_manifold(normalSolid);
```

That example remaps *all* UVs. For an imported textured mesh, you probably only want to change the new cut triangles (Recipe 1), not your original carefully authored UVs.

**Important:** `R3D_UpdateMesh()` only uploads GPU geometry; it does not replace an existing linked `mesh.meshData`. If you want subsequent convenience operations, synchronize your caller-owned CPU data:

```c
// `wall` already linked to `wallData` with R3D_LinkMeshData(&wall, &wallData).
if (R3D_IsMeshDataValid(data) && R3D_UpdateMesh(&wall, data, NULL))
{
    R3D_UnloadMeshData(wallData);
    wallData = data;             // &wallData pointer remains stable.
    data = (R3D_MeshData){0};    // Ownership transferred.
}
R3D_UnloadMeshData(data);
```

If you don't need to modify attributes, just call `R3D_UpdateMeshFromManifold(&wall, finished)`: it also updates the linked CPU data.

## 5. Multiple independently moving fragments

When you need more than the primary result, the native path is:

`manifold_split_by_plane()` or `manifold_difference()` → `manifold_decompose()` → `manifold_manifold_vec_get()` → per-part export/upload.

```c
// Assume a valid ManifoldManifold* broken from your previous operations.
ManifoldManifoldVec* components = manifold_decompose(manifold_alloc_manifold_vec(), broken);
if (components)
{
    size_t count = manifold_manifold_vec_length(components);
    for (size_t i = 0; i < count; i++)
    {
        ManifoldManifold* part = manifold_manifold_vec_get(manifold_alloc_manifold(), components, i);
        if (!part) continue;
        R3D_Mesh chunk = {0};
        if (R3D_UpdateMeshFromManifold(&chunk, part))
        {
            // Store 'chunk' in your game-owned chunk list; later draw and unload it.
            // Do not free this chunk here while your game still uses it.
        }
        manifold_delete_manifold(part);
    }
    manifold_delete_manifold_vec(components);
}
```

Each chunk is a normal GPU mesh. Manifold doesn't create rigid bodies or keep track of your application transforms. Set those up separately. If you want brick outside and concrete inside on each fragment, apply the triangle/material separation from Recipe 1 to each native part instead of uploading as one mesh.

## Quick choices

| Requirement | Recommended path |
| --- | --- |
| A slice with the **same** material on its new caps | `R3D_ManifoldSlicePlane()` |
| Custom cap albedo/normal/roughness/emission texture | Native `manifold_split_by_plane()` + classify cap triangles + two R3D meshes |
| Many holes at once | Native `manifold_batch_boolean()` + one `manifold_difference()` + one upload |
| One irregular deforming mesh | `manifold_refine_to_length()` + `manifold_warp()` |
| Multiple moving debris pieces | `manifold_decompose()` and create one or more R3D meshes per component |
| Precise texture mapping | Edit `R3D_MeshData` after native conversion, before GPU upload |

### API cheat sheet (the easy part)

```c
// Existing editable R3D meshes need linked CPU data:
R3D_LinkMeshData(&mesh, &meshData);
R3D_LinkMeshData(&cutter, &cutterData);
R3D_ManifoldResult r = R3D_ManifoldSubtract(&mesh, &cutter); // Also Union / Intersect.
R3D_UnloadManifoldResult(&r); // Frees extra pieces, NOT the primary mesh.
R3D_ManifoldResult s = R3D_ManifoldSlicePlane(&mesh, (Vector3){0,1,0}, 0);
R3D_UnloadManifoldResult(&s);
```

- Keep the original `R3D_MeshData` alive while linked; `R3D_UnloadMesh()` does not own/free it.
- For imported GLB editing, load with `R3D_IMPORT_RETAIN_MESH_DATA`.
- A newly generated Manifold cap can have **zero vertex alpha**—set it opaque before rendering.
- Local-space Boolean geometry is independent of `R3D_DrawMesh()`'s visual transform.
- Every `manifold_alloc_*`-created native object must be released by matching `manifold_delete_*`.
- `R3D_MANIFOLD_EMPTY_RESULT` is normal for nonoverlapping/overcut operations, not a geometry crash.

See also [`examples/manifold.c`](../examples/manifold.c), [`examples/manifold_operations.c`](../examples/manifold_operations.c), [Manifold's documentation](https://elalish.github.io/manifold/).
