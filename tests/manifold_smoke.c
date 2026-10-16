#include <manifold/manifoldc.h>
#include <stdio.h>

int main(void)
{
    ManifoldManifold* cube = manifold_cube(manifold_alloc_manifold(), 2.0, 2.0, 2.0, 1);
    ManifoldManifold* cutter = manifold_cube(manifold_alloc_manifold(), 1.0, 1.0, 3.0, 1);
    ManifoldManifold* result = manifold_difference(manifold_alloc_manifold(), cube, cutter);

    int success = 0;

    if (cube && cutter && result)
    {
        if (manifold_status(result) == MANIFOLD_NO_ERROR && !manifold_is_empty(result))
        {
            ManifoldMeshGL* mesh = manifold_get_meshgl(manifold_alloc_meshgl(), result);

            if (mesh)
            {
                size_t triangles = manifold_meshgl_num_tri(mesh);
                double volume = manifold_volume(result);

                printf("Manifold triangles: %zu\n", triangles);
                printf("Manifold volume: %.6f\n", volume);

                success = triangles > 0 && volume > 5.99 && volume < 6.01;

                manifold_delete_meshgl(mesh);
            }
        }
    }

    if (result) manifold_delete_manifold(result);
    if (cutter) manifold_delete_manifold(cutter);
    if (cube) manifold_delete_manifold(cube);

    if (!success)
    {
        fprintf(stderr, "Manifold smoke test failed\n");
        return 1;
    }

    printf("Manifold C API OK\n");
    return 0;
}