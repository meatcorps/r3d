
/* r3d_point_cloud.c -- Thin batching wrapper around R3D instancing. */

#include <r3d/r3d_point_cloud.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>

R3D_PointCloud R3D_LoadPointCloud(int capacity)
{
    R3D_PointCloud cloud = {0};

    if (capacity <= 0 || capacity > INT32_MAX / R3D_POINT_SHAPE_COUNT)
    {
        return cloud;
    }

    cloud.capacity = capacity;

    size_t total = (size_t)capacity * R3D_POINT_SHAPE_COUNT;

    cloud.positions = calloc(total, sizeof(*cloud.positions));
    cloud.scales = calloc(total, sizeof(*cloud.scales));
    cloud.colors = calloc(total, sizeof(*cloud.colors));

    if (!cloud.positions || !cloud.scales || !cloud.colors)
    {
        R3D_UnloadPointCloud(&cloud);
        return (R3D_PointCloud){0};
    }

    cloud.meshes[R3D_POINT_CIRCLE] = R3D_GenMeshPoly(
        20, 0.5f, (Vector3){0, 0, 1}
    );

    cloud.meshes[R3D_POINT_SQUARE] = R3D_GenMeshQuad(
        1, 1, 1, 1, (Vector3){0, 0, 1}
    );

    cloud.meshes[R3D_POINT_DIAMOND] = R3D_GenMeshPoly(
        4, 0.5f, (Vector3){0, 0, 1}
    );

    for (int i = 0; i < R3D_POINT_SHAPE_COUNT; i++)
    {
        if (!R3D_IsMeshValid(cloud.meshes[i]))
        {
            R3D_UnloadPointCloud(&cloud);
            return (R3D_PointCloud){0};
        }

        cloud.meshes[i].shadowCastMode = R3D_SHADOW_CAST_DISABLED;

        cloud.instances[i] = R3D_LoadInstanceBuffer(
            capacity,
            R3D_INSTANCE_POSITION |
            R3D_INSTANCE_SCALE |
            R3D_INSTANCE_COLOR
        );

        if (cloud.instances[i].capacity != capacity)
        {
            R3D_UnloadPointCloud(&cloud);
            return (R3D_PointCloud){0};
        }
    }

    return cloud;
}

void R3D_UnloadPointCloud(R3D_PointCloud* cloud)
{
    if (!cloud)
    {
        return;
    }

    for (int i = 0; i < R3D_POINT_SHAPE_COUNT; i++)
    {
        if (cloud->instances[i].capacity > 0)
        {
            R3D_UnloadInstanceBuffer(cloud->instances[i]);
        }

        if (R3D_IsMeshValid(cloud->meshes[i]))
        {
            R3D_UnloadMesh(cloud->meshes[i]);
        }
    }

    free(cloud->positions);
    free(cloud->scales);
    free(cloud->colors);

    *cloud = (R3D_PointCloud){0};
}

bool R3D_UpdatePointCloud(R3D_PointCloud* cloud, const R3D_PointCloudPoint* points, int count)
{
    if (!cloud || cloud->capacity <= 0 ||
        count < 0 || count > cloud->capacity ||
        (count > 0 && !points))
    {
        return false;
    }

    int counts[R3D_POINT_SHAPE_COUNT] = {0};

    // Group the input points by shape.
    for (int i = 0; i < count; i++)
    {
        R3D_PointCloudPoint point = points[i];

        if (point.shape < 0 || point.shape >= R3D_POINT_SHAPE_COUNT ||
            !isfinite(point.position.x) ||
            !isfinite(point.position.y) ||
            !isfinite(point.position.z) ||
            !isfinite(point.size) ||
            point.size <= 0.0f)
        {
            return false;
        }

        int shape = (int)point.shape;

        size_t offset =
            (size_t)shape * (size_t)cloud->capacity +
            (size_t)counts[shape]++;

        cloud->positions[offset] = point.position;
        cloud->scales[offset] = (Vector3){
            point.size, point.size, 1.0f
        };
        cloud->colors[offset] = point.color;
    }

    // Upload three attribute streams for each nonempty shape.
    for (int i = 0; i < R3D_POINT_SHAPE_COUNT; i++)
    {
        int n = counts[i];

        if (n > 0)
        {
            size_t offset = (size_t)i * (size_t)cloud->capacity;

            R3D_UploadInstances(
                cloud->instances[i],
                R3D_INSTANCE_POSITION,
                0, n,
                cloud->positions + offset,
                true
            );

            R3D_UploadInstances(
                cloud->instances[i],
                R3D_INSTANCE_SCALE,
                0, n,
                cloud->scales + offset,
                true
            );

            R3D_UploadInstances(
                cloud->instances[i],
                R3D_INSTANCE_COLOR,
                0, n,
                cloud->colors + offset,
                true
            );
        }

        cloud->counts[i] = n;
    }

    return true;
}

void R3D_DrawPointCloud(const R3D_PointCloud* cloud, R3D_Material material)
{
    if (!cloud)
    {
        return;
    }

    for (int i = 0; i < R3D_POINT_SHAPE_COUNT; i++)
    {
        if (cloud->counts[i] > 0)
        {
            R3D_DrawMeshInstanced(
                cloud->meshes[i],
                material,
                cloud->instances[i],
                cloud->counts[i]
            );
        }
    }
}

R3D_Material R3D_GetDefaultPointCloudMaterial(void)
{
    R3D_Material material = R3D_GetDefaultMaterial();

    material.unlit = true;
    material.billboardMode = R3D_BILLBOARD_FRONT;
    material.cullMode = R3D_CULL_NONE;

    return material;
}
