
/*
 * Minimal GLSL 330 definitions for AMD FidelityFX FSR1.
 * The EASU and RCAS algorithms remain in AMD's unmodified ffx_fsr1.h.
 * Only the 32-bit floating-point paths are enabled.
 */

#define A_GPU 1
#define A_GLSL 1
#define A_STATIC

#define AP1 bool
#define AF1 float
#define AF2 vec2
#define AF3 vec3
#define AF4 vec4
#define AU1 uint
#define AU2 uvec2
#define AU4 uvec4
#define ASU2 ivec2

#define AF1_(x) float(x)
#define AF2_(x) vec2(x)
#define AF3_(x) vec3(x)
#define AF4_(x) vec4(x)
#define AU1_(x) uint(x)

#define AF1_AU1(x) uintBitsToFloat(x)
#define AF2_AU2(x) uintBitsToFloat(x)
#define AU1_AF1(x) floatBitsToUint(x)

#define outAU4 out uvec4
#define varAF2(x) vec2 x
#define initAF2(x, y) vec2(x, y)

// Only con.x is used by the 32-bit RCAS path.
// The packed FP16 constant is not required.
#define AU1_AH2_AF2(x) 0u

float ARcpF1(float x)
{
    return 1.0 / x;
}

float AExp2F1(float x)
{
    return exp2(x);
}

float ASatF1(float x)
{
    return clamp(x, 0.0, 1.0);
}

vec3 ASatF3(vec3 x)
{
    return clamp(x, vec3(0.0), vec3(1.0));
}

float AFractF1(float x)
{
    return fract(x);
}

float AMax3F1(float x, float y, float z)
{
    return max(x, max(y, z));
}

vec3 AMax3F3(vec3 x, vec3 y, vec3 z)
{
    return max(x, max(y, z));
}

float AMin3F1(float x, float y, float z)
{
    return min(x, min(y, z));
}

vec3 AMin3F3(vec3 x, vec3 y, vec3 z)
{
    return min(x, min(y, z));
}

// AMD's reciprocal/reciprocal-square-root approximations.
float APrxLoRcpF1(float x)
{
    return uintBitsToFloat(0x7ef07ebbu - floatBitsToUint(x));
}

float APrxMedRcpF1(float x)
{
    float b = uintBitsToFloat(0x7ef19fffu - floatBitsToUint(x));
    return b * (-b * x + 2.0);
}

vec3 APrxMedRcpF3(vec3 x)
{
    vec3 b = uintBitsToFloat(uvec3(0x7ef19fffu) - floatBitsToUint(x));
    return b * (-b * x + vec3(2.0));
}

float APrxLoRsqF1(float x)
{
    return uintBitsToFloat(0x5f347d74u - (floatBitsToUint(x) >> 1u));
}

vec3 AGtZeroF3(vec3 x)
{
    return vec3(greaterThan(x, vec3(0.0)));
}
