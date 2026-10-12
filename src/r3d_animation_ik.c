/* r3d_animation_ik.c -- R3D Animation IK Module.
 *
 * Copyright (c) 2025-2026 Dennis Steffen
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_animation_ik.h>
#include <raymath.h>
#include <math.h>

static bool finite_vec3(Vector3 v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool finite_affine(Matrix matrix) {
    float16 values = MatrixToFloatV(matrix);

    for (int i = 0; i < 16; i++) {
        if (!isfinite(values.v[i])) return false;
    }

    return fabsf(matrix.m3) <= 1e-5f &&
           fabsf(matrix.m7) <= 1e-5f &&
           fabsf(matrix.m11) <= 1e-5f &&
           fabsf(matrix.m15 - 1.0f) <= 1e-5f;
}

static bool normalize_safe(Vector3 *vector) {
    float lengthSq = Vector3LengthSqr(*vector);
    if (!isfinite(lengthSq) || lengthSq <= 1e-12f) return false;

    *vector = Vector3Scale(*vector, 1.0f / sqrtf(lengthSq));
    return true;
}

static Vector3 matrix_position(Matrix matrix) {
    return (Vector3){matrix.m12, matrix.m13, matrix.m14};
}

static Vector3 perpendicular(Vector3 vector, Vector3 axis) {
    return Vector3Subtract(vector, Vector3Scale(axis, Vector3DotProduct(vector, axis)));
}

static bool rotation_between(Vector3 from, Vector3 to, Vector3 fallbackAxis, Quaternion *outRotation) {
    if (!normalize_safe(&from) || !normalize_safe(&to)) return false;

    float dot = Clamp(Vector3DotProduct(from, to), -1.0f, 1.0f);

    if (dot >= 0.999999f) {
        *outRotation = QuaternionIdentity();
        return true;
    }

    Vector3 axis;

    if (dot <= -0.999999f) {
        // Raylib's QuaternionFromVector3ToVector3() is ambiguous
        // for opposite vectors. Use a stable perpendicular axis.
        axis = perpendicular(fallbackAxis, from);

        if (!normalize_safe(&axis)) {
            Vector3 reference = fabsf(from.y) < 0.9f
                                    ? (Vector3){0.0f, 1.0f, 0.0f}
                                    : (Vector3){1.0f, 0.0f, 0.0f};

            axis = perpendicular(reference, from);

            if (!normalize_safe(&axis)) return false;
        }

        *outRotation = QuaternionFromAxisAngle(axis, PI);
        return true;
    }

    axis = Vector3CrossProduct(from, to);
    if (!normalize_safe(&axis)) return false;

    *outRotation = QuaternionFromAxisAngle(axis, acosf(dot));
    return true;
}

static Matrix rotate_basis(Matrix matrix, Quaternion rotation) {
    // Raylib MatrixMultiply(a, b) composes b * a.
    Matrix result = MatrixMultiply(matrix, QuaternionToMatrix(rotation));

    // Rotate the basis around the bone origin, not around model origin.
    result.m12 = matrix.m12;
    result.m13 = matrix.m13;
    result.m14 = matrix.m14;

    result.m3 = matrix.m3;
    result.m7 = matrix.m7;
    result.m11 = matrix.m11;
    result.m15 = matrix.m15;

    return result;
}

bool R3D_ApplyAnimationTwoBoneIK(R3D_AnimationPlayer *player, int rootBone, int middleBone, int endBone, R3D_TwoBoneIKParams params) {
    if (!player || !player->skeleton.bones) return false;
    if (!finite_vec3(params.targetModel) || !finite_vec3(params.poleModel)) return false;
    if (!isfinite(params.weight) || params.weight < 0.0f || params.weight > 1.0f) return false;

    // Validate optional bend-angle constraints.
    if (params.bend.enabled) {
        if (!isfinite(params.bend.minAngleRadians) ||
            !isfinite(params.bend.maxAngleRadians) ||
            params.bend.minAngleRadians < 0.0f ||
            params.bend.maxAngleRadians > PI ||
            params.bend.minAngleRadians > params.bend.maxAngleRadians) {
            return false;
        }
    }

    const int boneCount = player->skeleton.boneCount;

    if (rootBone < 0 || rootBone >= boneCount ||
        middleBone < 0 || middleBone >= boneCount ||
        endBone < 0 || endBone >= boneCount) {
        return false;
    }

    const R3D_BoneInfo *bones = player->skeleton.bones;

    if (bones[middleBone].parent != rootBone || bones[endBone].parent != middleBone) return false;

    const int parentIndex = bones[rootBone].parent;
    if (parentIndex < -1 || parentIndex >= boneCount) return false;
    if (parentIndex == rootBone || parentIndex == middleBone || parentIndex == endBone) return false;

    Matrix rootLocal, middleLocal, endLocal;
    Matrix rootModel, middleModel, endModel;

    if (!R3D_GetAnimationBoneLocalMatrix(player, rootBone, &rootLocal) ||
        !R3D_GetAnimationBoneLocalMatrix(player, middleBone, &middleLocal) ||
        !R3D_GetAnimationBoneLocalMatrix(player, endBone, &endLocal) ||
        !R3D_GetAnimationBoneModelMatrix(player, rootBone, &rootModel) ||
        !R3D_GetAnimationBoneModelMatrix(player, middleBone, &middleModel) ||
        !R3D_GetAnimationBoneModelMatrix(player, endBone, &endModel)) {
        return false;
    }

    Matrix parentModel = player->skeleton.rootBind;

    if (parentIndex >= 0 &&
        !R3D_GetAnimationBoneModelMatrix(player, parentIndex, &parentModel)) {
        return false;
    }

    if (!finite_affine(rootLocal) || !finite_affine(middleLocal) ||
        !finite_affine(endLocal) || !finite_affine(rootModel) ||
        !finite_affine(middleModel) || !finite_affine(endModel) ||
        !finite_affine(parentModel)) {
        return false;
    }

    const float parentDet = MatrixDeterminant(parentModel);
    const float rootDet = MatrixDeterminant(rootModel);

    if (!isfinite(parentDet) || !isfinite(rootDet) ||
        fabsf(parentDet) <= 1e-8f || fabsf(rootDet) <= 1e-8f) {
        return false;
    }

    const Vector3 rootPos = matrix_position(rootModel);
    const Vector3 middlePos = matrix_position(middleModel);
    const Vector3 endPos = matrix_position(endModel);

    const float upperLength = Vector3Distance(rootPos, middlePos);
    const float lowerLength = Vector3Distance(middlePos, endPos);

    if (!isfinite(upperLength) || !isfinite(lowerLength) ||
        upperLength <= 1e-6f || lowerLength <= 1e-6f) {
        return false;
    }

    Vector3 direction = Vector3Subtract(params.targetModel, rootPos);
    const float targetDistance = Vector3Length(direction);

    if (!isfinite(targetDistance) || targetDistance <= 1e-6f) return false;
    direction = Vector3Scale(direction, 1.0f / targetDistance);

    if (params.weight == 0.0f) return true;

    // ========================================
    // SOLVE THE ELBOW / KNEE POSITION
    // ========================================

    // ========================================
    // SOLVE THE ELBOW / KNEE POSITION
    // ========================================

    float minReach = fabsf(upperLength - lowerLength);
    float maxReach = upperLength + lowerLength;

    if (params.bend.enabled) {
        const float minBend = params.bend.minAngleRadians;
        const float maxBend = params.bend.maxAngleRadians;

        // Law of cosines:
        //
        // distance^2 = upper^2 + lower^2
        //            + 2 * upper * lower * cos(bendAngle)
        //
        // Larger bend angles produce a shorter reach.

        const float minReachSq =
                upperLength * upperLength +
                lowerLength * lowerLength +
                2.0f * upperLength * lowerLength * cosf(maxBend);

        const float maxReachSq =
                upperLength * upperLength +
                lowerLength * lowerLength +
                2.0f * upperLength * lowerLength * cosf(minBend);

        minReach = sqrtf(fmaxf(0.0f, minReachSq));
        maxReach = sqrtf(fmaxf(0.0f, maxReachSq));
    }

    // Clamp the target distance to the permitted reach.
    const float reach = Clamp(targetDistance, minReach, maxReach);

    const Vector3 reachedTarget = Vector3Add(
        rootPos,
        Vector3Scale(direction, reach)
    );

    // Determine which side the elbow or knee should bend toward.
    Vector3 bendDirection = perpendicular(
        Vector3Subtract(params.poleModel, rootPos),
        direction
    );

    if (!normalize_safe(&bendDirection)) {
        // Fall back to the original animated bend direction.
        bendDirection = perpendicular(
            Vector3Subtract(middlePos, rootPos),
            direction
        );

        if (!normalize_safe(&bendDirection)) {
            Vector3 reference = fabsf(direction.y) < 0.9f
                                    ? (Vector3){0.0f, 1.0f, 0.0f}
                                    : (Vector3){1.0f, 0.0f, 0.0f};

            bendDirection = perpendicular(reference, direction);

            if (!normalize_safe(&bendDirection)) return false;
        }
    }

    // Calculate the elbow position using the law of cosines.
    float along;
    float height;

    if (reach <= 1e-6f) {
        // A fully folded chain with equal segment lengths
        // has zero reach. The pole determines its bend direction.
        if (fabsf(upperLength - lowerLength) > 1e-5f) return false;

        along = 0.0f;
        height = upperLength;
    } else {
        along = (
                    upperLength * upperLength -
                    lowerLength * lowerLength +
                    reach * reach
                ) / (2.0f * reach);

        height = sqrtf(fmaxf(
            0.0f,
            upperLength * upperLength - along * along
        ));
    }

    const Vector3 desiredMiddle = Vector3Add(
        Vector3Add(rootPos, Vector3Scale(direction, along)),
        Vector3Scale(bendDirection, height)
    );

    // ========================================
    // SOLVE ROOT ROTATION
    // ========================================

    Quaternion rootCorrection;

    if (!rotation_between(Vector3Subtract(middlePos, rootPos),
                          Vector3Subtract(desiredMiddle, rootPos),
                          bendDirection, &rootCorrection)) {
        return false;
    }

    Quaternion weightedRoot = QuaternionSlerp(QuaternionIdentity(), rootCorrection, params.weight);
    Matrix desiredRootModel = rotate_basis(rootModel, weightedRoot);

    // Predict the rest of the chain without modifying the player.
    Matrix predictedMiddleModel = MatrixMultiply(middleLocal, desiredRootModel);
    Matrix predictedEndModel = MatrixMultiply(endLocal, predictedMiddleModel);

    if (!finite_affine(predictedMiddleModel) || !finite_affine(predictedEndModel)) return false;

    // ========================================
    // SOLVE ELBOW / KNEE ROTATION
    // ========================================

    const Vector3 predictedMiddle = matrix_position(predictedMiddleModel);
    const Vector3 predictedEnd = matrix_position(predictedEndModel);

    Quaternion middleCorrection;

    if (!rotation_between(Vector3Subtract(predictedEnd, predictedMiddle),
                          Vector3Subtract(reachedTarget, predictedMiddle),
                          bendDirection, &middleCorrection)) {
        return false;
    }

    Quaternion weightedMiddle = QuaternionSlerp(QuaternionIdentity(), middleCorrection, params.weight);
    Matrix desiredMiddleModel = rotate_basis(predictedMiddleModel, weightedMiddle);

    // ========================================
    // CONVERT SOLUTION BACK TO LOCAL MATRICES
    // ========================================

    // Both conversions use the new parent model matrices.
    // No intermediate modelPose writes or hierarchy recomputation.
    Matrix solvedRootLocal = MatrixMultiply(desiredRootModel, MatrixInvert(parentModel));
    Matrix solvedMiddleLocal = MatrixMultiply(desiredMiddleModel, MatrixInvert(desiredRootModel));

    if (!finite_affine(solvedRootLocal) || !finite_affine(solvedMiddleLocal)) return false;

    // Commit only after both resulting matrices have been validated.
    if (!R3D_SetAnimationBoneLocalMatrix(player, rootBone, solvedRootLocal)) return false;

    if (!R3D_SetAnimationBoneLocalMatrix(player, middleBone, solvedMiddleLocal)) {
        R3D_SetAnimationBoneLocalMatrix(player, rootBone, rootLocal);
        return false;
    }

    return true;
}
