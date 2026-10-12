/* r3d_animation_lookat.c -- R3D Animation Look at Module.
*
* Copyright (c) 2025-2026 Dennis Steffen
*
* This software is provided 'as-is', without any express or implied warranty.
* For conditions of distribution and use, see accompanying LICENSE file.
*/

#include <r3d/r3d_animation_lookat.h>
#include <raymath.h>
#include <math.h>

static bool finite_vec3(Vector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool normalize_safe(Vector3* v)
{
    float lengthSq = Vector3LengthSqr(*v);

    if (!isfinite(lengthSq) || lengthSq <= 1e-12f) return false;

    *v = Vector3Scale(*v, 1.0f / sqrtf(lengthSq));
    return true;
}

static Vector3 transform_direction(Vector3 direction, Matrix matrix)
{
    Vector3 origin = Vector3Transform(Vector3Zero(), matrix);
    return Vector3Subtract(Vector3Transform(direction, matrix), origin);
}

static Vector3 project_perpendicular(Vector3 vector, Vector3 normal)
{
    return Vector3Subtract(vector, Vector3Scale(normal, Vector3DotProduct(vector, normal)));
}

static Matrix rotation_basis(Vector3 forward, Vector3 up)
{
    Vector3 right = Vector3Normalize(Vector3CrossProduct(up, forward));
    Vector3 correctedUp = Vector3CrossProduct(forward, right);

    return (Matrix) {
        right.x, correctedUp.x, forward.x, 0.0f,
        right.y, correctedUp.y, forward.y, 0.0f,
        right.z, correctedUp.z, forward.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
}

bool R3D_ApplyAnimationBoneLookAt(R3D_AnimationPlayer* player, int boneIndex, Vector3 targetModel, R3D_BoneLookAtParams params)
{
    if (!player || !player->skeleton.bones) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;

    if (!finite_vec3(targetModel) ||
        !finite_vec3(params.localForward) ||
        !finite_vec3(params.localUp) ||
        !finite_vec3(params.modelUp) ||
        !isfinite(params.weight) ||
        !isfinite(params.maxAngleRadians) ||
        params.weight < 0.0f || params.weight > 1.0f ||
        params.maxAngleRadians < 0.0f)
    {
        return false;
    }

    Vector3 localForward = params.localForward;
    Vector3 localUp = params.localUp;
    Vector3 referenceUp = params.modelUp;

    if (!normalize_safe(&localForward) ||
        !normalize_safe(&localUp) ||
        !normalize_safe(&referenceUp))
    {
        return false;
    }

    // Forward and up must describe two distinct local directions.
    Vector3 localRight = Vector3CrossProduct(localUp, localForward);
    if (!normalize_safe(&localRight)) return false;

    Matrix localPose;
    if (!R3D_GetAnimationBoneLocalMatrix(player, boneIndex, &localPose)) return false;

    const int parentIndex = player->skeleton.bones[boneIndex].parent;
    if (parentIndex < -1 || parentIndex >= player->skeleton.boneCount) return false;
    if (parentIndex == boneIndex) return false;

    Matrix parentPose = player->skeleton.rootBind;

    if (parentIndex >= 0)
    {
        if (!R3D_GetAnimationBoneModelMatrix(player, parentIndex, &parentPose)) return false;
    }

    // We need a valid model-to-parent conversion for the target.
    float determinant = MatrixDeterminant(parentPose);
    if (!isfinite(determinant) || fabsf(determinant) <= 1e-8f) return false;

    Matrix inverseParent = MatrixInvert(parentPose);

    Vector3 targetParent = Vector3Transform(targetModel, inverseParent);
    Vector3 boneOrigin = {localPose.m12, localPose.m13, localPose.m14};

    Vector3 desiredForward = Vector3Subtract(targetParent, boneOrigin);

    if (!normalize_safe(&desiredForward)) return false;

    // Read the authored bone directions from the actual local matrix.
    // This also accounts for existing nonuniform scale and shear.
    Vector3 currentForward = transform_direction(localForward, localPose);
    Vector3 currentUp = transform_direction(localUp, localPose);

    if (!normalize_safe(&currentForward)) return false;

    currentUp = project_perpendicular(currentUp, currentForward);
    if (!normalize_safe(&currentUp)) return false;

    // Resolve the desired roll relative to the model-space up vector.
    Vector3 desiredUp = transform_direction(referenceUp, inverseParent);
    desiredUp = project_perpendicular(desiredUp, desiredForward);

    // Handle a target directly above/below the reference up direction.
    if (!normalize_safe(&desiredUp))
    {
        desiredUp = project_perpendicular(currentUp, desiredForward);

        if (!normalize_safe(&desiredUp))
        {
            Vector3 currentRight = Vector3CrossProduct(currentUp, currentForward);
            desiredUp = project_perpendicular(currentRight, desiredForward);

            if (!normalize_safe(&desiredUp)) return false;
        }
    }

    // Construct an orthonormal frame for the current and desired axes.
    Matrix currentBasis = rotation_basis(currentForward, currentUp);
    Matrix desiredBasis = rotation_basis(desiredForward, desiredUp);

    // Rotation mapping current frame to desired frame, in parent space.
    Matrix correctionMatrix = MatrixMultiply(MatrixTranspose(currentBasis), desiredBasis);
    Quaternion correction = QuaternionNormalize(QuaternionFromMatrix(correctionMatrix));

    if (!isfinite(correction.x) || !isfinite(correction.y) ||
        !isfinite(correction.z) || !isfinite(correction.w))
    {
        return false;
    }

    // Use the shortest quaternion arc.
    if (correction.w < 0.0f)
    {
        correction = QuaternionScale(correction, -1.0f);
    }

    float angle = 2.0f * acosf(Clamp(correction.w, 0.0f, 1.0f));
    float amount = params.weight;

    // Angle limits apply to the entire correction, including roll.
    if (params.maxAngleRadians > 0.0f && angle > params.maxAngleRadians)
    {
        amount *= params.maxAngleRadians / angle;
    }

    if (amount <= 0.0f) return true;

    Quaternion weightedCorrection = QuaternionSlerp(QuaternionIdentity(), correction, amount);
    Matrix rotatedLocal = MatrixMultiply(localPose, QuaternionToMatrix(weightedCorrection));

    // Rotate the 3x3 basis, not the translation relative to the parent.
    rotatedLocal.m12 = localPose.m12;
    rotatedLocal.m13 = localPose.m13;
    rotatedLocal.m14 = localPose.m14;

    // Retain the original affine bottom row.
    rotatedLocal.m3 = localPose.m3;
    rotatedLocal.m7 = localPose.m7;
    rotatedLocal.m11 = localPose.m11;
    rotatedLocal.m15 = localPose.m15;

    return R3D_SetAnimationBoneLocalMatrix(player, boneIndex, rotatedLocal);
}

bool R3D_ApplyAnimationBoneAim(R3D_AnimationPlayer* player, int boneIndex, Vector3 targetModel, R3D_BoneAimParams params)
{
    if (!player || !player->skeleton.bones) return false;
    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount) return false;

    if (!finite_vec3(targetModel) || !finite_vec3(params.localForward) ||
        !finite_vec3(params.localUp) || !isfinite(params.weight) ||
        !isfinite(params.maxAngleRadians) ||
        params.weight < 0.0f || params.weight > 1.0f ||
        params.maxAngleRadians < 0.0f)
    {
        return false;
    }

    Vector3 localForward = params.localForward;
    Vector3 localUp = params.localUp;

    if (!normalize_safe(&localForward) || !normalize_safe(&localUp)) return false;

    Vector3 localSide = Vector3CrossProduct(localForward, localUp);
    if (!normalize_safe(&localSide)) return false;

    Matrix localPose;
    if (!R3D_GetAnimationBoneLocalMatrix(player, boneIndex, &localPose)) return false;

    const int parentIndex = player->skeleton.bones[boneIndex].parent;
    if (parentIndex < -1 || parentIndex >= player->skeleton.boneCount || parentIndex == boneIndex) return false;

    Matrix parentPose = player->skeleton.rootBind;
    if (parentIndex >= 0 && !R3D_GetAnimationBoneModelMatrix(player, parentIndex, &parentPose)) return false;

    const float determinant = MatrixDeterminant(parentPose);
    if (!isfinite(determinant) || fabsf(determinant) <= 1e-8f) return false;

    // Work in parent space; this also handles rotated/nonuniform parents.
    const Matrix inverseParent = MatrixInvert(parentPose);
    const Vector3 targetParent = Vector3Transform(targetModel, inverseParent);
    const Vector3 boneOrigin = {localPose.m12, localPose.m13, localPose.m14};

    Vector3 desiredForward = Vector3Subtract(targetParent, boneOrigin);
    Vector3 currentForward = transform_direction(localForward, localPose);

    if (!normalize_safe(&desiredForward) || !normalize_safe(&currentForward)) return false;

    const float dot = Clamp(Vector3DotProduct(currentForward, desiredForward), -1.0f, 1.0f);
    const float angle = acosf(dot);

    if (angle <= 1e-6f || params.weight == 0.0f) return true;

    Vector3 axis = Vector3CrossProduct(currentForward, desiredForward);

    if (!normalize_safe(&axis))
    {
        // Exactly opposite directions: choose a stable 180-degree axis
        // from the already animated local up vector.
        if (dot > 0.0f) return true;

        Vector3 currentUp = transform_direction(localUp, localPose);
        axis = project_perpendicular(currentUp, currentForward);
        if (!normalize_safe(&axis)) return false;
    }

    float limitedAngle = angle;
    if (params.maxAngleRadians > 0.0f) limitedAngle = fminf(limitedAngle, params.maxAngleRadians);

    const Quaternion swing = QuaternionFromAxisAngle(axis, limitedAngle * params.weight);
    Matrix correctedLocal = MatrixMultiply(localPose, QuaternionToMatrix(swing));

    // Rotate only the linear basis, not the bone origin.
    correctedLocal.m12 = localPose.m12;
    correctedLocal.m13 = localPose.m13;
    correctedLocal.m14 = localPose.m14;
    correctedLocal.m3 = localPose.m3;
    correctedLocal.m7 = localPose.m7;
    correctedLocal.m11 = localPose.m11;
    correctedLocal.m15 = localPose.m15;

    return R3D_SetAnimationBoneLocalMatrix(player, boneIndex, correctedLocal);
}
