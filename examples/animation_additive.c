#include "../src/common/r3d_anim.h"
#include <r3d/r3d_utils.h>
#include <stdio.h>

static int check_pose(const char* name, Transform actual, Transform expected)
{
    Matrix actualMatrix = r3d_matrix_srt_quat(actual.scale, actual.rotation, actual.translation);
    Matrix expectedMatrix = r3d_matrix_srt_quat(expected.scale, expected.rotation, expected.translation);

    bool passed = R3D_MatrixEquals(actualMatrix, expectedMatrix);

    printf("%s: %s\n", name, passed ? "PASS" : "FAIL");

    return passed ? 0 : 1;
}

int main(void)
{
    int failures = 0;

    Quaternion rotationDelta = QuaternionFromAxisAngle((Vector3){1.0f, 0.0f, 0.0f}, 0.6f);

    Transform reference = {
        .translation = {10.0f, 20.0f, -5.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 0.0f, 1.0f}, 0.4f),
        .scale = {2.0f, 3.0f, 4.0f}
    };

    Transform base = {
        .translation = {1.0f, 2.0f, 3.0f},
        .rotation = QuaternionFromAxisAngle((Vector3){0.0f, 1.0f, 0.0f}, 0.7f),
        .scale = {6.0f, 9.0f, 8.0f}
    };

    Transform layer = {
        .translation = {13.0f, 18.0f, -1.0f},
        .rotation = QuaternionMultiply(reference.rotation, rotationDelta),
        .scale = {4.0f, 6.0f, 2.0f}
    };

    // Weight zero must preserve the base pose.
    failures += check_pose(
        "Zero weight",
        r3d_anim_transform_additive(base, layer, reference, 0.0f),
        base
    );

    // A layer equal to the reference pose must have no influence.
    failures += check_pose(
        "Reference pose is neutral",
        r3d_anim_transform_additive(base, reference, reference, 0.75f),
        base
    );

    Transform expectedFull = {
        .translation = {4.0f, 0.0f, 7.0f},
        .rotation = QuaternionMultiply(base.rotation, rotationDelta),
        .scale = {12.0f, 18.0f, 4.0f}
    };

    failures += check_pose(
        "Full additive weight",
        r3d_anim_transform_additive(base, layer, reference, 1.0f),
        expectedFull
    );

    Quaternion halfRotation = QuaternionFromAxisAngle((Vector3){1.0f, 0.0f, 0.0f}, 0.3f);

    Transform expectedHalf = {
        .translation = {2.5f, 1.0f, 5.0f},
        .rotation = QuaternionMultiply(base.rotation, halfRotation),
        .scale = {9.0f, 13.5f, 6.0f}
    };

    failures += check_pose(
        "Half additive weight",
        r3d_anim_transform_additive(base, layer, reference, 0.5f),
        expectedHalf
    );

    // If the base equals the reference, weight 1 should reproduce the layer.
    failures += check_pose(
        "Reference base becomes layer",
        r3d_anim_transform_additive(reference, layer, reference, 1.0f),
        layer
    );

    // A quaternion and its negation describe the same rotation.
    Transform equivalentLayer = reference;
    equivalentLayer.rotation = QuaternionScale(reference.rotation, -1.0f);

    failures += check_pose(
        "Quaternion sign equivalence",
        r3d_anim_transform_additive(base, equivalentLayer, reference, 1.0f),
        base
    );

    // Check the scale fallback when the reference contains a collapsed axis.
    Transform zeroScaleReference = reference;
    zeroScaleReference.scale.x = 0.0f;

    Transform zeroScaleLayer = zeroScaleReference;
    zeroScaleLayer.scale.x = 2.0f;

    Transform zeroScaleExpected = base;
    zeroScaleExpected.scale.x = 7.0f;

    failures += check_pose(
        "Zero reference scale",
        r3d_anim_transform_additive(base, zeroScaleLayer, zeroScaleReference, 0.5f),
        zeroScaleExpected
    );

    printf("\nAdditive animation tests: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}