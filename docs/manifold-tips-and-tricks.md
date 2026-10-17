# Manifold in R3D — Tips & Tricks

Practical recipes for runtime Boolean modeling, slicing and custom geometry operations using **R3D + Manifold's C API**.

This guide covers the optional integration on the `meatcorps/raylib6` branch. It assumes C, raylib 6 and `R3D_SUPPORT_MANIFOLD=ON`. For working visual examples, see [`examples/manifold_operations.c`](../examples/manifold_operations.c) and [`examples/manifold.c`](../examples/manifold.c).

## Build and include

~~~sh
git submodule update --init --recursive
cmake -S . -B build -DR3D_RAYLIB_VENDORED=ON -DR3D_ASSIMP_VENDORED=ON -DR3D_SUPPORT_MANIFOLD=ON
cmake --build build --target r3d_manifold_operations
~~~

~~~c
#include <r3d/r3d.h>
#include <r3d/r3d_manifold.h>
~~~

Manifold is **opt-in**. Normal R3D mesh uploads and drawing do not run Boolean code. The Manifold headers/API are available only in builds that enable the integration.

## Pick the right workflow

| You want to… | Use |
| --- | --- |
| Subtract / union / intersect two existing editable meshes | `R3D_ManifoldSubtract` / `Union` / `Intersect` |
| Cut a mesh into pieces with ready-to-render caps | `R3D_ManifoldSlicePlane` |
| Extrude a simple XY outline | `R3D_ManifoldExtrude` |
| Create a complex cutter, apply ten cuts, then upload once | Manifold's C API + `R3D_UpdateMeshFromManifold` |
| Control sophisticated UVs, materials, smoothing or topology | Explicit MeshData ↔ Manifold conversion and custom processing |

**Rule of thumb:** convenience functions do **one operation and one GPU update**. Native Manifold chaining keeps intermediate geometry off the GPU until you're done.

## Link CPU geometry once

Boolean convenience functions take two `R3D_Mesh` objects, not separate mesh-data arguments. They read CPU geometry through the optional, **non-owning** `mesh.meshData` pointer.

~~~c
R3D_MeshData wallData = R3D_GenMeshDataCube(3.0f, 2.0f, 0.4f);
R3D_MeshData toolData = R3D_GenMeshDataCube(0.8f, 0.8f, 0.8f);

R3D_Mesh wall = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, wallData, NULL);
R3D_Mesh tool = R3D_LoadMesh(R3D_PRIMITIVE_TRIANGLES, toolData, NULL);

R3D_LinkMeshData(&wall, &wallData);
R3D_LinkMeshData(&tool, &toolData);

// ...operations...

R3D_UnloadMesh(wall);
R3D_UnloadMesh(tool);
R3D_UnloadMeshData(wallData);
R3D_UnloadMeshData(toolData);
~~~

The link costs only a pointer; retaining CPU vertex/index buffers still costs RAM. `R3D_UnloadMesh()` does **not** free linked `R3D_MeshData`. Keep the linked data structure and its buffers alive until editing is finished; call `R3D_UnlinkMeshData(&wall)` if you want to free CPU data while keeping the GPU mesh.

**Don't copy an editable `R3D_Mesh` and assume you made an independent mesh.** Copying the struct also copies its GPU handles and CPU-data pointer, not its resources.

### Editing imported GLB models

~~~c
R3D_Model model = R3D_LoadModelEx("wall.glb", R3D_IMPORT_RETAIN_MESH_DATA);

// Retained model meshes are already linked to their model.meshData entries.
if (model.meshCount >= 2 && model.meshData)
{
    R3D_ManifoldResult result = R3D_ManifoldSubtract(&model.meshes[0], &model.meshes[1]);
    // Inspect result.status and use result.extraMeshes if needed.
    R3D_UnloadManifoldResult(&result);
}

// ...after all edits and rendering...
R3D_UnloadModel(model, true);
~~~

For lookup by mesh name, also pass `R3D_IMPORT_RETAIN_MESH_NAMES` and use `R3D_GetModelMesh()`. With no retain-data flag, a GPU-only imported mesh has no editable CPU geometry; reimporting with the flag is easier than implementing GPU readback.

Imported files can contain open surfaces, multiple submeshes and transforms. Manifold expects valid, consistently wound, closed solids for reliable 3D Boolean operations. The operation uses **mesh-local geometry**: a transform supplied only to `R3D_DrawMesh()` does not move the Boolean cutter. Transform the CPU geometry or the native Manifold cutter instead.

## One-line operations, including chaining

~~~c
R3D_ManifoldResult cut = R3D_ManifoldSubtract(&wall, &tool);

if (cut.status == R3D_MANIFOLD_OK)
{
    // wall's GPU geometry and linked wallData now contain the result.
    // cut.extraMeshes contains other disconnected components, if any.
}

R3D_UnloadManifoldResult(&cut);
~~~

Use `R3D_ManifoldUnion(&wall, &tool)` to add volume, or `R3D_ManifoldIntersect(&wall, &tool)` to keep only the overlap.

For another cut, call the convenience function again. It reads the **updated** linked `wallData`. You do not need to re-link the mesh.

~~~c
R3D_ManifoldResult first = R3D_ManifoldSubtract(&wall, &tool);
if (first.status == R3D_MANIFOLD_OK)
{
    // If the detached pieces are not needed, release them before the next edit.
    R3D_UnloadManifoldResult(&first);

    R3D_ManifoldResult second = R3D_ManifoldSubtract(&wall, &anotherTool);
    R3D_UnloadManifoldResult(&second);
}
else
{
    R3D_UnloadManifoldResult(&first);
}
~~~

This is convenient for occasional edits and editor actions. For many repeated cuts (e.g. generating destruction in one event), use the **native chain** below to avoid repeatedly converting and uploading.

## Slice a wall and keep the detached pieces

~~~c
// Plane through local Y=0; the first piece is on the normal-facing side.
R3D_ManifoldResult pieces = R3D_ManifoldSlicePlane(
    &wall, (Vector3){0.0f, 1.0f, 0.0f}, 0.0f
);

if (pieces.status == R3D_MANIFOLD_OK)
{
    // wall now holds the primary piece.
    for (int i = 0; i < pieces.extraCount; i++)
    {
        // Draw pieces.extraMeshes[i] with your material in an R3D render pass,
        // or give each piece its own transform/physics body.
    }
}

// Do not unload while you still use pieces.extraMeshes.
R3D_UnloadManifoldResult(&pieces);
~~~

`extraCount` excludes the primary mesh, and `extraMeshes`/`extraData` belong to the **result**, not the caller. Extra meshes have CPU links and may be edited again while the parent result lives. Do not individually unload them and then also unload the result.

For a straight plane split, Manifold supplies sealed caps. R3D applies basic planar UVs and opaque white vertex color to the exposed faces, so the existing material (such as a checkerboard or concrete texture) is immediately visible. Custom interior materials, triplanar mapping and fracture textures are application-specific.

A **thick cut** is different from a plane slice: subtract a cutter that passes right through the wall. The subtraction result can contain several disconnected solids, available as `extraMeshes`.

**Note:** piece ordering and the number of components are not a stable volume-based ranking. Use your own selection rule if gameplay depends on piece size or which side is retained.

## Extrude a simple outline

~~~c
Vector2 outline[] = {
    {-1.0f, -1.0f},
    { 1.0f, -1.0f},
    { 1.0f,  0.0f},
    { 0.0f,  0.0f},
    { 0.0f,  1.0f},
    {-1.0f,  1.0f}
};

R3D_Mesh shape = {0};
R3D_ManifoldResult result = R3D_ManifoldExtrude(&shape, outline, 6, 0.5f);

if (result.status == R3D_MANIFOLD_OK)
{
    // shape is now a regular GPU R3D mesh.
}

R3D_UnloadManifoldResult(&result);
if (R3D_IsMeshValid(shape)) R3D_UnloadMesh(shape);
~~~

The shortcut handles a **simple polygon in XY**, extruded along +Z. A new, zero-initialized GPU target does **not** automatically retain CPU data; repeated convenience Booleans require an existing CPU-data link. For extrusions with holes, twisting, custom profiles or multiple chained edits, prefer native Manifold cross sections/extrusion and explicitly manage the final CPU mesh.

## Native workflow: two cuts, one upload

This is the preferred path when you want many operations without repeated R3D ↔ Manifold conversions. It uses the **same existing R3D mesh types**—no persistent wrapper.

~~~c
static bool CutTwiceNative(R3D_Mesh* wall, const R3D_Mesh* cutter)
{
    if (!wall || !cutter || !wall->meshData || !cutter->meshData) return false;

    bool success = false;
    ManifoldMeshGL* wallGL = R3D_MeshDataToManifoldMeshGL(wall->meshData);
    ManifoldMeshGL* cutterGL = R3D_MeshDataToManifoldMeshGL(cutter->meshData);
    ManifoldManifold* base = NULL;
    ManifoldManifold* tool = NULL;
    ManifoldManifold* movedA = NULL;
    ManifoldManifold* movedB = NULL;
    ManifoldManifold* stepA = NULL;
    ManifoldManifold* stepB = NULL;

    if (!wallGL || !cutterGL) goto cleanup;

    base = manifold_of_meshgl(manifold_alloc_manifold(), wallGL);
    tool = manifold_of_meshgl(manifold_alloc_manifold(), cutterGL);
    if (!base || !tool || manifold_status(base) != MANIFOLD_NO_ERROR ||
        manifold_status(tool) != MANIFOLD_NO_ERROR) goto cleanup;

    movedA = manifold_translate(manifold_alloc_manifold(), tool, 0.5, 0.0, 0.0);
    movedB = manifold_translate(manifold_alloc_manifold(), tool, -0.5, 0.0, 0.0);
    if (!movedA || !movedB) goto cleanup;

    stepA = manifold_difference(manifold_alloc_manifold(), base, movedA);
    if (!stepA || manifold_status(stepA) != MANIFOLD_NO_ERROR) goto cleanup;

    stepB = manifold_difference(manifold_alloc_manifold(), stepA, movedB);
    if (!stepB || manifold_status(stepB) != MANIFOLD_NO_ERROR ||
        manifold_is_empty(stepB)) goto cleanup;

    // Converts, uploads, updates linked CPU data and releases temporary R3D data.
    success = R3D_UpdateMeshFromManifold(wall, stepB);

cleanup:
    if (stepB) manifold_delete_manifold(stepB);
    if (stepA) manifold_delete_manifold(stepA);
    if (movedB) manifold_delete_manifold(movedB);
    if (movedA) manifold_delete_manifold(movedA);
    if (tool) manifold_delete_manifold(tool);
    if (base) manifold_delete_manifold(base);
    if (cutterGL) manifold_delete_meshgl(cutterGL);
    if (wallGL) manifold_delete_meshgl(wallGL);
    return success;
}
~~~

Other useful native combinations:

- **Compound cutter:** `manifold_union(cutterA, cutterB)`, then `manifold_difference(wall, combinedCutter)`.
- **Angled / translated cutter:** `manifold_rotate`, `manifold_scale` and `manifold_translate` before subtracting.
- **Keep overlapping volume:** `manifold_intersection`.
- **Plane cut retaining both halves:** `manifold_split_by_plane`.
- **Break a result into individual connected solids:** `manifold_decompose`.

Each returned Manifold object is caller-owned. Free it with the corresponding `manifold_delete_*` function. The native `R3D_UpdateMeshFromManifold()` uploads one resulting solid as one R3D mesh, even if that solid contains disconnected components; if you need separate R3D meshes, decompose first or use the convenience operations.

## Rendering, UVs and materials

Manifold handles **geometry**. R3D handles mesh packing, texture coordinates, tangents and rendering. The convenience slice gives newly exposed planar faces basic UVs and opaque colors, preserving the original material. It is not an automatic UV unwrap for every kind of Boolean-generated face.

Tips for destruction effects:

- Prepare a closed, triangulated wall mesh; use several closed cutters for impacts, holes or cracks.
- Use the same R3D material on each piece for a quick result. For exposed brick/concrete aggregate or different interior materials, apply your own mapping/material workflow.
- For animated chunks, use an individual transform per returned `R3D_Mesh`; geometry operations themselves do not create physics bodies.
- Perform geometry edits at **events**, not every render frame. Repeated conversions, normal/tangent generation and buffer reallocations cost CPU/GPU time.

## Troubleshooting and ownership checklist

| Symptom | Check |
| --- | --- |
| `R3D_MANIFOLD_INVALID_INPUT` | Both meshes have linked, valid CPU data and triangle primitives |
| `R3D_MANIFOLD_EMPTY_RESULT` | There is no remaining/overlapping volume; this is often an expected outcome |
| `R3D_MANIFOLD_NATIVE_ERROR` | Check `result.manifoldError`; source solids may be open/non-manifold or invalid |
| A cap is invisible | Inspect vertex **alpha** as well as normals/culling; zero-alpha generated faces can look missing |
| UVs on new surfaces look wrong | Simple cap projection is only applied by `R3D_ManifoldSlicePlane`; custom Boolean faces may need mapping |
| Another operation does not use the edited geometry | Ensure the target is linked and its CPU data is still alive |
| Detached pieces disappear after a new operation | The old `R3D_ManifoldResult` was unloaded; its extra meshes are owned by that result |
| Imported GLB cannot be edited | Load with `R3D_IMPORT_RETAIN_MESH_DATA` and check that the selected mesh is a valid solid |

Keep in mind:

1. The **primary** `R3D_Mesh` remains caller-owned; `R3D_UnloadManifoldResult` frees only detached pieces and their CPU data.
2. Linked CPU buffers remain owned by their original `R3D_MeshData` or `R3D_Model`. Never free them while editing through that link.
3. Check `result.status` before using a result; an empty result is not automatically a Manifold error.
4. Geometry operations work in **mesh-local space**. World-space collision queries, physics bodies and custom rendering materials are separate application responsibilities.
5. GPU upload failure is not guaranteed to be fully transactional by the current R3D mesh allocator. Validate inputs and handle error statuses before treating an edit as committed.

## Related code

- [Convenience API and ownership](../include/r3d/r3d_manifold.h)
- [Implementation, conversions and planar cap mapping](../src/r3d_manifold.c)
- [Interactive Boolean/extrude/slice example](../examples/manifold_operations.c)
- [Lower-level MeshGL round-trip example](../examples/manifold.c)
- [Official Manifold documentation](https://elalish.github.io/manifold/)
