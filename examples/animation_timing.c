#include <r3d/r3d_animation_player.h>
#include <math.h>
#include <stdio.h>

typedef struct {
    int loops;
    int finished;
} TestEvents;

static void on_animation_event(R3D_AnimationPlayer* player, R3D_AnimationEvent event, int animIndex, void* userData)
{
    (void)player;
    (void)animIndex;

    TestEvents* events = userData;

    if (event == R3D_ANIM_EVENT_LOOPED) events->loops++;
    if (event == R3D_ANIM_EVENT_FINISHED) events->finished++;
}

static int check_time(const char* name, float actual, float expected)
{
    bool passed = fabsf(actual - expected) < 0.0001f;
    printf("%s: %s (actual %.4f, expected %.4f)\n", name, passed ? "PASS" : "FAIL", actual, expected);
    return passed ? 0 : 1;
}

int main(void)
{
    R3D_Animation animation = {
        .duration = 200.0f,
        .ticksPerSecond = 100.0f
    };

    R3D_AnimationLib library = {
        .animations = &animation,
        .count = 1
    };

    R3D_AnimationState state = {
        .speed = 1.0f,
        .play = true,
        .loop = true
    };

    TestEvents events = {0};

    R3D_AnimationPlayer player = {
        .animLib = library,
        .states = &state,
        .activeAnimIndex = 0,
        .eventCallback = on_animation_event,
        .eventUserData = &events
    };

    int failures = 0;

    failures += check_time("Duration", R3D_GetAnimationDurationSeconds(player, 0), 2.0f);

    R3D_SetAnimationTimeSeconds(&player, 0, 0.5f);
    R3D_AdvanceAnimationTime(&player, 6.25f);
    failures += check_time("Forward multi-loop", R3D_GetAnimationTimeSeconds(player, 0), 0.75f);
    failures += check_time("Forward loop event", (float)events.loops, 1.0f);

    R3D_SetAnimationSpeed(&player, 0, -1.0f);
    R3D_SetAnimationTimeSeconds(&player, 0, 1.5f);
    R3D_AdvanceAnimationTime(&player, 6.25f);
    failures += check_time("Reverse multi-loop", R3D_GetAnimationTimeSeconds(player, 0), 1.25f);
    failures += check_time("Reverse loop event", (float)events.loops, 2.0f);

    R3D_SetAnimationTimeSeconds(&player, 0, 1.0f);
    R3D_AdvanceAnimationTime(&player, 1.0f);
    failures += check_time("Reverse exact boundary", R3D_GetAnimationTimeSeconds(player, 0), 2.0f);

    R3D_SetAnimationLoop(&player, 0, false);
    R3D_SetAnimationTimeSeconds(&player, 0, 1.0f);
    R3D_AdvanceAnimationTime(&player, 3.0f);
    failures += check_time("Reverse non-looping", R3D_GetAnimationTimeSeconds(player, 0), 0.0f);
    failures += check_time("Reverse finished event", (float)events.finished, 1.0f);

    state.play = true;
    R3D_SetAnimationSpeed(&player, 0, 1.0f);
    R3D_SetAnimationTimeSeconds(&player, 0, 1.0f);
    R3D_AdvanceAnimationTime(&player, 3.0f);
    failures += check_time("Forward non-looping", R3D_GetAnimationTimeSeconds(player, 0), 2.0f);
    failures += check_time("Forward finished event", (float)events.finished, 2.0f);

    state.play = true;
    R3D_SetAnimationSpeed(&player, 0, 0.0f);
    R3D_SetAnimationTimeSeconds(&player, 0, 1.0f);
    R3D_AdvanceAnimationTime(&player, 10.0f);
    failures += check_time("Zero playback speed", R3D_GetAnimationTimeSeconds(player, 0), 1.0f);

    printf("\nTiming tests: %s\n", failures == 0 ? "ALL PASSED" : "FAILED");

    return failures == 0 ? 0 : 1;
}