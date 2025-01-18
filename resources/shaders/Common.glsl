#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require //for pointer arithmetic

//----------MODEL INPUT DATA----------//

//model
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

//----------MODEL INSTANCE INPUT DATA----------//

//model instance
struct ModelInstance
{
    vec3 position;
    vec3 scale;
    vec4 qRotation; //quaternion
    uint modelDataOffset;
};

layout(scalar, set = 0, binding = 1) readonly buffer InputInstances
{
    ModelInstance modelInstances[];
} inputInstances;

//----------FUNCTIONS----------//

mat3x4 getModelMatrix(ModelInstance modelInstance)
{
    //rotation
    const vec4 q = modelInstance.qRotation;
    const mat3 qMat = mat3(
        vec3(
            2.0 * (q[0] * q[0] + q[1] * q[1]) - 1.0,
            2.0 * (q[1] * q[2] - q[0] * q[3]),
            2.0 * (q[1] * q[3] + q[0] * q[2])
        ),
        vec3(
            2.0 * (q[1] * q[2] + q[0] * q[3]),
            2.0 * (q[0] * q[0] + q[2] * q[2]) - 1.0,
            2.0 * (q[2] * q[3] - q[0] * q[1])
        ),
        vec3(
            2.0 * (q[1] * q[3] - q[0] * q[2]),
            2.0 * (q[2] * q[3] + q[0] * q[1]),
            2.0 * (q[0] * q[0] + q[3] * q[3]) - 1.0
        )
    );

    //scale
    const mat3 scaleMat = mat3(
        vec3(modelInstance.scale.x, 0.0, 0.0),
        vec3(0.0, modelInstance.scale.y, 0.0),
        vec3(0.0, 0.0, modelInstance.scale.z)
    );

    //composition of rotation and scale
    const mat3 scaleRotMat = scaleMat * qMat;
    
    //return composition of all transformations
    return mat3x4(
        vec4(scaleRotMat[0], modelInstance.position.x),
        vec4(scaleRotMat[1], modelInstance.position.y),
        vec4(scaleRotMat[2], modelInstance.position.z)
    );
}

bool isInBounds(ModelInstance modelInstance, Model model, mat3x4 modelMatrix, mat4 projection, mat4 view)
{
    AABB bounds = model.bounds;
                
    //construct 8 vertices
    vec3 vertices[8];
    vertices[0] = vec3(bounds.posX, bounds.posY, bounds.posZ);      // + + +
    vertices[1] = vec3(bounds.posX, bounds.posY, bounds.negZ);      // + + -
    vertices[2] = vec3(bounds.negX, bounds.posY, bounds.posZ);      // - + +
    vertices[3] = vec3(bounds.posX, bounds.negY, bounds.posZ);      // + - +
    vertices[4] = vec3(bounds.posX, bounds.negY, bounds.negZ);      // + - -
    vertices[5] = vec3(bounds.negX, bounds.posY, bounds.negZ);      // - + -
    vertices[6] = vec3(bounds.negX, bounds.negY, bounds.posZ);      // - - +
    vertices[7] = vec3(bounds.negX, bounds.negY, bounds.negZ);      // - - -

    //transform vertices and construct AABB
    AABB aabb;
    aabb.negX = 1000000.0f;
    aabb.negY = 1000000.0f;
    aabb.negZ = 1000000.0f;
    aabb.posX = -1000000.0f;
    aabb.posY = -1000000.0f;
    aabb.posZ = -1000000.0f;

    for(int i = 0; i < 8; i++)
    {
        vertices[i] = (view * vec4(transpose(modelMatrix) * vec4(vertices[i], 1.0), 1.0)).xyz;
        aabb.posX = max(vertices[i].x, aabb.posX);
        aabb.negX = min(vertices[i].x, aabb.negX);
        aabb.posY = max(vertices[i].y, aabb.posY);
        aabb.negY = min(vertices[i].y, aabb.negY);
        aabb.posZ = max(vertices[i].z, aabb.posZ);
        aabb.negZ = min(vertices[i].z, aabb.negZ);
    }

    //create culling frustum
    mat4 projectionT = transpose(projection);
    vec4 frustumX = (projectionT[3] + projectionT[0]) / length((projectionT[3] + projectionT[0]).xyz);
    vec4 frustumY = (projectionT[3] + projectionT[1]) / length((projectionT[3] + projectionT[1]).xyz);

    //visibility test
    bool visible = true;
    visible = visible && aabb.negZ < 0.0; //z test (cut everything behind)
    visible = visible && !((aabb.posX < ((frustumX.z / frustumX.x) * -aabb.negZ)) || //check left
                           (aabb.negX > ((frustumX.z / frustumX.x) * aabb.negZ))); //check right
    visible = visible && !((aabb.posY < (frustumY.y * aabb.negZ)) || //check top
                           (aabb.negY > (frustumY.y * -aabb.negZ))); //check bottom
    
	return visible;
}

uint getLODLevel(ModelInstance modelInstance, Model model, vec4 camPos)
{
    //get largest OBB extent to be used as size
    AABB bounds = model.bounds;
    float xLength = bounds.posX - bounds.negX;
    float yLength = bounds.posY - bounds.negY;
    float zLength = bounds.posZ - bounds.negZ;

    float worldSize = 0.0;
    worldSize = max(worldSize, xLength);
    worldSize = max(worldSize, yLength);
    worldSize = max(worldSize, zLength);

    float cameraDistance = length(modelInstance.position.xyz - camPos.xyz);
    
    uint lodLevel = uint(floor(inversesqrt(worldSize * 10.0) * sqrt(cameraDistance)));

    return lodLevel;
}