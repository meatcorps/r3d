#include <r3d/r3d_utils.h>
#include <raymath.h>
#include <stdio.h>

static int failures = 0;

static void check_matrix(const char* name, Matrix actual, Matrix expected)
{
    bool passed = R3D_MatrixEquals(actual, expected);

    printf("%s: %s\n", name, passed ? "PASS" : "FAIL");

    if (!passed)
        failures++;
}

int main(void)
{
    Vector3 oldPosition = {1.0f, 2.0f, 3.0f};
    Vector3 newPosition = {4.0f, 5.0f, 6.0f};

    Quaternion oldRotation = QuaternionFromAxisAngle(
        (Vector3){0.0f, 1.0f, 0.0f}, 0.35f
    );

    Quaternion newRotation = QuaternionFromAxisAngle(
        Vector3Normalize((Vector3){0.3f, 0.7f, 0.2f}),
        0.85f
    );

    Vector3 oldScale = {1.0f, 2.0f, 3.0f};
    Vector3 newScale = {2.0f, 3.0f, 4.0f};

    Matrix original = R3D_MatrixFromTRS(
        oldPosition, oldRotation, oldScale
    );

    check_matrix(
        "TRS",
        R3D_MatrixFromTRS(newPosition, newRotation, newScale),
        R3D_MatrixFromTRS(newPosition, newRotation, newScale)
    );

    check_matrix(
        "TR",
        R3D_MatrixFromTR(original, newPosition, newRotation),
        R3D_MatrixFromTRS(newPosition, newRotation, oldScale)
    );

    check_matrix(
        "TS",
        R3D_MatrixFromTS(original, newPosition, newScale),
        R3D_MatrixFromTRS(newPosition, oldRotation, newScale)
    );

    check_matrix(
        "RS",
        R3D_MatrixFromRS(original, newRotation, newScale),
        R3D_MatrixFromTRS(oldPosition, newRotation, newScale)
    );

    check_matrix(
        "T",
        R3D_MatrixFromT(original, newPosition),
        R3D_MatrixFromTRS(newPosition, oldRotation, oldScale)
    );

    check_matrix(
        "R",
        R3D_MatrixFromR(original, newRotation),
        R3D_MatrixFromTRS(oldPosition, newRotation, oldScale)
    );

    check_matrix(
        "S",
        R3D_MatrixFromS(original, newScale),
        R3D_MatrixFromTRS(oldPosition, oldRotation, newScale)
    );

    // Translation-only replacement must also preserve shear.
    Matrix sheared = original;
    sheared.m4 += 0.45f;

    Matrix expected = sheared;
    expected.m12 = newPosition.x;
    expected.m13 = newPosition.y;
    expected.m14 = newPosition.z;

    check_matrix(
        "Translation preserves shear",
        R3D_MatrixFromT(sheared, newPosition),
        expected
    );

    printf(
        "\nMatrix helper tests: %s\n",
        failures == 0 ? "PASS" : "FAIL"
    );

    return failures == 0 ? 0 : 1;
}