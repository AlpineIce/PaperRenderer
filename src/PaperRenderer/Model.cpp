#include "Model.h"
#include "RenderPass.h"
#include "PaperRenderer.h"
#include "Material.h"
#include "AccelerationStructure.h"
#include "IndirectDraw.h"

namespace PaperRenderer
{
	//----------MODEL DEFINITIONS----------//

    Model::Model(RenderEngine *renderer, DeviceAllocation *allocation, const ModelCreateInfo &creationInfo)
        :rendererPtr(renderer),
        allocationPtr(allocation)
    {
		//temporary variables for creating the singular vertex and index buffer
		std::vector<char> creationVerticesData;
		std::vector<uint32_t> creationIndices;

		//AABB processing
		aabb.posX = -1000000.0f;
		aabb.negX = 1000000.0f;
		aabb.posY = -1000000.0f;
		aabb.negY = 1000000.0f;
		aabb.posZ = -1000000.0f;
		aabb.negZ = 1000000.0f;

		//vertex data
		vertexAttributes = creationInfo.vertexAttributes;
		vertexDescription = creationInfo.vertexDescription;
		vertexPositionOffset = creationInfo.vertexPositionOffset;

		//fill in variables with the input LOD data
		VkDeviceSize dynamicVertexOffset = 0;
		for(const ModelLODInfo& lod : creationInfo.LODs)
		{
			LOD returnLOD;
			for(const auto& [matIndex, meshes] : lod.lodData)
			{
				returnLOD.meshMaterialData.resize(lod.lodData.size());
				for(const MeshInfo& mesh : meshes)
				{
					LODMesh returnMesh;
					returnMesh.vboOffset = dynamicVertexOffset;
					returnMesh.vertexCount =  mesh.verticesData.size() / vertexDescription.stride;
					returnMesh.iboOffset = creationIndices.size();
					returnMesh.indexCount =  mesh.indices.size();

					creationVerticesData.insert(creationVerticesData.end(), mesh.verticesData.begin(), mesh.verticesData.end());
					creationIndices.insert(creationIndices.end(), mesh.indices.begin(), mesh.indices.end());

					dynamicVertexOffset += returnMesh.vertexCount;

					returnLOD.meshMaterialData.at(matIndex).push_back(returnMesh);

					//AABB processing
					uint32_t vertexCount = creationVerticesData.size() / vertexDescription.stride;
					for(uint32_t i = 0; i < vertexCount; i++)
					{
						const glm::vec3& vertexPosition = *(glm::vec3*)(creationVerticesData.data() + (i * vertexDescription.stride) + vertexPositionOffset);

						aabb.posX = std::max(vertexPosition.x, aabb.posX);
						aabb.negX = std::min(vertexPosition.x, aabb.negX);
						aabb.posY = std::max(vertexPosition.y, aabb.posY);
						aabb.negY = std::min(vertexPosition.y, aabb.negY);
						aabb.posZ = std::max(vertexPosition.z, aabb.posZ);
						aabb.negZ = std::min(vertexPosition.z, aabb.negZ);
					}
				}
			}
			LODs.push_back(returnLOD);
		}
		
		vbo = createDeviceLocalBuffer(creationVerticesData.size(), creationVerticesData.data(), 
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
		ibo = createDeviceLocalBuffer(sizeof(uint32_t) * creationIndices.size(), creationIndices.data(), 
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

		//set shader data and add to renderer
		setShaderData();
		rendererPtr->addModelData(this);
	}

	Model::~Model()
	{
		rendererPtr->removeModelData(this);
		
		vbo.reset();
		ibo.reset();
	}

	void Model::setShaderData()
    {
		std::vector<char> newData;
		uint32_t dynamicOffset = 0;

		//model data
		dynamicOffset += sizeof(ShaderModel);
		newData.resize(dynamicOffset);

		ShaderModel shaderModel = {};
		shaderModel.bounds = aabb;
		shaderModel.lodCount = LODs.size();
		shaderModel.lodsOffset = dynamicOffset;

		memcpy(newData.data(), &shaderModel, sizeof(ShaderModel));

		//model LODs data
		dynamicOffset += sizeof(ShaderModelLOD) * LODs.size();
		newData.resize(dynamicOffset);

		for(uint32_t lodIndex = 0; lodIndex < LODs.size(); lodIndex++)
		{
			ShaderModelLOD modelLOD = {};
			modelLOD.materialCount = LODs.at(lodIndex).meshMaterialData.size();
			modelLOD.meshGroupsOffset = dynamicOffset;

			memcpy(newData.data() + shaderModel.lodsOffset + sizeof(ShaderModelLOD) * lodIndex, &modelLOD, sizeof(ShaderModelLOD));
			
			//LOD mesh groups data
			dynamicOffset += sizeof(ShaderModelLODMeshGroup) * LODs.at(lodIndex).meshMaterialData.size();
			newData.resize(dynamicOffset);

			for(uint32_t matIndex = 0; matIndex < LODs.at(lodIndex).meshMaterialData.size(); matIndex++)
			{
				ShaderModelLODMeshGroup materialMeshGroup = {};
				materialMeshGroup.meshCount = LODs.at(lodIndex).meshMaterialData.at(matIndex).size();
				materialMeshGroup.meshesOffset = dynamicOffset;
				materialMeshGroup.iboOffset = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(0).iboOffset;
				materialMeshGroup.vboOffset = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(0).vboOffset;

				memcpy(newData.data() + modelLOD.meshGroupsOffset + sizeof(ShaderModelLODMeshGroup) * matIndex, &materialMeshGroup, sizeof(ShaderModelLODMeshGroup));

				//LOD mesh group meshes data
				dynamicOffset += sizeof(ShaderModelMeshData) * LODs.at(lodIndex).meshMaterialData.at(matIndex).size();
				newData.resize(dynamicOffset);

				for(uint32_t meshIndex = 0; meshIndex < LODs.at(lodIndex).meshMaterialData.at(matIndex).size(); meshIndex++)
				{
					ShaderModelMeshData meshData = {};
					meshData.vboOffset = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex).vboOffset;
					meshData.vertexCount = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex).vertexCount;
					meshData.iboOffset = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex).iboOffset;
					meshData.indexCount = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex).indexCount;
					
					memcpy(newData.data() + materialMeshGroup.meshesOffset + sizeof(ShaderModelMeshData) * meshIndex, &meshData, sizeof(ShaderModelMeshData));
				}
			}
		}
        
		shaderData = newData;
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
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;

		VkDeviceBufferMemoryRequirements bufferMemRequirements;
		bufferMemRequirements.sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS;
		bufferMemRequirements.pNext = NULL;
		bufferMemRequirements.pCreateInfo = &bufferCreateInfo;

		VkMemoryRequirements2 memRequirements = {};
		memRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
		vkGetDeviceBufferMemoryRequirements(device->getDevice(), &bufferMemRequirements, &memRequirements);

		return memRequirements.memoryRequirements.alignment * 2; //alignment for vertex and index buffer
    }

    std::unique_ptr<Buffer> Model::createDeviceLocalBuffer(VkDeviceSize size, void *data, VkBufferUsageFlags2KHR usageFlags)
    {
		//create staging buffer
		std::unique_ptr<DeviceAllocation> stagingAllocation;
		BufferInfo stagingBufferInfo = {};
		stagingBufferInfo.size = size;
		stagingBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stagingBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
		Buffer vboStaging(rendererPtr->getDevice()->getDevice(), stagingBufferInfo);

		//create staging allocation
		DeviceAllocationInfo stagingAllocationInfo = {};
		stagingAllocationInfo.allocationSize = vboStaging.getMemoryRequirements().size; //alignment doesnt matter here since buffer and allocation are 1:1
		stagingAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		stagingAllocation = std::make_unique<DeviceAllocation>(rendererPtr->getDevice()->getDevice(), rendererPtr->getDevice()->getGPU(), stagingAllocationInfo);

		//assign staging allocation and fill with information
		vboStaging.assignAllocation(stagingAllocation.get());

		//fill staging data
		BufferWrite write = {};
		write.data = data;
		write.size = size;
		write.offset = 0;
		vboStaging.writeToBuffer({ write });

		//create device local buffer
		BufferInfo bufferInfo = {};
		bufferInfo.size = size;
		bufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | usageFlags;
		bufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
		std::unique_ptr<Buffer> buffer = std::make_unique<Buffer>(rendererPtr->getDevice()->getDevice(), bufferInfo);

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

		SynchronizationInfo synchronizationInfo = {};
		synchronizationInfo.queueType = QueueType::TRANSFER;
		synchronizationInfo.fence = Commands::getUnsignaledFence(rendererPtr->getDevice()->getDevice());

		PaperRenderer::CommandBuffer cmdBuffer = buffer->copyFromBufferRanges(vboStaging, { copyRegion }, synchronizationInfo);

		//wait for fence and destroy (potential for efficiency improvements here since this is technically brute force synchronization)
		vkWaitForFences(rendererPtr->getDevice()->getDevice(), 1, &synchronizationInfo.fence, VK_TRUE, UINT64_MAX);
		vkDestroyFence(rendererPtr->getDevice()->getDevice(), synchronizationInfo.fence, nullptr);

		std::vector<CommandBuffer> cmdBuffers = { cmdBuffer };
		PaperRenderer::Commands::freeCommandBuffers(rendererPtr->getDevice()->getDevice(), cmdBuffers);

		return buffer;
    }

    void Model::bindBuffers(const VkCommandBuffer& cmdBuffer) const
	{
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vbo->getBuffer(), offsets);
		vkCmdBindIndexBuffer(cmdBuffer, ibo->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
	}

	//----------MODEL INSTANCE DEFINITIONS----------//

    ModelInstance::ModelInstance(RenderEngine *renderer, Model const* parentModel)
        : rendererPtr(renderer),
          modelPtr(parentModel)
    {
		rendererPtr->addObject(this);
    }

    ModelInstance::~ModelInstance()
    {
		rendererPtr->removeObject(this);
    }

	void ModelInstance::setRenderPassInstanceData(RenderPass const* renderPass)
    {
		std::vector<char> newData;
		newData.reserve(renderPassSelfReferences.at(renderPass).renderPassInstanceData.size());
		uint32_t dynamicOffset = 0;

		dynamicOffset += sizeof(LODMaterialData) * modelPtr->getLODs().size();
		newData.resize(dynamicOffset);

		for(uint32_t lodIndex = 0; lodIndex < modelPtr->getLODs().size(); lodIndex++)
		{
			dynamicOffset = DeviceAllocation::padToMultiple(dynamicOffset, 8);

			LODMaterialData lodMaterialData = {};
			lodMaterialData.meshGroupsOffset = dynamicOffset;

			memcpy(newData.data() + sizeof(LODMaterialData) * lodIndex, &lodMaterialData, sizeof(LODMaterialData));
			
			//LOD mesh groups data
			dynamicOffset += sizeof(MaterialMeshGroup) * modelPtr->getLODs().at(lodIndex).meshMaterialData.size();
			newData.resize(dynamicOffset);

			for(uint32_t matIndex = 0; matIndex < modelPtr->getLODs().at(lodIndex).meshMaterialData.size(); matIndex++)
			{
				MaterialMeshGroup materialMeshGroup = {};
				materialMeshGroup.bufferAddress = renderPassSelfReferences.at(renderPass).meshGroupReferences.at(&modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex))->getBufferAddress();
				materialMeshGroup.bufferFrameOffsetsOffset = dynamicOffset;
				dynamicOffset += sizeof(uint32_t) * Commands::getFrameCount();
				newData.resize(dynamicOffset);
				materialMeshGroup.indirectDrawDatasOffset = dynamicOffset;

				memcpy(newData.data() + lodMaterialData.meshGroupsOffset + sizeof(MaterialMeshGroup) * matIndex, &materialMeshGroup, sizeof(MaterialMeshGroup));

				//buffer frame offsets
				std::vector<uint32_t> bufferFrameOffsets;
				for(const VkDeviceSize& offset : renderPassSelfReferences.at(renderPass).meshGroupReferences.at(&modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex))->getBufferFrameOffsets())
				{
					bufferFrameOffsets.push_back((uint32_t)offset);
				}
				memcpy(newData.data() + materialMeshGroup.bufferFrameOffsetsOffset, bufferFrameOffsets.data(), bufferFrameOffsets.size() * sizeof(uint32_t));

				//LOD mesh group meshes data
				dynamicOffset += sizeof(IndirectDrawData) * modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex).size();
				newData.resize(dynamicOffset);

				for(uint32_t meshIndex = 0; meshIndex < modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex).size(); meshIndex++)
				{
					LODMesh const* lodMeshPtr = &modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex);
					IndirectDrawData indirectDrawData = {};
					indirectDrawData.drawCountsOffset = 
						renderPassSelfReferences.at(renderPass).meshGroupReferences.at(&modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex))->getMeshesData().at(lodMeshPtr).drawCountsOffset;
					indirectDrawData.drawCommandsOffset = 
						renderPassSelfReferences.at(renderPass).meshGroupReferences.at(&modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex))->getMeshesData().at(lodMeshPtr).drawCommandsOffset;
					indirectDrawData.outputObjectsOffset = 
						renderPassSelfReferences.at(renderPass).meshGroupReferences.at(&modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex))->getMeshesData().at(lodMeshPtr).outputObjectsOffset;
				
					memcpy(newData.data() + materialMeshGroup.indirectDrawDatasOffset + sizeof(IndirectDrawData) * meshIndex, &indirectDrawData, sizeof(IndirectDrawData));
				}
			}
		}

		renderPassSelfReferences.at(renderPass).renderPassInstanceData = newData;
    }

    std::vector<char> ModelInstance::getRenderPassInstanceData(RenderPass const* renderPass) const
    {
        return renderPassSelfReferences.at(renderPass).renderPassInstanceData;
    }

    ModelInstance::ShaderModelInstance ModelInstance::getShaderInstance() const
    {
		ShaderModelInstance shaderModelInstance = {};
		shaderModelInstance.position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		shaderModelInstance.qRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		shaderModelInstance.scale = glm::vec4(1.0f);
		shaderModelInstance.modelDataOffset = modelPtr->shaderDataLocation; //TODO

		return shaderModelInstance;
    }

    void ModelInstance::setTransformation(const ModelTransformation &newTransformation)
    {
		ModelInstance::ShaderModelInstance& shaderObject = *((ModelInstance::ShaderModelInstance*)rendererPtr->hostInstancesDataBuffer->getHostDataPtr() + rendererSelfIndex);
		shaderObject.position = glm::vec4(newTransformation.position, 1.0f);
		shaderObject.scale = glm::vec4(newTransformation.scale, 1.0f);
		shaderObject.qRotation = newTransformation.rotation;
    }

    ModelTransformation ModelInstance::getTransformation() const
    {
		const ModelInstance::ShaderModelInstance& shaderObject = *((ModelInstance::ShaderModelInstance*)rendererPtr->hostInstancesDataBuffer->getHostDataPtr() + rendererSelfIndex);

		ModelTransformation transformation;
		transformation.position = shaderObject.position;
		transformation.rotation = shaderObject.qRotation;
		transformation.scale = shaderObject.scale;

		return transformation;
    }

    /*bool ModelInstance::getVisibility(RenderPass *renderPass) const
    {
        const bool& visibility = false;

		return visibility;
    }

	void ModelInstance::setVisibility(RenderPass *renderPass, bool newVisibility)
    {

    }*/
}