/* r3d_animation_spring.c -- R3D Animation Spring Bone Module.
 *
 * Copyright (c) 2025-2026 Dennis Steffen
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_animation_spring.h>
#include <raymath.h>
#include <math.h>

static bool finite_vec3(Vector3 value)
{
    return isfinite(value.x) &&
           isfinite(value.y) &&
           isfinite(value.z);
}

static bool finite_affine(Matrix matrix)
{
    float16 values = MatrixToFloatV(matrix);

    for (int i = 0; i < 16; i++)
    {
        if (!isfinite(values.v[i])) return false;
    }

    return fabsf(matrix.m3) <= 1e-5f &&
           fabsf(matrix.m7) <= 1e-5f &&
           fabsf(matrix.m11) <= 1e-5f &&
           fabsf(matrix.m15 - 1.0f) <= 1e-5f;
}

static bool normalize_safe(Vector3* vector)
{
    float lengthSq = Vector3LengthSqr(*vector);

    if (!isfinite(lengthSq) || lengthSq <= 1e-12f)
    {
        return false;
    }

    *vector = Vector3Scale(*vector, 1.0f / sqrtf(lengthSq));
    return true;
}

static Vector3 matrix_position(Matrix matrix)
{
    return (Vector3){
        matrix.m12,
        matrix.m13,
        matrix.m14
    };
}

static Vector3 transform_direction(Vector3 vector, Matrix matrix)
{
    return Vector3Subtract(
        Vector3Transform(vector, matrix),
        matrix_position(matrix)
    );
}

static Matrix rotate_basis(Matrix matrix, Quaternion rotation)
{
    // Raylib MatrixMultiply(a, b) composes b * a.
    Matrix result = MatrixMultiply(matrix, QuaternionToMatrix(rotation));

    // Rotate around the bone origin, preserving translation.
    result.m12 = matrix.m12;
    result.m13 = matrix.m13;
    result.m14 = matrix.m14;

    result.m3 = matrix.m3;
    result.m7 = matrix.m7;
    result.m11 = matrix.m11;
    result.m15 = matrix.m15;

    return result;
}

static bool rotation_between(
    Vector3 from,
    Vector3 to,
    Matrix modelMatrix,
    Quaternion* result)
{
    if (!normalize_safe(&from) || !normalize_safe(&to))
    {
        return false;
    }

    float dot = Clamp(Vector3DotProduct(from, to), -1.0f, 1.0f);

    if (dot >= 0.999999f)
    {
        *result = QuaternionIdentity();
        return true;
    }

    Vector3 axis;

    if (dot <= -0.999999f)
    {
        // Opposite directions have no unique shortest rotation.
        // Prefer an axis from the current bone basis.
        axis = Vector3CrossProduct(
            from,
            transform_direction((Vector3){0, 1, 0}, modelMatrix)
        );

        if (!normalize_safe(&axis))
        {
            Vector3 reference = fabsf(from.x) < 0.9f
                ? (Vector3){1, 0, 0}
                : (Vector3){0, 1, 0};

            axis = Vector3CrossProduct(from, reference);
            if (!normalize_safe(&axis)) return false;
        }

        *result = QuaternionFromAxisAngle(axis, PI);
        return true;
    }

    axis = Vector3CrossProduct(from, to);

    if (!normalize_safe(&axis)) return false;

    *result = QuaternionFromAxisAngle(axis, acosf(dot));
    return true;
}

/**
 * Constrains a simulated spring direction to independent
 * horizontal and vertical angular ranges.
 *
 * Directions and velocities are in model space.
 * The constraint frame follows the authored bone orientation.
 */
static bool apply_spring_angle_limits(
    Vector3 authoredDirection,
    Matrix authoredModel,
    R3D_SpringBoneAngleLimits limits,
    Vector3* direction,
    Vector3* velocity)
{
    Vector3 forward = authoredDirection;

    if (!normalize_safe(&forward)) return false;

    // Build an orthonormal constraint frame from the
    // current authored bone orientation.
    Vector3 right = transform_direction(
        limits.localReferenceAxis,
        authoredModel
    );

    right = Vector3Subtract(
        right,
        Vector3Scale(
            forward,
            Vector3DotProduct(right, forward)
        )
    );

    if (!normalize_safe(&right)) return false;

    Vector3 up = Vector3CrossProduct(right, forward);

    if (!normalize_safe(&up)) return false;

    // ========================================
    // EXTRACT TWO ANGLES
    // ========================================

    const float horizontalComponent =
        Vector3DotProduct(*direction, right);

    const float verticalComponent =
        Vector3DotProduct(*direction, up);

    const float forwardComponent =
        Vector3DotProduct(*direction, forward);

    const float horizontalAngle = atan2f(
        horizontalComponent,
        forwardComponent
    );

    const float verticalAngle = atan2f(
        verticalComponent,
        sqrtf(fmaxf(
            0.0f,
            horizontalComponent * horizontalComponent +
            forwardComponent * forwardComponent
        ))
    );

    // ========================================
    // CLAMP BOTH AXES INDEPENDENTLY
    // ========================================

    const float yaw = Clamp(
        horizontalAngle,
        limits.minHorizontalRadians,
        limits.maxHorizontalRadians
    );

    const float pitch = Clamp(
        verticalAngle,
        limits.minVerticalRadians,
        limits.maxVerticalRadians
    );

    const float sinYaw = sinf(yaw);
    const float cosYaw = cosf(yaw);
    const float sinPitch = sinf(pitch);
    const float cosPitch = cosf(pitch);

    Vector3 horizontal = Vector3Add(
        Vector3Scale(forward, cosYaw),
        Vector3Scale(right, sinYaw)
    );

    Vector3 constrained = Vector3Add(
        Vector3Scale(horizontal, cosPitch),
        Vector3Scale(up, sinPitch)
    );

    if (!normalize_safe(&constrained)) return false;

    // ========================================
    // REMOVE OUTWARD VELOCITY AT LIMITS
    // ========================================

    // The two tangent vectors describe positive motion
    // around each angular coordinate.
    Vector3 yawTangent = Vector3Subtract(
        Vector3Scale(right, cosYaw),
        Vector3Scale(forward, sinYaw)
    );

    Vector3 pitchTangent = Vector3Subtract(
        Vector3Scale(up, cosPitch),
        Vector3Scale(horizontal, sinPitch)
    );

    Vector3 constrainedVelocity = *velocity;

    // Remove radial velocity first.
    constrainedVelocity = Vector3Subtract(
        constrainedVelocity,
        Vector3Scale(
            constrained,
            Vector3DotProduct(constrainedVelocity, constrained)
        )
    );

    float yawVelocity = Vector3DotProduct(
        constrainedVelocity,
        yawTangent
    );

    float pitchVelocity = Vector3DotProduct(
        constrainedVelocity,
        pitchTangent
    );

    const float epsilon = 1e-5f;

    // Prevent velocity from pushing past the horizontal limits.
    if ((yaw <= limits.minHorizontalRadians + epsilon &&
         yawVelocity < 0.0f) ||
        (yaw >= limits.maxHorizontalRadians - epsilon &&
         yawVelocity > 0.0f))
    {
        constrainedVelocity = Vector3Subtract(
            constrainedVelocity,
            Vector3Scale(yawTangent, yawVelocity)
        );
    }

    // Prevent velocity from pushing past the vertical limits.
    if ((pitch <= limits.minVerticalRadians + epsilon &&
         pitchVelocity < 0.0f) ||
        (pitch >= limits.maxVerticalRadians - epsilon &&
         pitchVelocity > 0.0f))
    {
        constrainedVelocity = Vector3Subtract(
            constrainedVelocity,
            Vector3Scale(
                pitchTangent,
                Vector3DotProduct(
                    constrainedVelocity,
                    pitchTangent
                )
            )
        );
    }

    // Keep the final velocity tangent to the constrained sphere.
    constrainedVelocity = Vector3Subtract(
        constrainedVelocity,
        Vector3Scale(
            constrained,
            Vector3DotProduct(constrainedVelocity, constrained)
        )
    );

    if (!finite_vec3(constrained) ||
        !finite_vec3(constrainedVelocity))
    {
        return false;
    }

    *direction = constrained;
    *velocity = constrainedVelocity;

    return true;
}

/**
 * Integrates a damped harmonic spring toward a fixed target.
 *
 * This uses the analytical solution of a second-order spring,
 * with the target treated as constant over the current frame.
 *
 * The result is independent of Euler integration step size.
 */
static void integrate_spring(
    Vector3 current,
    Vector3 velocity,
    Vector3 target,
    float frequencyHz,
    float dampingRatio,
    float dt,
    Vector3* outPosition,
    Vector3* outVelocity)
{
    const float omega = 2.0f * PI * frequencyHz;
    const float alpha = dampingRatio * omega;

    // Displacement relative to the moving target.
    Vector3 x = Vector3Subtract(current, target);

    Vector3 nextX;
    Vector3 nextVelocity;

    if (dampingRatio >= 0.999f)
    {
        // Critically damped spring.
        float decay = expf(-omega * dt);

        Vector3 initialSlope = Vector3Add(
            velocity,
            Vector3Scale(x, omega)
        );

        nextX = Vector3Scale(
            Vector3Add(x, Vector3Scale(initialSlope, dt)),
            decay
        );

        nextVelocity = Vector3Scale(
            Vector3Subtract(
                velocity,
                Vector3Scale(initialSlope, omega * dt)
            ),
            decay
        );
    }
    else
    {
        // Underdamped spring.
        float omegaD = omega * sqrtf(
            1.0f - dampingRatio * dampingRatio
        );

        float decay = expf(-alpha * dt);
        float cosine = cosf(omegaD * dt);
        float sineOverOmegaD = sinf(omegaD * dt) / omegaD;

        float positionFromPosition =
            decay * (cosine + alpha * sineOverOmegaD);

        float positionFromVelocity =
            decay * sineOverOmegaD;

        float velocityFromPosition =
            -decay * omega * omega * sineOverOmegaD;

        float velocityFromVelocity =
            decay * (cosine - alpha * sineOverOmegaD);

        nextX = Vector3Add(
            Vector3Scale(x, positionFromPosition),
            Vector3Scale(velocity, positionFromVelocity)
        );

        nextVelocity = Vector3Add(
            Vector3Scale(x, velocityFromPosition),
            Vector3Scale(velocity, velocityFromVelocity)
        );
    }

    *outPosition = Vector3Add(target, nextX);
    *outVelocity = nextVelocity;
}

void R3D_ResetAnimationSpringBone(R3D_SpringBoneState* state)
{
    if (!state) return;

    state->tipModel = Vector3Zero();
    state->velocityModel = Vector3Zero();
    state->initialized = false;
}

bool R3D_ApplyAnimationSpringBone(
    R3D_AnimationPlayer* player,
    int boneIndex,
    R3D_SpringBoneState* state,
    R3D_SpringBoneParams params,
    float deltaTime)
{
    // ========================================
    // VALIDATION
    // ========================================

    if (!player || !state || !player->skeleton.bones)
    {
        return false;
    }

    if (boneIndex < 0 || boneIndex >= player->skeleton.boneCount)
    {
        return false;
    }

    if (!finite_vec3(params.localAxis) ||
        !isfinite(params.length) ||
        !isfinite(params.frequencyHz) ||
        !isfinite(params.dampingRatio) ||
        !isfinite(params.weight) ||
        !isfinite(deltaTime))
    {
        return false;
    }

    if (params.length <= 0.0f ||
        params.frequencyHz <= 0.0f ||
        params.frequencyHz > 1000.0f ||
        params.dampingRatio < 0.0f ||
        params.dampingRatio > 1.0f ||
        params.weight < 0.0f ||
        params.weight > 1.0f ||
        deltaTime < 0.0f ||
        deltaTime > 1.0f)
    {
        return false;
    }

    Vector3 localAxis = params.localAxis;

    if (!normalize_safe(&localAxis)) return false;

    // ========================================
    // OPTIONAL TWO-AXIS ANGLE LIMITS
    // ========================================

    if (params.limits.enabled)
    {
        const R3D_SpringBoneAngleLimits* limits =
            &params.limits;

        const float halfPi = PI * 0.5f;

        if (!finite_vec3(limits->localReferenceAxis) ||
            !isfinite(limits->minHorizontalRadians) ||
            !isfinite(limits->maxHorizontalRadians) ||
            !isfinite(limits->minVerticalRadians) ||
            !isfinite(limits->maxVerticalRadians))
        {
            return false;
        }

        if (limits->minHorizontalRadians < -PI ||
            limits->maxHorizontalRadians > PI ||
            limits->minHorizontalRadians >
                limits->maxHorizontalRadians ||
            limits->minVerticalRadians < -halfPi ||
            limits->maxVerticalRadians > halfPi ||
            limits->minVerticalRadians >
                limits->maxVerticalRadians)
        {
            return false;
        }

        // The reference axis must not be parallel to
        // the spring's local forward axis.
        Vector3 reference = Vector3Subtract(
            limits->localReferenceAxis,
            Vector3Scale(
                localAxis,
                Vector3DotProduct(
                    limits->localReferenceAxis,
                    localAxis
                )
            )
        );

        if (!normalize_safe(&reference)) return false;
    }

    if (state->initialized &&
        (!finite_vec3(state->tipModel) ||
         !finite_vec3(state->velocityModel)))
    {
        return false;
    }

    const int parentIndex = player->skeleton.bones[boneIndex].parent;

    if (parentIndex < -1 ||
        parentIndex >= player->skeleton.boneCount ||
        parentIndex == boneIndex)
    {
        return false;
    }

    Matrix localMatrix;
    Matrix modelMatrix;

    if (!R3D_GetAnimationBoneLocalMatrix(player, boneIndex, &localMatrix) ||
        !R3D_GetAnimationBoneModelMatrix(player, boneIndex, &modelMatrix))
    {
        return false;
    }

    Matrix parentMatrix = player->skeleton.rootBind;

    if (parentIndex >= 0 &&
        !R3D_GetAnimationBoneModelMatrix(player, parentIndex, &parentMatrix))
    {
        return false;
    }

    if (!finite_affine(localMatrix) ||
        !finite_affine(modelMatrix) ||
        !finite_affine(parentMatrix))
    {
        return false;
    }

    const float determinant = MatrixDeterminant(parentMatrix);

    if (!isfinite(determinant) || fabsf(determinant) <= 1e-8f)
    {
        return false;
    }

    // ========================================
    // AUTHORED TIP POSITION
    // ========================================

    const Vector3 origin = matrix_position(modelMatrix);

    const Vector3 authoredDirection = transform_direction(
        Vector3Scale(localAxis, params.length),
        modelMatrix
    );

    const float lengthModel = Vector3Length(authoredDirection);

    if (!isfinite(lengthModel) || lengthModel <= 1e-6f)
    {
        return false;
    }

    const Vector3 targetTip = Vector3Add(
        origin,
        authoredDirection
    );

    // ========================================
    // INITIALIZATION
    // ========================================

    if (!state->initialized)
    {
        // The first evaluation establishes the rest position.
        // No unexpected spring rotation on the first frame.
        state->tipModel = targetTip;
        state->velocityModel = Vector3Zero();
        state->initialized = true;
        return true;
    }

    // ========================================
    // SIMULATE SPRING TIP
    // ========================================

    R3D_SpringBoneState next = *state;

    Vector3 simulatedTip;
    Vector3 simulatedVelocity;

    integrate_spring(
        state->tipModel,
        state->velocityModel,
        targetTip,
        params.frequencyHz,
        params.dampingRatio,
        deltaTime,
        &simulatedTip,
        &simulatedVelocity
    );

    if (!finite_vec3(simulatedTip) ||
        !finite_vec3(simulatedVelocity))
    {
        return false;
    }

    // Constrain the simulated tip to a sphere around the bone
    // origin. The spring changes direction, never bone length.
    Vector3 direction = Vector3Subtract(simulatedTip, origin);

    if (!normalize_safe(&direction))
    {
        direction = authoredDirection;

        if (!normalize_safe(&direction)) return false;

        simulatedVelocity = Vector3Zero();
    }

    // ========================================
    // CONSTRAIN THE SIMULATED SPRING
    // ========================================

    if (params.limits.enabled)
    {
        if (!apply_spring_angle_limits(
            authoredDirection,
            modelMatrix,
            params.limits,
            &direction,
            &simulatedVelocity))
        {
            return false;
        }
    }

    // The persistent state stores the constrained result,
    // not the unrestricted spring position.
    next.tipModel = Vector3Add(
        origin,
        Vector3Scale(direction, lengthModel)
    );

    // Remove radial velocity. Only tangential motion should
    // contribute to changes in the bone's direction.
    next.velocityModel = Vector3Subtract(
        simulatedVelocity,
        Vector3Scale(
            direction,
            Vector3DotProduct(simulatedVelocity, direction)
        )
    );

    if (!finite_vec3(next.tipModel) ||
        !finite_vec3(next.velocityModel))
    {
        return false;
    }

    // ========================================
    // APPLY BONE ORIENTATION
    // ========================================

    if (params.weight == 0.0f)
    {
        // Simulation continues, but the authored pose is untouched.
        *state = next;
        return true;
    }

    Quaternion correction;

    if (!rotation_between(
        authoredDirection,
        direction,
        modelMatrix,
        &correction))
    {
        return false;
    }

    Quaternion weightedCorrection = QuaternionSlerp(
        QuaternionIdentity(),
        correction,
        params.weight
    );

    Matrix desiredModel = rotate_basis(
        modelMatrix,
        weightedCorrection
    );

    // Convert the rotated model basis back into local space.
    Matrix solvedLocal = MatrixMultiply(
        desiredModel,
        MatrixInvert(parentMatrix)
    );

    if (!finite_affine(solvedLocal))
    {
        return false;
    }

    // Do not commit the spring state until the pose write succeeds.
    if (!R3D_SetAnimationBoneLocalMatrix(player, boneIndex, solvedLocal))
    {
        return false;
    }

    *state = next;

    return true;
}