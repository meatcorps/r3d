
#include <r3d/r3d_geometry_deformation.h>

#include <math.h>
#include <limits.h>

static float clamp01(float value)
{
    return fmaxf(0.0f, fminf(1.0f, value));
}

static Vector3 clamp_axes(Vector3 v)
{
    return (Vector3){
        clamp01(v.x), clamp01(v.y), clamp01(v.z)
    };
}

static Vector3 normalized_or(Vector3 value, Vector3 fallback)
{
    float length = sqrtf(
        value.x * value.x +
        value.y * value.y +
        value.z * value.z
    );

    if (length < 0.00001f) return fallback;

    return (Vector3){
        value.x / length,
        value.y / length,
        value.z / length
    };
}

static R3D_BuiltRenderMode make_deformation(const char* source)
{
    R3D_BuiltRenderMode built = {0};
    built.effect = R3D_LoadSurfaceEffectFromMemory(source);

    if (!built.effect)
        TraceLog(LOG_ERROR, "R3D: Failed to build geometry deformation");

    return built;
}

static void set_float(R3D_BuiltRenderMode built, const char* name, float value)
{
    if (built.effect)
        R3D_SetSurfaceEffectUniform(built.effect, name, &value);
}

static void set_vec3(R3D_BuiltRenderMode built, const char* name, Vector3 value)
{
    if (built.effect)
        R3D_SetSurfaceEffectUniform(built.effect, name, &value);
}

static void set_int(R3D_BuiltRenderMode built, const char* name, int value)
{
    if (built.effect)
        R3D_SetSurfaceEffectUniform(built.effect, name, &value);
}

/* ------------------------------------------------------------
 * Inflate / Deflate
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildInflate(
    float strength, Vector3 axisInfluence, bool planar, bool continuous)
{
    static const char source[] =
        "uniform float strength = 0.0;\n"
        "uniform vec3 axisInfluence = vec3(1.0,1.0,1.0);\n"
        "uniform int planar = 0;\n"
        "uniform int continuous = 1;\n"
        "void vertex()\n"
        "{\n"
        "    if (continuous != 0)\n"
        "    {\n"
        "        vec3 direction = planar != 0\n"
        "            ? vec3(POSITION.xy, 0.0)\n"
        "            : POSITION;\n"
        "        POSITION += direction * axisInfluence * strength;\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        vec3 direction = normalize(NORMAL);\n"
        "        if (planar != 0)\n"
        "        {\n"
        "            float d = length(POSITION.xy);\n"
        "            direction = d > 0.00001\n"
        "                ? vec3(POSITION.xy / d, 0.0)\n"
        "                : vec3(0.0);\n"
        "        }\n"
        "        POSITION += direction * axisInfluence * strength;\n"
        "    }\n"
        "}\n";

    R3D_BuiltRenderMode built = make_deformation(source);
    int planarValue = planar ? 1 : 0;

    set_float(built, "strength", strength);
    set_vec3(built, "axisInfluence", clamp_axes(axisInfluence));
    set_int(built, "continuous", continuous ? 1 : 0);

    if (built.effect)
        R3D_SetSurfaceEffectUniform(built.effect, "planar", &planarValue);

    return built;
}

/* ------------------------------------------------------------
 * Bulge
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildBulge(
    Vector3 center, float radius, float strength, float falloff, bool continuous)
{
    static const char source[] =
        "uniform vec3 center = vec3(0.0);\n"
        "uniform float radius = 1.0;\n"
        "uniform float strength = 0.0;\n"
        "uniform float falloff = 2.0;\n"
        "uniform int continuous = 1;\n"
        "void vertex()\n"
        "{\n"
        "    vec3 offset = POSITION - center;\n"
        "    float d = length(offset);\n"
        "    float t = clamp(1.0 - d / max(radius,0.0001),0.0,1.0);\n"
        "    float influence = pow(t, max(falloff,0.0001));\n"
        "    vec3 direction = normalize(NORMAL);\n"
        "    if (continuous != 0)\n"
        "    {\n"
        "        direction = d > 0.00001\n"
        "            ? offset / d : vec3(0.0);\n"
        "    }\n"
        "    POSITION += direction * strength * influence;\n"
        "}\n";

    R3D_BuiltRenderMode built = make_deformation(source);

    set_vec3(built, "center", center);
    set_float(built, "radius", fmaxf(radius, 0.0001f));
    set_float(built, "strength", strength);
    set_float(built, "falloff", fmaxf(falloff, 0.0001f));
    set_int(built, "continuous", continuous ? 1 : 0);

    return built;
}

/* ------------------------------------------------------------
 * Wave / Wobble
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildWave(
    Vector3 axis, Vector3 direction,
    float amplitude, float frequency, float speed)
{
    static const char source[] =
        "uniform vec3 axis = vec3(1.0,0.0,0.0);\n"
        "uniform vec3 direction = vec3(0.0,1.0,0.0);\n"
        "uniform float amplitude = 0.0;\n"
        "uniform float frequency = 5.0;\n"
        "uniform float speed = 1.0;\n"
        "void vertex()\n"
        "{\n"
        "    float phase = dot(POSITION,axis)*frequency + TIME*speed;\n"
        "    POSITION += direction * sin(phase) * amplitude;\n"
        "}\n";

    R3D_BuiltRenderMode built = make_deformation(source);

    set_vec3(built, "axis",
        normalized_or(axis, (Vector3){1,0,0}));
    set_vec3(built, "direction",
        normalized_or(direction, (Vector3){0,1,0}));

    set_float(built, "amplitude", amplitude);
    set_float(built, "frequency", frequency);
    set_float(built, "speed", speed);

    return built;
}

/* ------------------------------------------------------------
 * Twist
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildTwist(
    Vector3 axis, float angle, float start, float end)
{
    static const char source[] =
        "uniform vec3 axis = vec3(0.0,1.0,0.0);\n"
        "uniform float angle = 0.0;\n"
        "uniform float start = -1.0;\n"
        "uniform float end = 1.0;\n"
        "vec3 twistRotate(vec3 v,vec3 a,float t)\n"
        "{\n"
        "    return v*cos(t)+cross(a,v)*sin(t)\n"
        "        +a*dot(a,v)*(1.0-cos(t));\n"
        "}\n"
        "void vertex()\n"
        "{\n"
        "    float h = dot(POSITION,axis);\n"
        "    float f = clamp((h-start)/max(end-start,0.0001),0.0,1.0);\n"
        "    float t = angle*f;\n"
        "    POSITION = twistRotate(POSITION,axis,t);\n"
        "    NORMAL = twistRotate(NORMAL,axis,t);\n"
        "    TANGENT.xyz = twistRotate(TANGENT.xyz,axis,t);\n"
        "}\n";

    R3D_BuiltRenderMode built = make_deformation(source);

    set_vec3(built, "axis",
        normalized_or(axis, (Vector3){0,1,0}));
    set_float(built, "angle", angle);
    set_float(built, "start", start);
    set_float(built, "end", fmaxf(end, start + 0.0001f));

    return built;
}

/* ------------------------------------------------------------
 * Bend
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildBend(
    Vector3 axis, Vector3 direction, float curvature)
{
    static const char source[] =
        "uniform vec3 axis = vec3(0.0,1.0,0.0);\n"
        "uniform vec3 direction = vec3(1.0,0.0,0.0);\n"
        "uniform float curvature = 0.0;\n"
        "vec3 bendRotate(vec3 v,vec3 a,float t)\n"
        "{\n"
        "    return v*cos(t)+cross(a,v)*sin(t)\n"
        "        +a*dot(a,v)*(1.0-cos(t));\n"
        "}\n"
        "void vertex()\n"
        "{\n"
        "    if (abs(curvature)<0.00001) return;\n"
        "    float h = dot(POSITION,axis);\n"
        "    float t = h*curvature;\n"
        "    vec3 pivotAxis = normalize(cross(axis,direction));\n"
        "    vec3 radial = POSITION-axis*h;\n"
        "    vec3 curve = axis*(sin(t)/curvature)\n"
        "        +direction*((1.0-cos(t))/curvature);\n"
        "    POSITION = curve + bendRotate(radial,pivotAxis,t);\n"
        "    NORMAL = bendRotate(NORMAL,pivotAxis,t);\n"
        "    TANGENT.xyz = bendRotate(TANGENT.xyz,pivotAxis,t);\n"
        "}\n";

    Vector3 a = normalized_or(axis, (Vector3){0,1,0});
    float dot = a.x*direction.x+a.y*direction.y+a.z*direction.z;

    Vector3 perpendicular = {
        direction.x-a.x*dot,
        direction.y-a.y*dot,
        direction.z-a.z*dot
    };

    Vector3 fallback = fabsf(a.y) < 0.9f
        ? (Vector3){0,1,0}
        : (Vector3){1,0,0};

    if (fabsf(perpendicular.x)+fabsf(perpendicular.y)+
        fabsf(perpendicular.z) < 0.00001f)
    {
        dot = a.x*fallback.x+a.y*fallback.y+a.z*fallback.z;
        perpendicular = (Vector3){
            fallback.x-a.x*dot,
            fallback.y-a.y*dot,
            fallback.z-a.z*dot
        };
    }

    R3D_BuiltRenderMode built = make_deformation(source);

    set_vec3(built, "axis", a);
    set_vec3(built, "direction",
        normalized_or(perpendicular,(Vector3){1,0,0}));
    set_float(built, "curvature", curvature);

    return built;
}

/* ------------------------------------------------------------
 * Ripple
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildRipple(
    Vector3 center, float amplitude,
    float frequency, float speed, float decay, bool continuous)
{
    static const char source[] =
        "uniform vec3 center = vec3(0.0);\n"
        "uniform float amplitude = 0.0;\n"
        "uniform float frequency = 8.0;\n"
        "uniform float speed = 3.0;\n"
        "uniform float decay = 0.5;\n"
        "uniform int continuous = 1;\n"
        "void vertex()\n"
        "{\n"
        "    vec3 offset = POSITION - center;\n"
        "    float d = length(offset);\n"
        "    float phase = d*frequency - TIME*speed;\n"
        "    float attenuation = exp(-d*max(decay,0.0));\n"
        "    vec3 direction = normalize(NORMAL);\n"
        "    if (continuous != 0)\n"
        "    {\n"
        "        direction = d > 0.00001\n"
        "            ? offset / d : vec3(0.0);\n"
        "    }\n"
        "    POSITION += direction * sin(phase)\n"
        "        * amplitude * attenuation;\n"
        "}\n";

    R3D_BuiltRenderMode built = make_deformation(source);

    set_vec3(built, "center", center);
    set_float(built, "amplitude", amplitude);
    set_float(built, "frequency", frequency);
    set_float(built, "speed", speed);
    set_float(built, "decay", fmaxf(0.0f, decay));
    set_int(built, "continuous", continuous ? 1 : 0);

    return built;
}

/* ------------------------------------------------------------
 * Smooth Noise Displacement
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildNoiseDisplacement(
    Vector3 scale, float strength, float speed, bool continuous)
{
    static const char source[] =
        "uniform vec3 scale = vec3(3.0);\n"
        "uniform float strength = 0.0;\n"
        "uniform float speed = 1.0;\n"
        "uniform int continuous = 1;\n"
        "float noiseHash(vec3 p)\n"
        "{\n"
        "    return fract(sin(dot(p,vec3(127.1,311.7,74.7)))\n"
        "        *43758.5453);\n"
        "}\n"
        "float noiseValue(vec3 p)\n"
        "{\n"
        "    vec3 i=floor(p);\n"
        "    vec3 f=fract(p);\n"
        "    f=f*f*(3.0-2.0*f);\n"
        "    float a=mix(noiseHash(i),noiseHash(i+vec3(1,0,0)),f.x);\n"
        "    float b=mix(noiseHash(i+vec3(0,1,0)),\n"
        "        noiseHash(i+vec3(1,1,0)),f.x);\n"
        "    float c=mix(noiseHash(i+vec3(0,0,1)),\n"
        "        noiseHash(i+vec3(1,0,1)),f.x);\n"
        "    float d=mix(noiseHash(i+vec3(0,1,1)),\n"
        "        noiseHash(i+vec3(1,1,1)),f.x);\n"
        "    return mix(mix(a,b,f.y),mix(c,d,f.y),f.z);\n"
        "}\n"
    "void vertex()\n"
    "{\n"
    "    vec3 p=POSITION*scale+vec3(0.0,0.0,TIME*speed);\n"
    "    if (continuous != 0)\n"
    "    {\n"
    "        vec3 displacement = vec3(\n"
    "            noiseValue(p + vec3(31.7,4.2,9.1)),\n"
    "            noiseValue(p + vec3(8.3,42.1,17.5)),\n"
    "            noiseValue(p + vec3(19.4,7.8,53.2))\n"
    "        ) * 2.0 - 1.0;\n"
    "        POSITION += displacement * strength;\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        float n = noiseValue(p)*2.0-1.0;\n"
    "        POSITION += normalize(NORMAL)*n*strength;\n"
    "    }\n"
    "}\n";

    R3D_BuiltRenderMode built = make_deformation(source);

    set_vec3(built, "scale", scale);
    set_float(built, "strength", strength);
    set_float(built, "speed", speed);
    set_int(built, "continuous", continuous ? 1 : 0);

    return built;
}

/* ------------------------------------------------------------
 * Vertex Jitter
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildVertexJitter(
    float strength, float rate, float seed, bool continuous)
{
    static const char source[] =
    "uniform float strength = 0.0;\n"
    "uniform float rate = 8.0;\n"
    "uniform float seed = 1.0;\n"
    "uniform int continuous = 1;\n"
    "void vertex()\n"
    "{\n"
    "    float frame=floor(TIME*max(rate,0.0));\n"
    "    vec3 key;\n"
    "    if (continuous != 0)\n"
    "    {\n"
    "        key=floor(POSITION*2048.0+0.5);\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        float id=float(gl_VertexID);\n"
    "        key=vec3(id,id*0.73,id*1.37);\n"
    "    }\n"
    "    vec3 p=key+vec3(seed,frame+seed,seed*0.37);\n"
    "    vec3 n=fract(sin(vec3(\n"
    "        dot(p,vec3(127.1,311.7,74.7)),\n"
    "        dot(p,vec3(269.5,183.3,246.1)),\n"
    "        dot(p,vec3(113.5,271.9,124.6))))*43758.5453);\n"
    "    POSITION+=(n*2.0-1.0)*strength;\n"
    "}\n";

    R3D_BuiltRenderMode built = make_deformation(source);

    set_float(built, "strength", strength);
    set_float(built, "rate", fmaxf(0.0f, rate));
    set_float(built, "seed", seed);
    set_int(built, "continuous", continuous ? 1 : 0);

    return built;
}

/* ------------------------------------------------------------
 * Vertex Snap
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildVertexSnap(
    float step, Vector3 axisInfluence)
{
    static const char source[] =
        "uniform float stepSize = 0.1;\n"
        "uniform vec3 axisInfluence = vec3(1.0);\n"
        "void vertex()\n"
        "{\n"
        "    vec3 snapped=floor(POSITION/max(stepSize,0.0001)+0.5)\n"
        "        *max(stepSize,0.0001);\n"
        "    POSITION=mix(POSITION,snapped,axisInfluence);\n"
        "}\n";

    R3D_BuiltRenderMode built = make_deformation(source);

    set_float(built, "stepSize", fmaxf(step, 0.0001f));
    set_vec3(built, "axisInfluence", clamp_axes(axisInfluence));

    return built;
}

/* ------------------------------------------------------------
 * Triangle Explosion
 * ------------------------------------------------------------ */

R3D_BuiltRenderMode R3D_RenderModeBuildExplode(
    float amount, float seed)
{
    static const char source[] =
        "uniform float amount = 0.0;\n"
        "uniform float seed = 1.0;\n"
        "void vertex()\n"
        "{\n"
        "    // Prepared meshes have three consecutive vertices per face.\n"
        "    int localVertex=gl_VertexID-uMorphVertexBase;\n"
        "    float face=float(localVertex/3)+seed;\n"
        "    vec3 p=vec3(face,face*0.73,face*1.37);\n"
        "    vec3 n=fract(sin(vec3(\n"
        "        dot(p,vec3(127.1,311.7,74.7)),\n"
        "        dot(p,vec3(269.5,183.3,246.1)),\n"
        "        dot(p,vec3(113.5,271.9,124.6))))*43758.5453);\n"
        "    vec3 direction=n*2.0-1.0;\n"
        "    float len=length(direction);\n"
        "    if (len>0.00001)\n"
        "        POSITION+=direction/len*amount;\n"
        "}\n";

    R3D_BuiltRenderMode built = make_deformation(source);

    set_float(built, "amount", amount);
    set_float(built, "seed", seed);

    return built;
}

/* ------------------------------------------------------------
 * Triangle mesh preparation
 * ------------------------------------------------------------ */

R3D_MeshData R3D_PrepareMeshDataForExplosion(R3D_MeshData source)
{
    R3D_MeshData result = {0};

    if (!source.vertices || source.vertexCount <= 0)
        return result;

    const bool indexed = source.indexCount > 0;
    const int count = indexed ? source.indexCount : source.vertexCount;

    if (indexed && !source.indices)
        return result;

    if (count <= 0 || (count % 3) != 0)
    {
        TraceLog(LOG_WARNING,
            "R3D: Explosion preparation requires triangle data");
        return result;
    }

    // Validate before allocating or copying.
    for (int i = 0; i < count; i++)
    {
        uint32_t index = indexed ? source.indices[i] : (uint32_t)i;

        if (index >= (uint32_t)source.vertexCount)
        {
            TraceLog(LOG_ERROR, "R3D: Invalid explosion mesh index");
            return result;
        }
    }

    result = R3D_LoadMeshData(count, 0);
    if (!result.vertices) return result;

    for (int i = 0; i < count; i++)
    {
        uint32_t index = indexed ? source.indices[i] : (uint32_t)i;

        // Copies positions, UVs, normals, tangents, colors,
        // bone indices and bone weights unchanged.
        result.vertices[i] = source.vertices[index];
    }

    result.vertexCount = count;
    result.indexCount = 0;

    return result;
}

/* ------------------------------------------------------------
 * Conservative bounds expansion
 * ------------------------------------------------------------ */

static void expand_bounds(BoundingBox* bounds, float margin)
{
    if (!bounds || !(margin > 0.0f) || !isfinite(margin))
        return;

    bounds->min.x -= margin;
    bounds->min.y -= margin;
    bounds->min.z -= margin;

    bounds->max.x += margin;
    bounds->max.y += margin;
    bounds->max.z += margin;
}

void R3D_ExpandMeshBoundsForDeformation(
    R3D_Mesh* mesh, float margin)
{
    if (mesh)
        expand_bounds(&mesh->aabb, margin);
}

void R3D_ExpandModelBoundsForDeformation(
    R3D_Model* model, float margin)
{
    if (!model) return;

    expand_bounds(&model->aabb, margin);

    if (model->hasAnimatedAabb)
        expand_bounds(&model->animatedAabb, margin);

    if (model->meshes)
    {
        for (int i = 0; i < model->meshCount; i++)
            expand_bounds(&model->meshes[i].aabb, margin);
    }
}
