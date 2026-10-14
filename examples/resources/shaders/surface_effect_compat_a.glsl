#pragma usage opaque shadow probe

uniform sampler2D noiseTexture;
uniform float strength = 0.5;

varying vec2 localUv;

float curve(float value)
{
    return value * value;
}

void vertex()
{
    localUv = TEXCOORD;
    POSITION.y += curve(strength) * 0.15;
}

void fragment()
{
    vec3 sampleColor = texture(noiseTexture, localUv).rgb;
    ALBEDO = mix(ALBEDO, sampleColor, strength);
}