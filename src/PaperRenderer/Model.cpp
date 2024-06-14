#include "Model.h"
#include "RenderPass.h"
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
		std::vector<char> creationVerticesData;
		std::vector<uint32_t> creationIndices;

		//AABB processing
		aabb.posX = -1000000.0f;
		aabb.negX = 1000000.0f;
		aabb.posY = -1000000.0f;
		aabb.negY = 1000000.0f;
		aabb.posZ = -1000000.0f;
		aabb.negZ = 1000000.0f;

		//fill in variables with the input LOD data
		for(const std::unordered_map<uint32_t, std::vector<MeshInfo>>& lod : creationInfo.LODs)
		{
			LOD returnLOD;
			for(const auto& [matIndex, meshes] : lod)
			{
				returnLOD.meshMaterialData.resize(lod.size());
				for(const MeshInfo& mesh : meshes)
				{
					LODMesh returnMesh;
					returnMesh.vertexPositionOffset = mesh.vertexPositionOffset;
					returnMesh.vboOffset = creationVerticesData.size();
					returnMesh.vertexCount =  mesh.verticesData.size();
					returnMesh.iboOffset = creationIndices.size();
					returnMesh.indexCount =  mesh.indices.size();
					
					creationVerticesData.insert(creationVerticesData.end(), mesh.verticesData.begin(), mesh.verticesData.end());
					creationIndices.insert(creationIndices.end(), mesh.indices.begin(), mesh.indices.end());

					returnLOD.meshMaterialData.at(matIndex).push_back(returnMesh);

					//AABB processing
					uint32_t vertexCount = creationVerticesData.size() / mesh.vertexDescription.stride;
					for(uint32_t i = 0; i < vertexCount; i++)
					{
						const glm::vec3& vertexPosition = *(glm::vec3*)(creationVerticesData.data() + (i * mesh.vertexDescription.stride) + mesh.vertexPositionOffset);

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

				memcpy(newData.data() + modelLOD.meshGroupsOffset + sizeof(ShaderModelLODMeshGroup) * matIndex, &materialMeshGroup, sizeof(ShaderModelLODMeshGroup));

				//LOD mesh group meshes data
				dynamicOffset += sizeof(ShaderModelMeshData) * LODs.at(lodIndex).meshMaterialData.at(matIndex).size();
				newData.resize(dynamicOffset);

				for(uint32_t meshIndex = 0; meshIndex < LODs.at(lodIndex).meshMaterialData.at(matIndex).size(); meshIndex++)
				{
					ShaderModelMeshData meshData = {};
					meshData.iboOffset = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex).iboOffset;
					meshData.indexCount = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex).indexCount;
					meshData.vboOffset = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex).vboOffset;
					meshData.vertexCount = LODs.at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex).vertexCount;
					
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

    std::unique_ptr<PaperMemory::Buffer> Model::createDeviceLocalBuffer(VkDeviceSize size, void *data, VkBufferUsageFlags2KHR usageFlags)
    {
		//create staging buffer
		std::unique_ptr<PaperMemory::DeviceAllocation> stagingAllocation;
		PaperMemory::BufferInfo stagingBufferInfo = {};
		stagingBufferInfo.size = size;
		stagingBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stagingBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
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
		bufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
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

		PaperRenderer::PaperMemory::CommandBuffer cmdBuffer = buffer->copyFromBufferRanges(vboStaging, { copyRegion }, synchronizationInfo);

		//wait for fence and destroy (potential for efficiency improvements here since this is technically brute force synchronization)
		vkWaitForFences(rendererPtr->getDevice()->getDevice(), 1, &synchronizationInfo.fence, VK_TRUE, UINT64_MAX);
		vkDestroyFence(rendererPtr->getDevice()->getDevice(), synchronizationInfo.fence, nullptr);

		std::vector<PaperMemory::CommandBuffer> cmdBuffers = { cmdBuffer };
		PaperRenderer::PaperMemory::Commands::freeCommandBuffers(rendererPtr->getDevice()->getDevice(), cmdBuffers);

		return buffer;
    }

    void Model::bindBuffers(const VkCommandBuffer& cmdBuffer) const
	{
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vbo->getBuffer(), offsets);
		vkCmdBindIndexBuffer(cmdBuffer, ibo->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
	}

	//----------MODEL INSTANCE DEFINITIONS----------//

    void ModelInstance::setRenderPassInstanceData(RenderPass const* renderPass)
    {
		std::vector<char> newData;
		uint32_t dynamicOffset = 0;

		for(uint32_t lodIndex = 0; lodIndex < modelPtr->getLODs().size(); lodIndex++)
		{
			LODMaterialData lodMaterialData = {};
			lodMaterialData.meshGroupsOffset = dynamicOffset;

			memcpy(newData.data() + sizeof(LODMaterialData) * lodIndex, &lodMaterialData, sizeof(LODMaterialData));
			
			//LOD mesh groups data
			dynamicOffset += sizeof(MaterialMeshGroup) * modelPtr->getLODs().at(lodIndex).meshMaterialData.size();
			newData.resize(dynamicOffset);

			for(uint32_t matIndex = 0; matIndex < modelPtr->getLODs().at(lodIndex).meshMaterialData.size(); matIndex++)
			{
				MaterialMeshGroup materialMeshGroup = {};
				materialMeshGroup.indirectDrawDatasOffset = dynamicOffset;

				memcpy(newData.data() + lodMaterialData.meshGroupsOffset + sizeof(MaterialMeshGroup) * matIndex, &materialMeshGroup, sizeof(MaterialMeshGroup));

				//LOD mesh group meshes data
				dynamicOffset += sizeof(IndirectDrawData) * modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex).size();
				newData.resize(dynamicOffset);

				for(uint32_t meshIndex = 0; meshIndex < modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex).size(); meshIndex++)
				{
					LODMesh const* lodMeshPtr = &modelPtr->getLODs().at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex);
					IndirectDrawData indirectDrawData = {}; 
					indirectDrawData.drawCountsOffset = renderPassSelfReferences.at(renderPass).meshGroupReferences.at(lodMeshPtr)->getMeshesData().at(lodMeshPtr).drawCountsOffset;
					indirectDrawData.drawCommandsOffset = renderPassSelfReferences.at(renderPass).meshGroupReferences.at(lodMeshPtr)->getMeshesData().at(lodMeshPtr).drawCommandsOffset;
					indirectDrawData.outputObjectsOffset = renderPassSelfReferences.at(renderPass).meshGroupReferences.at(lodMeshPtr)->getMeshesData().at(lodMeshPtr).outputObjectsOffset;
					
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

    ModelInstance::ShaderModelInstance ModelInstance::getShaderInstance() const
    {

		ShaderModelInstance shaderModelInstance = {};
		shaderModelInstance.position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		shaderModelInstance.qRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		shaderModelInstance.scale = glm::vec4(1.0f);
		shaderModelInstance.modelDataOffset = 0; //TODO

		return shaderModelInstance;
		
		/*std::vector<char> newData;
		uint32_t dynamicOffset = 0;

		dynamicOffset += sizeof(ShaderModelInstance);
		newData.resize(dynamicOffset);

		ShaderModelInstance instanceData = {};

		//shader LODs
		lodsOffset = dynamicOffset;
		dynamicOffset += sizeof(ShaderLOD) * modelPtr->getLODs().size();
		for(uint32_t lodIndex = 0; lodIndex < modelPtr->getLODs().size(); lodIndex++)
		{
			ShaderLOD lod = {};
			lod.meshReferenceCount = 0;
			lod.meshReferencesOffset = dynamicOffset;
			for(uint32_t matIndex = 0; matIndex < modelPtr->getLODs()[lodIndex].meshMaterialData.size(); matIndex++)
			{
				lod.meshReferenceCount += modelPtr->getLODs()[lodIndex].meshMaterialData[matIndex].size();
				for(uint32_t meshIndex = 0; meshIndex < modelPtr->getLODs()[lodIndex].meshMaterialData[matIndex].size(); meshIndex++)
				{
					instanceData.shaderMeshReferences.push_back({ *shaderMeshOffsetReferences[lodIndex][matIndex][meshIndex] });
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

		shaderdata = newData;*/
    }

    void ModelInstance::setTransformation(const ModelTransformation &newTransformation)
    {
		ModelInstance::ShaderModelInstance& shaderObject = *((ModelInstance::ShaderModelInstance*)rendererPtr->getHostInstancesBufferPtr()->getHostDataPtr() + rendererSelfIndex);
		shaderObject.position = glm::vec4(newTransformation.position, 1.0f);
		shaderObject.scale = glm::vec4(newTransformation.scale, 1.0f);
		shaderObject.qRotation = newTransformation.rotation;
    }

    ModelTransformation ModelInstance::getTransformation() const
    {
		const ModelInstance::ShaderModelInstance& shaderObject = *((ModelInstance::ShaderModelInstance*)rendererPtr->getHostInstancesBufferPtr()->getHostDataPtr() + rendererSelfIndex);

		ModelTransformation transformation;
		transformation.position = shaderObject.position;
		transformation.rotation = shaderObject.qRotation;
		transformation.scale = shaderObject.scale;

		return transformation;
    }

    bool ModelInstance::getVisibility(RenderPass *renderPass) const
    {
        const bool& visibility = false;

		return visibility;
    }

	void ModelInstance::setVisibility(RenderPass *renderPass, bool newVisibility)
    {

    }
}