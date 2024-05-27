#include "Model.h"
#include "PaperRenderer.h"

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

			returnLOD.shaderLOD.meshCount = 0;
			for(auto [matIndex, meshes] : returnLOD.meshes)
			{
				returnLOD.shaderLOD.meshCount += meshes.size();
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

    std::vector<ShaderLOD> Model::getLODData(uint32_t currentBufferSize)
    {
        this->LODDataOffset = currentBufferSize;
		std::vector<ShaderLOD> returnData;
		for(auto& lod : LODs) //iterate LODs
		{
			for(const auto& [matIndex, lodMeshes] : lod.meshes) //iterate LODs
			{
				returnData.push_back(lod.shaderLOD); //lod count is set on model initialization
			}
		}
		return returnData;
    }

    std::vector<LODMesh> Model::getMeshLODData(uint32_t currentBufferSize)
    {
		this->bufferMeshLODsOffset = currentBufferSize;
		std::vector<LODMesh> returnData;
		for(auto& lod : LODs) //iterate LODs
		{
			lod.shaderLOD.meshesLocationOffset = currentBufferSize + returnData.size() * sizeof(LODMesh);
			for(const auto& [index, lodMeshes] : lod.meshes) //iterate LODs
			{
				for(auto& mesh : lodMeshes) //creates copy
				{
					returnData.push_back(mesh); //mesh data is set by indirect draw handler 
				}
			}
		}
		return returnData;
    }

	//----------MODEL INSTANCE DEFINITIONS----------//

    ModelInstance::ModelInstance(RenderEngine* renderer, Model* parentModel, const std::vector<std::unordered_map<uint32_t, MaterialInstance*>>& materials)
		:rendererPtr(renderer),
		modelPtr(parentModel),
		materials(materials)
    {
		if(parentModel && renderer)
		{
			this->materials.resize(modelPtr->getLODs().size());
			this->meshReferences.resize(modelPtr->getLODs().size());
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

    void ModelInstance::transform(const ModelTransform &newTransform)
    {
		transformation = newTransform;
    }
}