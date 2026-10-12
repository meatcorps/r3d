#include "../src/common/r3d_math.h"
#include <r3d/r3d_utils.h>
#include <stdio.h>

static int check_roundtrip(const char* name, Vector3 translation, Quaternion rotation, Vector3 scale)
{
    Matrix original = r3d_matrix_srt_quat(scale, rotation, translation);

    Vector3 resultTranslation;
    Vector3 resultScale;
    Quaternion resultRotation;

    r3d_matrix_decompose(original, &resultTranslation, &resultRotation, &resultScale);

    Matrix rebuilt = r3d_matrix_srt_quat(resultScale, resultRotation, resultTranslation);

    bool passed = R3D_MatrixEquals(original, rebuilt);

    printf("%s: %s\n", name, passed ? "PASS" : "FAIL");

    if (!passed)
    {
        printf("  Original scale: %.4f, %.4f, %.4f\n", scale.x, scale.y, scale.z);
        printf("  Extracted scale: %.4f, %.4f, %.4f\n", resultScale.x, resultScale.y, resultScale.z);
    }

    return passed ? 0 : 1;
}

int main(void)
{
    int failures = 0;

    Vector3 zero = {0.0f, 0.0f, 0.0f};
    Vector3 position = {3.0f, 4.0f, 5.0f};

    Quaternion identity = QuaternionIdentity();
    Quaternion rotation = QuaternionFromAxisAngle((Vector3){0.3f, 0.7f, 0.2f}, 0.8f);

    failures += check_roundtrip("Identity", zero, identity, (Vector3){1.0f, 1.0f, 1.0f});
    failures += check_roundtrip("Uniform scale + rotation", zero, rotation, (Vector3){2.0f, 2.0f, 2.0f});
    failures += check_roundtrip("Nonuniform scale + rotation", zero, rotation, (Vector3){2.0f, 3.0f, 4.0f});
    failures += check_roundtrip("Full TRS", position, rotation, (Vector3){2.0f, 3.0f, 4.0f});
    failures += check_roundtrip("Negative X scale", position, rotation, (Vector3){-2.0f, 3.0f, 4.0f});
    failures += check_roundtrip("Negative Y scale", position, rotation, (Vector3){2.0f, -3.0f, 4.0f});
    failures += check_roundtrip("Three negative axes", position, rotation, (Vector3){-2.0f, -3.0f, -4.0f});

    // Shear cannot be represented exactly using translation, rotation and scale.
    // Check that decomposition still produces a finite, normalized rotation.
    Matrix sheared = r3d_matrix_srt_quat((Vector3){2.0f, 3.0f, 4.0f}, rotation, position);
    sheared.m4 += 0.5f;

    Vector3 resultTranslation;
    Vector3 resultScale;
    Quaternion resultRotation;

    r3d_matrix_decompose(sheared, &resultTranslation, &resultRotation, &resultScale);

    bool shearPassed =
        isfinite(resultScale.x) &&
        isfinite(resultScale.y) &&
        isfinite(resultScale.z) &&
        isfinite(resultRotation.x) &&
        isfinite(resultRotation.y) &&
        isfinite(resultRotation.z) &&
        isfinite(resultRotation.w) &&
        fabsf(QuaternionLength(resultRotation) - 1.0f) < 0.0001f &&
        Vector3Distance(resultTranslation, position) < 0.0001f;

    printf("Shear decomposition stability: %s\n", shearPassed ? "PASS" : "FAIL");

    if (!shearPassed) failures++;

    printf("\nMatrix decomposition tests: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}