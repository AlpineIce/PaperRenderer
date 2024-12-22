#ifndef RAY_COMMON_GLSL
#define RAY_COMMON_GLSL 1

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_query : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 4) uniform InputData
{
    mat4 projection;
    mat4 view;
    uint64_t modelDataReference;
    uint64_t frameNumber;
    uint recursionDepth;
    uint aoSamples;
    float aoRadius;
    uint shadowSamples;
    uint reflectionSamples;
} inputData;

//hit payload
struct HitPayload
{
    vec3 returnColor;
    int depth;
};

//clear value for floats (NaN)
const int depthClearMagicNumber = 0xFFFFFFFF;

//----------FROM NVIDIA----------//

void ComputeDefaultBasis(const vec3 normal, out vec3 x, out vec3 y)
{
    // ZAP's default coordinate system for compatibility
    vec3 z = normal;
    const float yz = -z.y * z.z;
    y = normalize(((abs(z.z) > 0.99999f) ? vec3(-z.x * z.y, 1.0f - z.y * z.y, yz) : vec3(-z.x * z.z, yz, 1.0f - z.z * z.z)));

    x = cross(y, z);
}

// Avoiding self intersections (see Ray Tracing Gems, Ch. 6)
//
vec3 OffsetRay(in vec3 p, in vec3 n)
{
    const float intScale   = 256.0f;
    const float floatScale = 1.0f / 65536.0f;
    const float origin     = 1.0f / 32.0f;

    ivec3 of_i = ivec3(intScale * n.x, intScale * n.y, intScale * n.z);

    vec3 p_i = vec3(intBitsToFloat(floatBitsToInt(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
                    intBitsToFloat(floatBitsToInt(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
                    intBitsToFloat(floatBitsToInt(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

    return vec3(abs(p.x) < origin ? p.x + floatScale * n.x : p_i.x,  //
                abs(p.y) < origin ? p.y + floatScale * n.y : p_i.y,  //
                abs(p.z) < origin ? p.z + floatScale * n.z : p_i.z);
}

#endif