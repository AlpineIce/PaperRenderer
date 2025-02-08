#ifndef HIT_COMMON_GLSL
#define HIT_COMMON_GLSL 1

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#include "raycommon.glsl"
#include "../../../resources/shaders/Common.glsl"

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

//----------VERTEX DATA----------//

struct Vertex
{
    vec3 pos;
    vec3 normal;
    vec2 uv;
};

layout(buffer_reference, scalar) readonly buffer Vertices 
{
    Vertex v[];
};

layout(buffer_reference, scalar) readonly buffer Indices32
{
    uint i;
};
layout(buffer_reference, scalar) readonly buffer Indices16
{
    uint16_t i;
};
layout(buffer_reference, scalar) readonly buffer Indices8
{
    uint8_t i;
};

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
    
    //indices
    uint ind0;
    uint ind1;
    uint ind2;
    if(modelMeshGroup.iboStride == 4)
    {
        ind0 = Indices32(model.indexAddress + uint64_t(modelMeshGroup.iboOffset + (modelMeshGroup.iboStride * ((gl_PrimitiveID * 3) + 0)))).i;
        ind1 = Indices32(model.indexAddress + uint64_t(modelMeshGroup.iboOffset + (modelMeshGroup.iboStride * ((gl_PrimitiveID * 3) + 1)))).i;
        ind2 = Indices32(model.indexAddress + uint64_t(modelMeshGroup.iboOffset + (modelMeshGroup.iboStride * ((gl_PrimitiveID * 3) + 2)))).i;
        
    }
    else if(modelMeshGroup.iboStride == 2)
    {
        ind0 = uint(Indices16(model.indexAddress + uint64_t(modelMeshGroup.iboOffset + (modelMeshGroup.iboStride * ((gl_PrimitiveID * 3) + 0)))).i);
        ind1 = uint(Indices16(model.indexAddress + uint64_t(modelMeshGroup.iboOffset + (modelMeshGroup.iboStride * ((gl_PrimitiveID * 3) + 1)))).i);
        ind2 = uint(Indices16(model.indexAddress + uint64_t(modelMeshGroup.iboOffset + (modelMeshGroup.iboStride * ((gl_PrimitiveID * 3) + 2)))).i);
    }
    else //1
    {
        ind0 = uint(Indices8(model.indexAddress + uint64_t(modelMeshGroup.iboOffset + (modelMeshGroup.iboStride * ((gl_PrimitiveID * 3) + 0)))).i);
        ind1 = uint(Indices8(model.indexAddress + uint64_t(modelMeshGroup.iboOffset + (modelMeshGroup.iboStride * ((gl_PrimitiveID * 3) + 1)))).i);
        ind2 = uint(Indices8(model.indexAddress + uint64_t(modelMeshGroup.iboOffset + (modelMeshGroup.iboStride * ((gl_PrimitiveID * 3) + 2)))).i);
    }

    //vertices
    Vertices vertices = Vertices(model.vertexAddress + uint64_t(modelMeshGroup.vboOffset));
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