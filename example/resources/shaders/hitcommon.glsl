#ifndef HIT_COMMON_GLSL
#define HIT_COMMON_GLSL 1

#extension GL_GOOGLE_include_directive : require

#include "raycommon.glsl"

//----------INSTANCE DESCRIPTIONS----------//

struct InstanceDescription
{
    uint modelDataOffset;
};

layout(scalar, set = 3, binding = 0) readonly buffer InstanceDescriptions
{
    InstanceDescription descriptions[];
} instanceDescriptions;

//----------MATERIAL DESCRIPTIONS----------//

struct Material
{
    //surface
    vec3 albedo; //normalized vec3
    vec3 emissive; //non-normalized
    float metallic; //normalized
    float roughness; //normalized

    //transmission
    vec3 transmission;
    float ior;
};

layout(scalar, set = 2, binding = 2) readonly buffer MaterialDefinitions
{
    Material materials[];
} materialDefinitions;

//----------MODEL DATA----------//

struct AABB
{
    float posX;
    float negX;
    float posY;
    float negY;
    float posZ;
    float negZ;
};

struct Model
{
    AABB bounds;
    uint64_t vertexAddress;
    uint64_t indexAddress;
    uint lodCount;
    uint lodsOffset;
    uint vertexStride;
};

layout(scalar, buffer_reference) readonly buffer InputModel
{
    Model model;
};

//lods
struct ModelLOD
{
    uint materialCount;
    uint meshGroupOffset;
};

layout(scalar, buffer_reference) readonly buffer ModelLODs
{
    ModelLOD LODs[];
};

//mesh group
struct ModelLODMeshGroup
{
    uint iboOffset; //only used for ray tracing
    uint vboOffset; //only used for ray tracing
};

layout(scalar, buffer_reference) readonly buffer ModelLODMeshGroups
{
    ModelLODMeshGroup groups[];
};

//----------VERTEX DATA----------//

struct Vertex
{
    vec3 pos;
    vec3 normal;
    vec2 uv;
};

layout(buffer_reference, scalar) readonly buffer Vertices { Vertex v[]; };
layout(buffer_reference, scalar) readonly buffer Indices { uint i[]; };

hitAttributeEXT vec3 attribs;

//----------HELPER FUNCTIONS----------//

struct HitInfo
{
    vec3 worldPosition;
    vec3 normal;
    vec2 uv;
};

HitInfo getHitInfo()
{
    //get instance description
    InstanceDescription instanceDescription = instanceDescriptions.descriptions[gl_InstanceID];

    //get model data
    Model model = InputModel(inputData.modelDataReference + instanceDescription.modelDataOffset).model;
    ModelLOD modelLOD0 = ModelLODs(inputData.modelDataReference + instanceDescription.modelDataOffset + model.lodsOffset).LODs[0];
    ModelLODMeshGroup modelMeshGroup = ModelLODMeshGroups(inputData.modelDataReference + instanceDescription.modelDataOffset + modelLOD0.meshGroupOffset).groups[gl_GeometryIndexEXT];

    //vertex data
    Indices indices = Indices(model.indexAddress + uint64_t(modelMeshGroup.iboOffset * 4 /* sizeof(uint32_t_) */));
    Vertices vertices = Vertices(model.vertexAddress + uint64_t(modelMeshGroup.vboOffset * model.vertexStride));

    //indices
    const uint ind0 = indices.i[(gl_PrimitiveID * 3) + 0];
    const uint ind1 = indices.i[(gl_PrimitiveID * 3) + 1];
    const uint ind2 = indices.i[(gl_PrimitiveID * 3) + 2];

    //vertices
    const Vertex v0 = vertices.v[ind0];
    const Vertex v1 = vertices.v[ind1];
    const Vertex v2 = vertices.v[ind2];

    //barycentrics, position, normal, and UVs
    const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    const vec3 pos = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
    const vec3 worldPosition = vec3(gl_ObjectToWorldEXT * vec4(pos, 1.0));

    const vec3 localNormal = v0.normal * barycentrics.x + v1.normal * barycentrics.y + v2.normal * barycentrics.z; //named localNormal to avoid mistakes
    const vec3 worldNormal = normalize(vec3(localNormal * gl_WorldToObjectEXT));

    const vec2 UVs = v0.uv * barycentrics.x + v1.uv * barycentrics.y + v2.uv * barycentrics.z;

    HitInfo returnInfo;
    returnInfo.worldPosition = worldPosition;
    returnInfo.normal = worldNormal;
    returnInfo.uv = UVs;

    return returnInfo;
}

#endif