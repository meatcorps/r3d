/* r3d_importer.c -- R3D Importer Module
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_importer.h>
#include <r3d_config.h>
#include <raylib.h>
#include <stddef.h>

#ifdef R3D_SUPPORT_ASSIMP
#   include "./importer/r3d_importer_internal.h"
#   include "./common/r3d_helper.h"
#   include <assimp/cimport.h>
#   include <assimp/light.h>
#   include <assimp/metadata.h>
#   include <raymath.h>
#   include <limits.h>
#   include <math.h>
#   include <stdio.h>
#   include <string.h>
#   include "./common/r3d_math.h"
#endif

// ========================================
// INTERNAL CONSTANTS
// ========================================

#define POST_PROCESS_BASELINE           \
    aiProcess_CalcTangentSpace      |   \
    aiProcess_JoinIdenticalVertices |   \
    aiProcess_Triangulate           |   \
    aiProcess_GenUVCoords           |   \
    aiProcess_SortByPType           |   \
    aiProcess_FlipUVs               |   \
    aiProcess_RemoveRedundantMaterials

// ========================================
// PRIVATE FUNCTIONS
// ========================================

#ifdef R3D_SUPPORT_ASSIMP

static enum aiPostProcessSteps build_flags(R3D_ImportFlags flags)
{
    enum aiPostProcessSteps aiFlags = POST_PROCESS_BASELINE;

    aiFlags |= R3D_BIT_ANY(flags, R3D_IMPORT_SMOOTH_NORMALS)
        ? aiProcess_GenSmoothNormals
        : aiProcess_GenNormals;

    if (R3D_BIT_ANY(flags, R3D_IMPORT_OPTIMIZE_MESH))
    {
        aiFlags |= aiProcess_ImproveCacheLocality | aiProcess_SplitLargeMeshes;
    }

    if (R3D_BIT_ANY(flags, R3D_IMPORT_VALIDATE_DATA))
    {
        aiFlags |= aiProcess_FindDegenerates | aiProcess_FindInvalidData;
    }

    return aiFlags;
}

static void determine_importer_name(char* outName, size_t outSize, const struct aiScene* scene, const char* hint)
{
    if (!outName || outSize == 0) return;

    if (scene && scene->mMetaData)
    {
        struct aiMetadata* meta = scene->mMetaData;
        for (unsigned int i = 0; i < meta->mNumProperties; i++)
        {
            if ((strcmp(meta->mKeys[i].data, "SourceAsset_Filename") == 0 ||
                 strcmp(meta->mKeys[i].data, "FileName") == 0) &&
                meta->mValues[i].mType == AI_AISTRING)
            {
                struct aiString* str = meta->mValues[i].mData;
                const char* filename = strrchr(str->data, '/');
                if (!filename) filename = strrchr(str->data, '\\');
                filename = filename ? filename + 1 : str->data;
                r3d_string_format(outName, outSize, "memory data (%s)", filename);
                return;
            }
        }
    }

    if (hint && hint[0] != '\0')
    {
        r3d_string_format(outName, outSize, "memory data (%s)", hint);
        return;
    }

    r3d_string_format(outName, outSize, "memory data");
}

static void build_bone_mapping(R3D_Importer* importer)
{
    int totalBones = 0;
    for (uint32_t meshIdx = 0; meshIdx < importer->scene->mNumMeshes; meshIdx++)
    {
        const struct aiMesh* mesh = importer->scene->mMeshes[meshIdx];
        if (mesh && mesh->mNumBones) totalBones += mesh->mNumBones;
    }

    if (totalBones == 0)
    {
        importer->bones.array = NULL;
        importer->bones.head = NULL;
        importer->bones.count = 0;
        return;
    }

    importer->bones.array = r3d_zalloc(totalBones * sizeof(r3d_importer_bone_entry_t));
    importer->bones.head = NULL;
    importer->bones.count = 0;

    for (uint32_t meshIdx = 0; meshIdx < importer->scene->mNumMeshes; meshIdx++)
    {
        const struct aiMesh* mesh = importer->scene->mMeshes[meshIdx];
        if (!mesh || !mesh->mNumBones) continue;

        for (uint32_t boneIdx = 0; boneIdx < mesh->mNumBones; boneIdx++)
        {
            const struct aiBone* bone = mesh->mBones[boneIdx];
            if (!bone) continue;

            const char* boneName = bone->mName.data;

            r3d_importer_bone_entry_t* entry = NULL;
            HASH_FIND_STR(importer->bones.head, boneName, entry);
            if (entry != NULL) continue;

            entry = &importer->bones.array[importer->bones.count];

            strncpy(entry->name, boneName, sizeof(entry->name) - 1);
            entry->name[sizeof(entry->name) - 1] = '\0';
            entry->index = importer->bones.count;

            HASH_ADD_STR(importer->bones.head, name, entry);
            importer->bones.count++;
        }
    }

    if (importer->bones.count > 0)
    {
        R3D_TRACELOG(LOG_DEBUG, "Built bone mapping with %d bones", importer->bones.count);
    }
}

static void find_light_node(const struct aiNode* node, const char* name, Matrix parentTransform, const struct aiNode** resultNode, Matrix* resultTransform, int* matchCount)
{
    if (!node || *matchCount > 1)
        return;

    Matrix local =
        r3d_importer_cast(node->mTransformation);

    Matrix global =
        MatrixMultiply(local, parentTransform);

    if (strcmp(node->mName.data, name) == 0)
    {
        (*matchCount)++;

        if (*matchCount == 1)
        {
            *resultNode = node;
            *resultTransform = global;
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        find_light_node(node->mChildren[i], name, global, resultNode, resultTransform, matchCount);

        if (*matchCount > 1)
            break;
    }
}

static bool get_light_range(const struct aiNode* node, float* result)
{
    if (!node || !node->mMetaData || !result)
        return false;

    const struct aiMetadata* metadata = node->mMetaData;

    for (unsigned int i = 0;
         i < metadata->mNumProperties;
         i++)
    {
        if (strcmp(
            metadata->mKeys[i].data,
            "PBR_LightRange") != 0)
        {
            continue;
        }

        const struct aiMetadataEntry* entry =
            &metadata->mValues[i];

        if (!entry->mData)
            return false;

        float value;

        switch (entry->mType)
        {
            case AI_FLOAT:
                value = *(const float*)entry->mData;
                break;

            case AI_DOUBLE:
                value = (float)*(const double*)entry->mData;
                break;

            default:
                return false;
        }

        if (!isfinite(value) || value <= 0.0f)
            return false;

        *result = value;
        return true;
    }

    return false;
}

#endif // R3D_SUPPORT_ASSIMP

bool r3d_importer_load_lights(const R3D_Importer* importer, R3D_Model* model)
{
#ifdef R3D_SUPPORT_ASSIMP

    if (!importer || !importer->scene || !model)
        return false;

    const struct aiScene* scene = importer->scene;

    if (scene->mNumLights == 0)
        return true;

    if (!scene->mLights ||
        scene->mNumLights > INT_MAX)
    {
        return false;
    }

    R3D_ImportedLight* lights = r3d_zalloc(
        (size_t)scene->mNumLights *
        sizeof(R3D_ImportedLight)
    );

    if (!lights)
        return false;

    for (unsigned int i = 0; i < scene->mNumLights; i++)
    {
        const struct aiLight* source = scene->mLights[i];

        if (!source)
        {
            r3d_free(lights);
            return false;
        }

        R3D_ImportedLight* dest = &lights[i];

        snprintf(
            dest->name,
            sizeof(dest->name),
            "%s",
            source->mName.length > 0
                ? source->mName.data
                : ""
        );

        dest->sourceType = (int)source->mType;

        switch (source->mType)
        {
            case aiLightSource_DIRECTIONAL:
                dest->type = R3D_LIGHT_DIR;
                break;

            case aiLightSource_POINT:
                dest->type = R3D_LIGHT_OMNI;
                break;

            case aiLightSource_SPOT:
                dest->type = R3D_LIGHT_SPOT;
                break;

            default:
                dest->type = R3D_LIGHT_TYPE_COUNT;
                break;
        }

        dest->position =
            r3d_importer_cast(source->mPosition);

        dest->direction =
            r3d_importer_cast(source->mDirection);

        /*
         * Assimp glTF2 imports color multiplied
         * by physical intensity.
         *
         * Keep floating-point values intact.
         */
        dest->colorIntensity = (Vector3) {
            source->mColorDiffuse.r,
            source->mColorDiffuse.g,
            source->mColorDiffuse.b
        };

        dest->attenuationConstant =
            source->mAttenuationConstant;

        dest->attenuationLinear =
            source->mAttenuationLinear;

        dest->attenuationQuadratic =
            source->mAttenuationQuadratic;

        if (dest->type == R3D_LIGHT_SPOT)
        {
            dest->innerConeAngle =
                source->mAngleInnerCone * RAD2DEG;

            dest->outerConeAngle =
                source->mAngleOuterCone * RAD2DEG;
        }

        /*
         * Resolve the node belonging to this light.
         * Assimp's glTF2 importer associates light
         * definitions with scene-node names.
         */
        const struct aiNode* node = NULL;

        Matrix nodeTransform = MatrixIdentity();
        int matches = 0;

        if (source->mName.length > 0)
        {
            find_light_node(
                scene->mRootNode,
                source->mName.data,
                MatrixIdentity(),
                &node,
                &nodeTransform,
                &matches
            );
        }

        if (matches == 1)
        {
            dest->nodeResolved = 1;

            dest->position = r3d_vector3_transform(
                dest->position,
                &nodeTransform
            );

            /*
             * Extract rotation separately so a
             * nonuniform node scale doesn't distort
             * the light direction.
             */
            Vector3 translation;
            Vector3 scale;
            Quaternion rotation;

            MatrixDecompose(
                nodeTransform,
                &translation,
                &rotation,
                &scale
            );

            Vector3 direction = Vector3RotateByQuaternion(
                dest->direction,
                rotation
            );

            if (Vector3LengthSqr(direction) > 1e-12f)
            {
                dest->direction =
                    Vector3Normalize(direction);
            }

            if (get_light_range(node, &dest->range))
            {
                dest->hasRange = 1;
            }
        }
        else
        {
            R3D_TRACELOG(
                LOG_WARNING,
                "Light '%s': scene node resolution failed (%d matches)",
                source->mName.data,
                matches
            );
        }
    }

    model->lights = lights;
    model->lightCount = (int)scene->mNumLights;

    R3D_TRACELOG(
        LOG_INFO,
        "R3D: Imported %d light definitions",
        model->lightCount
    );

    return true;

#else
    (void)importer;
    (void)model;
    return false;
#endif
}

// ========================================
// PUBLIC FUNCTIONS
// ========================================

R3D_Importer* R3D_LoadImporter(const char* filePath, R3D_ImportFlags flags)
{
#ifdef R3D_SUPPORT_ASSIMP
    enum aiPostProcessSteps aiFlags = build_flags(flags);
    const struct aiScene* scene = aiImportFile(filePath, aiFlags);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
    {
        R3D_TRACELOG(LOG_ERROR, "Assimp failed to load '%s': %s", filePath, aiGetErrorString());
        return NULL;
    }

    R3D_Importer* importer = r3d_zalloc(sizeof(*importer));
    importer->scene = scene;
    importer->flags = flags;

    strncpy(importer->name, filePath, sizeof(importer->name) - 1);
    importer->name[sizeof(importer->name) - 1] = '\0';

    build_bone_mapping(importer);

    R3D_TRACELOG(LOG_INFO, "Importer loaded successfully: '%s'", filePath);
    return importer;

#else
    R3D_TRACELOG(LOG_WARNING, "Cannot load '%s': built without Assimp support", filePath);
    return NULL;

#endif // R3D_SUPPORT_ASSIMP
}

R3D_Importer* R3D_LoadImporterFromMemory(const void* data, unsigned int size, const char* hint, R3D_ImportFlags flags)
{
#ifdef R3D_SUPPORT_ASSIMP
    enum aiPostProcessSteps aiFlags = build_flags(flags);
    const struct aiScene* scene = aiImportFileFromMemory(data, size, aiFlags, hint);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE))
    {
        if (hint && hint[0] != '\0')
        {
            R3D_TRACELOG(LOG_ERROR, "Assimp failed to load memory asset '%s': %s", hint, aiGetErrorString());
        }
        else
        {
            R3D_TRACELOG(LOG_ERROR, "Assimp failed to load memory asset: %s", aiGetErrorString());
        }
        return NULL;
    }

    R3D_Importer* importer = r3d_zalloc(sizeof(*importer));
    importer->scene = scene;
    importer->flags = flags;

    determine_importer_name(importer->name, sizeof(importer->name), scene, hint);
    build_bone_mapping(importer);

    if (hint && hint[0] != '\0')
    {
        R3D_TRACELOG(LOG_INFO, "Importer loaded successfully from memory: '%s'", hint);
    }
    else
    {
        R3D_TRACELOG(LOG_INFO, "Importer loaded successfully from memory");
    }

    return importer;

#else
    if (hint && hint[0] != '\0')
    {
        R3D_TRACELOG(LOG_WARNING, "Cannot load '%s' from memory: built without Assimp support", hint);
    }
    else
    {
        R3D_TRACELOG(LOG_WARNING, "Cannot load asset from memory: built without Assimp support");
    }

    return NULL;

#endif // R3D_SUPPORT_ASSIMP
}

void R3D_UnloadImporter(R3D_Importer* importer)
{
#ifdef R3D_SUPPORT_ASSIMP
    if (!importer) return;

    HASH_CLEAR(hh, importer->bones.head);

    if (importer->bones.array)
    {
        r3d_free(importer->bones.array);
    }

    aiReleaseImport(importer->scene);
    r3d_free(importer);

#else
    (void)importer;

#endif // R3D_SUPPORT_ASSIMP
}
