#pragma usage opaque shadow probe

uniform sampler2D noiseTexture;
uniform float strength = 0.5;

varying vec2 localUv;

float curve(float value)
{
    return sin(value * 6.2831853);
}

void vertex()
{
    localUv = TEXCOORD;
    POSITION.x += curve(strength + TIME * 0.25) * 0.2;
}

void fragment()
{
    vec3 sampleColor = texture(noiseTexture, localUv).rgb;
    EMISSION += sampleColor * strength * 0.5;
}