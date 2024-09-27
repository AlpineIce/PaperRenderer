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
    uint lodCount;
    uint lodsOffset;
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
    uint meshCount;
    uint meshesOffset;
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
    vec4 position; //w as padding
    vec4 scale;
    vec4 qRotation; //quaternion
    uint modelDataOffset;
};

layout(scalar, set = 0, binding = 1) readonly buffer InputInstances
{
    ModelInstance modelInstances[];
} inputInstances;

//----------FUNCTIONS----------//

mat4 getModelMatrix(ModelInstance modelInstance)
{
    //from the GLM library
    mat3 qMat;

    //rotation
    vec4 q = modelInstance.qRotation;
    float qxx = q.x * q.x;
    float qyy = q.y * q.y;
    float qzz = q.z * q.z;
    float qxz = q.x * q.z;
    float qxy = q.x * q.y;
    float qyz = q.y * q.z;
    float qwx = q.w * q.x;
    float qwy = q.w * q.y;
    float qwz = q.w * q.z;

    qMat[0] = vec3(1.0 - 2.0 * (qyy + qzz), 2.0 * (qxy + qwz), 2.0 * (qxz - qwy));
    //qMat[0][0] = 1.0 - 2.0 * (qyy + qzz);
    //qMat[0][1] = 2.0 * (qxy + qwz);
    //qMat[0][2] = 2.0 * (qxz - qwy);

    qMat[1] = vec3(2.0 * (qxy - qwz), 1.0 - 2.0 * (qxx + qzz), 2.0 * (qyz + qwx));
    //qMat[1][0] = 2.0 * (qxy - qwz);         
    //qMat[1][1] = 1.0 - 2.0 * (qxx + qzz);
    //qMat[1][2] = 2.0 * (qyz + qwx);
    
    qMat[2] = vec3(2.0 * (qxz + qwy), 2.0 * (qyz - qwx), 1.0 - 2.0 * (qxx + qyy));
    //qMat[2][0] = 2.0 * (qxz + qwy);
    //qMat[2][1] = 2.0 * (qyz - qwx);
    //qMat[2][2] = 1.0 - 2.0 * (qxx + qyy);

    //scale
    mat3 scaleMat;
    scaleMat[0] = vec3(modelInstance.scale.x, 0.0, 0.0);
    scaleMat[1] = vec3(0.0, modelInstance.scale.y, 0.0);
    scaleMat[2] = vec3(0.0, 0.0, modelInstance.scale.z);

    //composition of rotation and scale
    mat3 scaleRotMat = scaleMat * qMat;

    mat4 result;
    result[0] = vec4(scaleRotMat[0], 0.0);
    result[1] = vec4(scaleRotMat[1], 0.0);
    result[2] = vec4(scaleRotMat[2], 0.0);
    result[3][3] = 1.0;

    //position
    result[3][0] = modelInstance.position.x;
    result[3][1] = modelInstance.position.y;
    result[3][2] = modelInstance.position.z;

    //other
    result[0][3] = 0.0;
    result[1][3] = 0.0;
    result[2][3] = 0.0;
    
    return result;
}

bool isInBounds(ModelInstance modelInstance, Model model, mat4 modelMatrix, mat4 projection, mat4 view)
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
        vertices[i] = (view * modelMatrix * vec4(vertices[i], 1.0)).xyz;
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