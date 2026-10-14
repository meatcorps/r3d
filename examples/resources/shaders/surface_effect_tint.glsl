
// A test-only, externally loaded surface effect.
// The effect composer will namespace this shader's identifiers when combining
// the same effect multiple times within one draw submission.

uniform vec3 tint = vec3(1.0, 1.0, 1.0);
uniform float strength = 0.0;

void fragment()
{
    ALBEDO = mix(ALBEDO, tint, clamp(strength, 0.0, 1.0));
}
