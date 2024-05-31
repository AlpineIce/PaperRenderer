#pragma once
#include "Memory/VulkanResources.h"
#include "Descriptor.h"
#include "Pipeline.h"
#include "glm/gtx/quaternion.hpp"

#include <unordered_map>

namespace PaperRenderer
{
    struct InstancedMeshData
    {
        struct LODMesh const* meshPtr;
        uint32_t** shaderMeshOffsetPtr; //only necessary for performance through avoiding the unordered map "search"
    };

    class CommonMeshGroup
    {
    private:
        struct ShaderMesh
        {
            uint32_t vboOffset;
            uint32_t vertexCount;
            uint32_t iboOffset;
            uint32_t indexCount;

            uint32_t drawCountsOffset;
            uint32_t drawCommandsOffset;
            uint32_t outputObjectsOffset;
            uint32_t padding;
        };

        struct ShaderLODData
        {
            uint32_t meshesOffset;
            uint32_t meshCount;
        };

        struct ShaderOutputObject
        {
            glm::mat4 modelMatrix;
            glm::mat4 transformMatrix;
        };

        struct MeshInstancesData
        {
            class Model const* parentModelPtr = NULL;
            uint32_t shaderMeshOffset = 0;
            uint32_t instanceCount = 0;
            uint32_t drawCountsOffset = 0;
            uint32_t drawCommandsOffset = 0;
            uint32_t outputObjectsOffset = 0;
        };

        std::mutex addAndRemoveLock;
        std::vector<char> preprocessData;
        std::unordered_map<struct LODMesh const*, MeshInstancesData> meshesData;
        std::unordered_map<class ModelInstance*, std::vector<InstancedMeshData>> instanceMeshes;

        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        RasterPipeline const* pipelinePtr;

    public:
        CommonMeshGroup(Device *device, DescriptorAllocator* descriptor, RasterPipeline const* pipeline);
        ~CommonMeshGroup();

        std::vector<char> getPreprocessData(uint32_t currentRequiredSize); //should initialize this data chunk to 0

        void addInstanceMeshes(class ModelInstance* instance, const std::vector<InstancedMeshData>& instanceMeshesData);
        void removeInstanceMeshes(class ModelInstance* instance);

        void draw(const VkCommandBuffer& cmdBuffer, const VkBuffer& dataBuffer, uint32_t currentFrame);

        const std::unordered_map<struct LODMesh const*, MeshInstancesData>& getMeshesData() const { return meshesData; }
    };
}