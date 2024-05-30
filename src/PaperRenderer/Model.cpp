#include "Model.h"
#include "PaperRenderer.h"
#include "RHI/IndirectDraw.h"

namespace PaperRenderer
{
	//----------MODEL DEFINITIONS----------//

    Model::Model(RenderEngine *renderer, PaperMemory::DeviceAllocation *allocation, const ModelCreateInfo &creationInfo)
        :rendererPtr(renderer),
        allocationPtr(allocation)
    {
		//temporary variables for creating the singular vertex and index buffer
		std::vector<PaperMemory::Vertex> creationVertices;
		std::vector<uint32_t> creationIndices;

		//fill in variables with the input LOD data
		for(const std::unordered_map<uint32_t, std::vector<MeshInfo>>& lod : creationInfo.LODs)
		{
			LOD returnLOD;
			for(const auto& [matIndex, meshes] : lod)
			{
				std::vector<LODMesh> returnMeshes;
				for(const MeshInfo& mesh : meshes)
				{
					LODMesh returnMesh;
					returnMesh.vboOffset = creationVertices.size();
					returnMesh.vertexCount =  mesh.vertices.size();
					returnMesh.iboOffset = creationIndices.size();
					returnMesh.indexCount =  mesh.indices.size();

					creationVertices.insert(creationVertices.end(), mesh.vertices.begin(), mesh.vertices.end());
					creationIndices.insert(creationIndices.end(), mesh.indices.begin(), mesh.indices.end());

					returnMeshes.push_back(returnMesh);
				}
				returnLOD.meshes[matIndex] = returnMeshes;
			}
			LODs.push_back(returnLOD);
		}

		//AABB processing
		aabb.posX = -1000000.0f;
		aabb.negX = 1000000.0f;
		aabb.posY = -1000000.0f;
		aabb.negY = 1000000.0f;
		aabb.posZ = -1000000.0f;
		aabb.negZ = 1000000.0f;
		for(const PaperMemory::Vertex& vertex : creationVertices)
		{
			aabb.posX = std::max(vertex.position.x, aabb.posX);
			aabb.negX = std::min(vertex.position.x, aabb.negX);
			aabb.posY = std::max(vertex.position.y, aabb.posY);
			aabb.negY = std::min(vertex.position.y, aabb.negY);
			aabb.posZ = std::max(vertex.position.z, aabb.posZ);
			aabb.negZ = std::min(vertex.position.z, aabb.negZ);
		}
		
		vbo = createDeviceLocalBuffer(sizeof(PaperMemory::Vertex) * creationVertices.size(), creationVertices.data(), 
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
		ibo = createDeviceLocalBuffer(sizeof(uint32_t) * creationIndices.size(), creationIndices.data(), 
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
	}

	Model::~Model()
	{

	}

    VkDeviceSize Model::getMemoryAlignment(Device* device)
    {
		//kind of "hackish" but I'm not sure of any other way to do this
       	VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.pNext = NULL;
		bufferCreateInfo.flags = 0;
		bufferCreateInfo.size = 1000000;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkDeviceBufferMemoryRequirements bufferMemRequirements;
		bufferMemRequirements.sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS;
		bufferMemRequirements.pNext = NULL;
		bufferMemRequirements.pCreateInfo = &bufferCreateInfo;

		VkMemoryRequirements2 memRequirements = {};
		memRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		vkGetDeviceBufferMemoryRequirements(device->getDevice(), &bufferMemRequirements, &memRequirements);

		return memRequirements.memoryRequirements.alignment * 2; //alignment for vertex and index buffer
    }

    std::unique_ptr<PaperMemory::Buffer> Model::createDeviceLocalBuffer(VkDeviceSize size, void* data, VkBufferUsageFlags2KHR usageFlags)
    {
		//create staging buffer
		std::unique_ptr<PaperMemory::DeviceAllocation> stagingAllocation;
		PaperMemory::BufferInfo stagingBufferInfo = {};
		stagingBufferInfo.size = size;
		stagingBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stagingBufferInfo.queueFamilyIndices = { rendererPtr->getDevice()->getQueues().at(PaperMemory::QueueType::TRANSFER).queueFamilyIndex }; //used for transfer operation only
		PaperMemory::Buffer vboStaging(rendererPtr->getDevice()->getDevice(), stagingBufferInfo);

		//create staging allocation
		PaperMemory::DeviceAllocationInfo stagingAllocationInfo = {};
		stagingAllocationInfo.allocationSize = vboStaging.getMemoryRequirements().size; //alignment doesnt matter here since buffer and allocation are 1:1
		stagingAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		stagingAllocation = std::make_unique<PaperMemory::DeviceAllocation>(rendererPtr->getDevice()->getDevice(), rendererPtr->getDevice()->getGPU(), stagingAllocationInfo);

		//assign staging allocation and fill with information
		vboStaging.assignAllocation(stagingAllocation.get());

		//fill staging data
		PaperMemory::BufferWrite write = {};
		write.data = data;
		write.size = size;
		write.offset = 0;
		vboStaging.writeToBuffer({ write });

		//create device local buffer
		PaperMemory::BufferInfo bufferInfo = {};
		bufferInfo.size = size;
		bufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usageFlags;
		bufferInfo.queueFamilyIndices = { 
			rendererPtr->getDevice()->getQueues().at(PaperMemory::QueueType::GRAPHICS).queueFamilyIndex,
            rendererPtr->getDevice()->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex
		}; //used for graphics by raster and compute by RT. ownership transfer should probably be considered in the future
		std::unique_ptr<PaperMemory::Buffer> buffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo);

		//assign memory
		if(buffer->assignAllocation(allocationPtr) != 0)
		{
			throw std::runtime_error("Buffer device local allocation assignment failed");
		}

		//copy
		VkBufferCopy copyRegion;
		copyRegion.dstOffset = 0;
		copyRegion.srcOffset = 0;
		copyRegion.size = size;

		PaperMemory::SynchronizationInfo synchronizationInfo = {};
		synchronizationInfo.queueType = PaperMemory::QueueType::TRANSFER;
		synchronizationInfo.fence = PaperMemory::Commands::getUnsignaledFence(rendererPtr->getDevice()->getDevice());

		PaperRenderer::PaperMemory::CommandBuffer cmdBuffer = buffer->copyFromBufferRanges(
			vboStaging, rendererPtr->getDevice()->getQueues().at(PaperMemory::QueueType::TRANSFER).queueFamilyIndex, { copyRegion }, synchronizationInfo);

		//wait for fence and destroy (potential for efficiency improvements here since this is technically brute force synchronization)
		vkWaitForFences(rendererPtr->getDevice()->getDevice(), 1, &synchronizationInfo.fence, VK_TRUE, UINT64_MAX);
		vkDestroyFence(rendererPtr->getDevice()->getDevice(), synchronizationInfo.fence, nullptr);

		PaperRenderer::PaperMemory::Commands::freeCommandBuffer(rendererPtr->getDevice()->getDevice(), cmdBuffer);

		return buffer;
    }

    void Model::bindBuffers(const VkCommandBuffer& cmdBuffer) const
	{
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vbo->getBuffer(), offsets);
		vkCmdBindIndexBuffer(cmdBuffer, ibo->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
	}

	//----------MODEL INSTANCE DEFINITIONS----------//

    ModelInstance::ModelInstance(RenderEngine *renderer, Model const *parentModel, const std::vector<std::unordered_map<uint32_t, MaterialInstance *>> &materials)
        : rendererPtr(renderer),
          modelPtr(parentModel),
          materials(materials)
    {
		if(parentModel && renderer)
		{
			this->materials.resize(modelPtr->getLODs().size());
			rendererPtr->addObject(*this, meshReferences, selfIndex);
		}
    }

    ModelInstance::~ModelInstance()
    {
		if(modelPtr && rendererPtr)
		{
			rendererPtr->removeObject(*this, meshReferences, selfIndex);
		}
    }

	std::vector<char> ModelInstance::getRasterPreprocessData(uint32_t currentRequiredSize)
    {
		struct ShaderInstanceData
		{
			std::vector<ShaderLOD> shaderLODs;
			std::vector<ShaderMeshReference> shaderMeshReferences;
		};

		ShaderInstanceData instanceData = {};
		uint32_t dynamicOffset = currentRequiredSize;

		//shader LODs
		lodsOffset = dynamicOffset;
		dynamicOffset += sizeof(ShaderLOD) * modelPtr->getLODs().size();
		for(uint32_t i = 0; i < modelPtr->getLODs().size(); i++)
		{
			ShaderLOD lod = {};
			lod.meshReferenceCount = 0;
			lod.meshReferencesOffset = dynamicOffset;
			for(const auto& [matIndex, meshes] : modelPtr->getLODs().at(i).meshes)
			{
				lod.meshReferenceCount += meshes.size();
				for(const LODMesh& mesh : meshes)
				{
					instanceData.shaderMeshReferences.push_back({ meshReferences.at(&mesh)->getMeshesData().at(&mesh).shaderMeshOffset });
					dynamicOffset += sizeof(ShaderMeshReference);
				}
			}
			instanceData.shaderLODs.push_back(lod);
		}

		//copy data
		preprocessData.clear();
		uint32_t lastSize = 0;

		preprocessData.resize(preprocessData.size() + sizeof(ShaderLOD) * instanceData.shaderLODs.size());
		memcpy(preprocessData.data() + lastSize, instanceData.shaderLODs.data(), sizeof(ShaderLOD) * instanceData.shaderLODs.size());
		lastSize = preprocessData.size();
		
		preprocessData.resize(preprocessData.size() + sizeof(ShaderMeshReference) * instanceData.shaderMeshReferences.size());
		memcpy(preprocessData.data() + lastSize, instanceData.shaderMeshReferences.data(), sizeof(ShaderMeshReference) * instanceData.shaderMeshReferences.size());
		lastSize = preprocessData.size();

		return preprocessData;
    }

    ModelInstance::ShaderInputObject ModelInstance::getShaderInputObject() const
    {
		ShaderInputObject inputObject = {};
		inputObject.position = glm::vec4(transformation.position, 1.0f);
		inputObject.scale = glm::vec4(transformation.scale, 1.0f);
		inputObject.rotation = glm::mat4_cast(transformation.rotation); //TODO SHADER QUATERNION
		inputObject.bounds = modelPtr->getAABB();
		inputObject.lodCount = modelPtr->getLODs().size();
		inputObject.lodsOffset = lodsOffset;

        return inputObject;
    }

    void ModelInstance::transform(const ModelTransform &newTransform)
    {
		transformation = newTransform;
    }
}