# Phase 5 — Procedural Geometry Showroom

## Purpose

The procedural showroom validates the R3D text-to-mesh pipeline
using the normal rendering path.

The implementation combines:

- stb_truetype font and glyph outline extraction
- reusable R3D_TextFont resources
- Manifold cross-section and solid generation
- optional geometry smoothing and refinement
- R3D_MeshData CPU geometry
- R3D_Mesh GPU rendering

The showroom is intentionally separate from the geometry library.

## Running the showroom

Requires R3D_SUPPORT_MANIFOLD=ON.

Build the r3d_procedural_showroom target.

Controls:

- LEFT / RIGHT: change exhibit
- F: change font
- S: toggle smooth edges
- U: automatically change text and fonts
- R: regenerate meshes
- B: perform 12 live update samples
- O: toggle orbital camera

The title and information label are generated as real 3D meshes.

The performance overlay uses ordinary 2D text to avoid
regenerating geometry merely to display changing timings.

## Font resource ownership

R3D_LoadTextFontFromFile() owns its font data.

R3D_LoadTextFontFromMemory() copies the supplied byte buffer.
The caller may release the original buffer immediately.

R3D_TextFont owns:
- copied font bytes
- stb_truetype font information
- cached glyph outlines

R3D_UnloadTextFont() releases these resources.

R3D_TextFont is an owning handle, not a reference-counted
resource. Copies of the handle must not be unloaded independently.

Do not use the font after unloading it.

## Mesh ownership

R3D_GenMeshDataText() returns caller-owned CPU geometry.

The caller releases it using R3D_UnloadMeshData().

R3D_LoadMesh() uploads that geometry to GPU buffers.

R3D_UpdateMesh() updates an existing GPU mesh using newly
generated CPU geometry.

CPU mesh data can be released after the GPU upload completes.

R3D_UnloadMesh() releases GPU mesh resources.

Font resources and generated meshes have independent lifetimes.

## Caching policy

Current caching:
- font data stays loaded
- glyph outlines are cached by glyph index
- repeated GPU updates reuse existing mesh storage where possible

Not implemented:
- global mesh cache
- automatic text-to-mesh result cache
- cross-section cache
- geometry caching by font and settings

These should only be considered when measured regeneration cost
and repeated-use frequency justify their memory overhead.

A potential future mesh cache key would require:
- font resource identity
- complete UTF-8 text
- all text mesh settings

Cache entries must have explicit ownership and eviction rules.

## Benchmarks

The headless benchmark measures:

1. Font loading
2. First geometry generation with an empty glyph cache
3. Full glyph preloading
4. Repeated generation with warm glyph data
5. Changing text using the same font
6. Geometry smoothing and refinement

Results include:
- average generation time
- minimum generation time
- maximum generation time
- vertex counts
- triangle counts

The benchmark uses CPU-side wall-clock timing.

It does not measure GPU execution.

The interactive showroom separately times R3D_LoadMesh()
or R3D_UpdateMesh() calls.

These are upload/submission timings, not GPU completion timings.

Frame rate should be measured independently.

## Thread safety

The current R3D_TextFont glyph cache is mutable.

Concurrent generation using the same font resource is not
guaranteed safe while glyphs may be added to the cache.

The current implementation is suitable for serial generation.

For worker-thread usage:
- keep font resources alive until generation completes
- avoid concurrent cache mutations
- publish CPU mesh data through a synchronized handoff
- upload GPU meshes on the rendering thread

R3D_GenMeshDataNormals() uses shared scratch memory and is not
thread-safe. The text generator normally receives normals
from Manifold, avoiding that fallback.

No general multithreaded guarantee is claimed for this feature.

## Text limitations

- UTF-8 decoding is supported.
- Basic font kerning is supported.
- Multiline text and alignment are supported.
- Complex script shaping is not supported.
- Glyph substitution, ligatures and bidirectional layout
  are not provided by a shaping engine.
- Font files should be trusted input.

Empty strings or unsupported generation conditions may
produce empty mesh data.

## Rounded geometry

bevelSmoothness=0 disables smoothing.

Positive values use Manifold SmoothOut and Refine.

This is not a constant-radius bevel implementation.

Actual geometry is generated, which can affect silhouettes,
lighting and shadows.

Subdivisions significantly increase triangle count.

Rounded text should generally be generated once and reused.

## Validation

Run manifold_text_mesh through CTest.

Inspect:
- front and rear surfaces
- side surfaces
- counter holes in O, B and 8
- sharp and curved fonts
- different extrusion depths
- hard and smoothed edges
- repeated GPU updates
- font switching

For resource-leak validation, repeat generation and unloading
under a memory profiler or address sanitizer.

A stable memory footprint during repeated updates is expected
after font caches and GPU storage have reached their working
capacities.

Any continuing unexplained memory growth must be investigated
before claiming Phase 5 complete.

## Performance decision

No additional finished-mesh cache is introduced in Phase 5
without actual measurement evidence.

The showroom and benchmark provide that evidence.