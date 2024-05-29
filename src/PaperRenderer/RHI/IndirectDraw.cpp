#include "IndirectDraw.h"
#include "../Model.h"

namespace PaperRenderer
{
    CommonMeshGroup::CommonMeshGroup(Device *device, DescriptorAllocator* descriptor, RasterPipeline const* pipeline)
        :devicePtr(device),
        descriptorsPtr(descriptor),
        pipelinePtr(pipeline)
    {
    }

    CommonMeshGroup::~CommonMeshGroup()
    {
        for(auto& [instance, meshes] : instanceMeshes)
        {
            removeInstanceMeshes(instance);
        }
    }

    std::vector<char> CommonMeshGroup::getPreprocessData(uint32_t currentRequiredSize)
    {
        //helper struct
        struct MeshGroupData
        {
            ShaderMesh shaderMesh = {};
            uint32_t size = 0;
        };

        //build shader meshes
        std::vector<MeshGroupData> shaderMeshesData;
        uint32_t dynamicOffset = currentRequiredSize;
        for(auto& [mesh, meshData] : meshesData)
        {
            uint32_t lastSize = dynamicOffset;

            //shader mesh
            meshData.shaderMeshOffset = dynamicOffset;
            dynamicOffset += PaperMemory::DeviceAllocation::padToMultiple(sizeof(ShaderMesh), 4); //draw counts offset (below) must be padded to 4 bytes per spec

            ShaderMesh shaderMesh;
            shaderMesh.vboOffset = mesh->vboOffset;
            shaderMesh.vertexCount = mesh->vertexCount;
            shaderMesh.iboOffset = mesh->iboOffset;
            shaderMesh.indexCount = mesh->indexCount;
            shaderMesh.padding = 420;

            //get size requirements
            uint32_t instanceCount = meshData.instanceCount;

            shaderMesh.drawCountsOffset = dynamicOffset;
            meshData.drawCountsOffset = dynamicOffset;
            dynamicOffset = PaperMemory::DeviceAllocation::padToMultiple(dynamicOffset + sizeof(uint32_t), 4); //draw commands offset (below) must be padded to 4 bytes per spec
            shaderMesh.drawCommandsOffset = dynamicOffset;
            meshData.drawCommandsOffset = dynamicOffset;
            dynamicOffset = PaperMemory::DeviceAllocation::padToMultiple(dynamicOffset + sizeof(VkDrawIndexedIndirectCommand) * instanceCount,
                devicePtr->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment); //output objects (below) must be padded to min storage buffer alignment
            shaderMesh.outputObjectsOffset = dynamicOffset;
            meshData.outputObjectsOffset = dynamicOffset;
            dynamicOffset += sizeof(ShaderOutputObject) * instanceCount;
            
            //add to vector
            shaderMeshesData.push_back({ shaderMesh, dynamicOffset - lastSize });
        }

        //copy necessary data and reqired regions for shader
        preprocessData.clear();
        for(const MeshGroupData& shaderMeshData : shaderMeshesData)
        {
            uint32_t shaderMeshDataLocation = preprocessData.size();

            preprocessData.resize(preprocessData.size() + shaderMeshData.size, 0);
            memcpy(preprocessData.data() + shaderMeshDataLocation, &shaderMeshData.shaderMesh, sizeof(ShaderMesh));
        }

        return preprocessData;
    }

    void CommonMeshGroup::addInstanceMeshes(ModelInstance* instance, std::vector<LODMesh const*> instanceMeshes)
    {
        addAndRemoveLock.lock();
        
        this->instanceMeshes[instance].insert(this->instanceMeshes[instance].end(), instanceMeshes.begin(), instanceMeshes.end());
        for(LODMesh const* mesh : instanceMeshes)
        {
            if(!meshesData.count(mesh))
            {
                meshesData[mesh].parentModelPtr = instance->getParentModelPtr();
            }

            instance->meshReferences[mesh] = this;
            meshesData.at(mesh).instanceCount++;
        }

        addAndRemoveLock.unlock();
    }

    void CommonMeshGroup::removeInstanceMeshes(ModelInstance *instance)
    {
        addAndRemoveLock.lock();
        
        for(LODMesh const* mesh : this->instanceMeshes.at(instance))
        {
            instance->meshReferences.at(mesh) = NULL;
            meshesData.at(mesh).instanceCount--;
        }
        instanceMeshes.erase(instance);

        addAndRemoveLock.unlock();
    }

    void CommonMeshGroup::draw(const VkCommandBuffer &cmdBuffer, const VkBuffer& dataBuffer, uint32_t currentFrame)
    {
        for(const auto& [mesh, meshData] : meshesData)
        {
            if(!meshData.parentModelPtr) continue; //null safety

            //get new descriptor set
            VkDescriptorSet objDescriptorSet = descriptorsPtr->allocateDescriptorSet(pipelinePtr->getDescriptorSetLayouts().at(DescriptorScopes::RASTER_OBJECT), currentFrame);
            
            //write uniforms
            VkDescriptorBufferInfo descriptorInfo = {};
            descriptorInfo.buffer = dataBuffer;
            descriptorInfo.offset = meshData.outputObjectsOffset;
            descriptorInfo.range = sizeof(ShaderOutputObject) * meshData.instanceCount;
            BuffersDescriptorWrites write = {};
            write.binding = 0;
            write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.infos.push_back(descriptorInfo);

            DescriptorWrites descriptorWritesInfo = {};
            descriptorWritesInfo.bufferWrites = { write };
            DescriptorAllocator::writeUniforms(devicePtr->getDevice(), objDescriptorSet, descriptorWritesInfo);

            //bind set
            DescriptorBind bindingInfo = {};
            bindingInfo.descriptorScope = DescriptorScopes::RASTER_OBJECT;
            bindingInfo.set = objDescriptorSet;
            bindingInfo.layout = pipelinePtr->getLayout();
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            DescriptorAllocator::bindSet(devicePtr->getDevice(), cmdBuffer, bindingInfo);

            //bind vbo and ibo and send draw calls (draw calls should be computed in the performCulling() function)
            meshData.parentModelPtr->bindBuffers(cmdBuffer);
            vkCmdDrawIndexedIndirectCount(
                cmdBuffer,
                dataBuffer,
                meshData.drawCommandsOffset,
                dataBuffer,
                meshData.drawCountsOffset,
                meshData.instanceCount,
                sizeof(VkDrawIndexedIndirectCommand));
        }
    }
}