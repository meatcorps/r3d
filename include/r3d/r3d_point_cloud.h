
/* r3d_point_cloud.h -- Instanced camera-facing point clouds. */
#ifndef R3D_POINT_CLOUD_H
#define R3D_POINT_CLOUD_H

#include "r3d_instance.h"
#include "r3d_mesh.h"
#include "r3d_material.h"
#include "r3d_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

    typedef enum R3D_PointShape {
        R3D_POINT_CIRCLE,
        R3D_POINT_SQUARE,
        R3D_POINT_DIAMOND,
        R3D_POINT_SHAPE_COUNT
    } R3D_PointShape;

    typedef struct R3D_PointCloudPoint {
        Vector3 position;
        Color color;
        float size;                 /* Diameter in world units. */
        R3D_PointShape shape;
    } R3D_PointCloudPoint;

    /*
     * Owns the shape meshes, instance buffers and CPU staging arrays.
     * Input point arrays remain caller-owned.
     */
    typedef struct R3D_PointCloud {
        R3D_Mesh meshes[R3D_POINT_SHAPE_COUNT];
        R3D_InstanceBuffer instances[R3D_POINT_SHAPE_COUNT];
        Vector3* positions;
        Vector3* scales;
        Color* colors;
        int counts[R3D_POINT_SHAPE_COUNT];
        int capacity;
    } R3D_PointCloud;

    /* Create after R3D_Init. Capacity is the maximum live point count. */
    R3DAPI R3D_PointCloud R3D_LoadPointCloud(int capacity);

    /* Releases all GPU and CPU resources owned by the cloud. */
    R3DAPI void R3D_UnloadPointCloud(R3D_PointCloud* cloud);

    /* Replaces the visible points without creating new GPU buffers. */
    R3DAPI bool R3D_UpdatePointCloud(R3D_PointCloud* cloud, const R3D_PointCloudPoint* points, int count);

    /* Draw between R3D_Begin() and R3D_End(). */
    R3DAPI void R3D_DrawPointCloud(const R3D_PointCloud* cloud, R3D_Material material);

    /* Recommended unlit, camera-facing point material. */
    R3DAPI R3D_Material R3D_GetDefaultPointCloudMaterial(void);

#ifdef __cplusplus
}
#endif

#endif // R3D_POINT_CLOUD_H
