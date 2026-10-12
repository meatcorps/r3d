#include <r3d/r3d_animation_player.h>
#include <raymath.h>
#include <glad.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int check(const char* name, bool condition)
{
    printf("%s: %s\n", name, condition ? "PASS" : "FAIL");
    return condition ? 0 : 1;
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(64, 64, "Animation skin upload test");

    if (!IsWindowReady()) return 1;

    Matrix localBind[1] = {MatrixIdentity()};
    Matrix modelBind[1] = {MatrixIdentity()};
    Matrix invBind[1] = {MatrixTranslate(-2.0f, 0.0f, 0.0f)};

    R3D_Skeleton skeleton = {
        .boneCount = 1,
        .localBind = localBind,
        .modelBind = modelBind,
        .invBind = invBind,
        .rootBind = MatrixIdentity()
    };

    R3D_Animation animation = {0};
    R3D_AnimationLib library = {
        .animations = &animation,
        .count = 1
    };

    R3D_AnimationPlayer player = R3D_LoadAnimationPlayer(skeleton, library);

    int failures = 0;

    failures += check("Animation player created",
        R3D_IsAnimationPlayerValid(player));

    if (!R3D_IsAnimationPlayerValid(player))
    {
        R3D_UnloadAnimationPlayer(player);
        CloseWindow();
        return 1;
    }

    // Modify the model pose without uploading yet.
    player.modelPose[0] = MatrixTranslate(5.0f, 2.0f, 0.0f);

    failures += check("GPU skin buffer not yet updated",
        near(player.skinBuffer[0].m12, -2.0f));

    // Create a separate texture binding to verify GL state preservation.
    GLuint otherTexture = 0;
    glGenTextures(1, &otherTexture);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_1D, otherTexture);

    R3D_UploadAnimationPose(&player);

    failures += check("Skinning translation X",
        near(player.skinBuffer[0].m12, 3.0f));

    failures += check("Skinning translation Y",
        near(player.skinBuffer[0].m13, 2.0f));

    GLint activeUnit = 0;
    GLint boundTexture = 0;

    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
    glGetIntegerv(GL_TEXTURE_BINDING_1D, &boundTexture);

    failures += check("Active texture unit preserved",
        activeUnit == GL_TEXTURE1);

    failures += check("Texture binding preserved",
        boundTexture == (GLint)otherTexture);

    // Read back the GPU texture as float data.
    glBindTexture(GL_TEXTURE_1D, player.skinTexture);

    float gpuData[16] = {0};
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_FLOAT, gpuData);

    GLenum glError = glGetError();

    failures += check("GPU texture readback succeeded",
        glError == GL_NO_ERROR);

    // The skin texture stores the raw row-major Matrix memory.
    // MatrixToFloatV() would transpose the expected element ordering.
    float expected[16];
    memcpy(expected, &player.skinBuffer[0], sizeof(expected));

    bool matches = true;

    for (int i = 0; i < 16; i++)
    {
        if (!near(gpuData[i], expected[i]))
        {
            printf("GPU mismatch [%d]: actual %.6f, expected %.6f\n", i, gpuData[i], expected[i]);
            matches = false;
        }
    }

    failures += check("GPU contains final skinning matrix", matches);

    glBindTexture(GL_TEXTURE_1D, (GLuint)boundTexture);
    glDeleteTextures(1, &otherTexture);
    glActiveTexture(GL_TEXTURE0);

    R3D_UnloadAnimationPlayer(player);
    CloseWindow();

    printf("\nAnimation skin upload tests: %s\n",
        failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}