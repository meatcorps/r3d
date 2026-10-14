
/*
 * GPU GLITCH DISPLACEMENT
 *
 * Intentionally violent vertex deformation.
 * No helper functions or custom varyings:
 * compatible with the current surface-effect composer.
 */

uniform float amplitude = 0.75;
uniform float frequency = 9.0;
uniform float seed = 0.0;

void vertex()
{
    // World position gives each object a different random pattern.
    vec3 origin = vec3(MATRIX_MODEL[3]);

    // Quantized time: the mesh snaps to a new scrambled shape.
    float tick = floor(TIME * frequency);

    vec3 key = POSITION * 37.17
    + origin * 3.13
    + vec3(seed + tick * 0.713);

    float rx = fract(sin(dot(key, vec3(127.1, 311.7, 74.7))) * 43758.5453);
    float ry = fract(sin(dot(key, vec3(269.5, 183.3, 246.1))) * 22578.1459);
    float rz = fract(sin(dot(key, vec3(113.5, 271.9, 124.6))) * 19642.3491);

    vec3 randomOffset = vec3(rx, ry, rz) * 2.0 - 1.0;

    // Occasionally amplify the explosion.
    float burst = step(0.62, fract(TIME * 0.7 + origin.x * 0.11));
    float power = amplitude * mix(0.35, 1.0, burst);

    POSITION += randomOffset * power;
}
