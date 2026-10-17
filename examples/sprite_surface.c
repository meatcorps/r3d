/*
 * Sprite Surface demo
 * Texture3D source rectangle and spritesheet validation.
 *
 * SPACE       Pause animation
 * LEFT/RIGHT  Select frame manually
 * F           Flip horizontally
 */

#include <r3d/r3d.h>
#include <r3d/r3d_texture_surface.h>
#include <raymath.h>

#include "r3d/r3d_texture.h"

#define VIEW_W 1280
#define VIEW_H 720

#define FRAME_WIDTH 64
#define FRAME_HEIGHT 64
#define FRAME_COUNT 4
#define ANIMATION_FPS 10.0f

#ifndef RESOURCES_PATH
#define RESOURCES_PATH "./"
#endif

int main(void)
{
    InitWindow(VIEW_W, VIEW_H, "[r3d] Sprite Surface - Texture3D");
    SetTargetFPS(60);

    if (!R3D_Init(VIEW_W, VIEW_H))
    {
        CloseWindow();
        return 1;
    }

    // Pixel-perfect source sampling, no neighboring-frame bleed.
    Texture2D spritesheet = R3D_LoadTextureEx(
        RESOURCES_PATH "images/spritesheet.png",
        TEXTURE_WRAP_CLAMP,
        TEXTURE_FILTER_POINT,
        true
    );

    if (!spritesheet.id || !R3D_InitTexture3DResources())
    {
        if (spritesheet.id) R3D_UnloadTexture(spritesheet);
        R3D_UnloadTexture3DResources();
        R3D_Close();
        CloseWindow();
        return 1;
    }

    R3D_ENVIRONMENT_SET(background.color, (Color){13, 19, 33, 255});

    Camera3D camera = {
        .position = {0.0f, 0.0f, 9.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 45.0f,
        .projection = CAMERA_PERSPECTIVE
    };

    R3D_TextureDrawOptions options = R3D_DefaultTextureDrawOptions();
    options.aspectMode = R3D_TEXTURE_ASPECT_STRETCH;
    options.transparencyMode = R3D_TRANSPARENCY_BLEND;
    options.billboardMode = R3D_BILLBOARD_FRONT;
    options.cullMode = R3D_CULL_NONE;

    int frame = 0;
    float frameTimer = 0.0f;
    bool paused = false;
    bool flipped = false;

    while (!WindowShouldClose())
    {
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_F)) flipped = !flipped;

        if (IsKeyPressed(KEY_RIGHT))
        {
            frame = (frame + 1) % FRAME_COUNT;
            frameTimer = 0.0f;
            paused = true;
        }

        if (IsKeyPressed(KEY_LEFT))
        {
            frame = (frame + FRAME_COUNT - 1) % FRAME_COUNT;
            frameTimer = 0.0f;
            paused = true;
        }

        if (!paused)
        {
            frameTimer += GetFrameTime();

            if (frameTimer >= 1.0f / ANIMATION_FPS)
            {
                frameTimer -= 1.0f / ANIMATION_FPS;
                frame = (frame + 1) % FRAME_COUNT;
            }
        }

        // The only thing needed to animate the 3D texture!
        Rectangle source = {
            (float)(frame * FRAME_WIDTH),
            0.0f,
            (float)FRAME_WIDTH,
            (float)FRAME_HEIGHT
        };

        // Negative source width tests horizontal flipping.
        if (flipped)
        {
            source.x += source.width;
            source.width = -source.width;
        }

        BeginDrawing();
        ClearBackground((Color){13, 19, 33, 255});

        R3D_Begin(camera);

            // Large animated fish.
            R3D_DrawTexture3DPro(
                spritesheet,
                source,
                (Vector3){0.0f, 1.1f, 0.0f},
                QuaternionIdentity(),
                (Vector2){2.5f, 2.5f},
                (Vector2){0.5f, 0.5f},
                options
            );

            // Four static frames displayed side-by-side.
            // Useful for detecting incorrect source offsets.
            for (int i = 0; i < FRAME_COUNT; i++)
            {
                Rectangle frameSource = {
                    (float)(i * FRAME_WIDTH),
                    0.0f,
                    (float)FRAME_WIDTH,
                    (float)FRAME_HEIGHT
                };

                R3D_DrawTexture3DPro(
                    spritesheet,
                    frameSource,
                    (Vector3){-2.7f + i * 1.8f, -1.5f, 0.0f},
                    QuaternionIdentity(),
                    (Vector2){1.1f, 1.1f},
                    (Vector2){0.5f, 0.5f},
                    options
                );
            }

        R3D_End();

        DrawText("MEATCORPS / R3D - KISS", 25, 22, 28, RAYWHITE);
        DrawText("TEXTURE3D SOURCE RECT VALIDATION", 25, 57, 18, SKYBLUE);

        DrawText(
            TextFormat(
                "Frame: %d / %d     Source: (%.0f, %.0f, %.0f, %.0f)",
                frame + 1, FRAME_COUNT,
                source.x, source.y, source.width, source.height
            ),
            25, 91, 18, LIGHTGRAY
        );

        DrawText(
            TextFormat(
                "SPACE PAUSE   LEFT/RIGHT FRAME   F FLIP [%s]",
                flipped ? "ON" : "OFF"
            ),
            25, 121, 17, GRAY
        );

        // Draw the original atlas using Raylib 2D for comparison.
        const int atlasX = (VIEW_W - spritesheet.width * 2) / 2;
        const int atlasY = VIEW_H - 148;

        DrawText("ORIGINAL SPRITESHEET / SELECTED FRAME", atlasX, atlasY - 27, 16, LIGHTGRAY);

        DrawTextureEx(
            spritesheet,
            (Vector2){(float)atlasX, (float)atlasY},
            0.0f, 2.0f, WHITE
        );

        DrawRectangleLinesEx(
            (Rectangle){
                (float)(atlasX + frame * FRAME_WIDTH * 2),
                (float)atlasY,
                (float)(FRAME_WIDTH * 2),
                (float)(FRAME_HEIGHT * 2)
            },
            3.0f,
            YELLOW
        );

        DrawFPS(VIEW_W - 105, 25);

        EndDrawing();
    }

    R3D_UnloadTexture3DResources();
    R3D_UnloadTexture(spritesheet);

    R3D_Close();
    CloseWindow();

    return 0;
}