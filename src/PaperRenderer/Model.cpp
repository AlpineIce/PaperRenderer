#include "Model.h"
#include "RenderPass.h"
#include "PaperRenderer.h"
#include "Material.h"
#include "AccelerationStructure.h"
#include "IndirectDraw.h"

namespace PaperRenderer
{
	//----------MODEL DEFINITIONS----------//

    Model::Model(RenderEngine& renderer, const ModelCreateInfo& creationInfo)
        :renderer(renderer)
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

			//iterate materials in LOD
			for(const auto& [matIndex, meshGroup] : lod.lodData)
			{
				//process mesh data
				MaterialMesh materialMesh;
				materialMesh.invokeAnyHit = !meshGroup.opaque;
				materialMesh.mesh.vboOffset = dynamicVertexOffset;
				materialMesh.mesh.vertexCount =  meshGroup.meshInfo.verticesData.size() / vertexDescription.stride;
				materialMesh.mesh.iboOffset = creationIndices.size();
				materialMesh.mesh.indexCount =  meshGroup.meshInfo.indices.size();

				creationVerticesData.insert(creationVerticesData.end(), meshGroup.meshInfo.verticesData.begin(), meshGroup.meshInfo.verticesData.end());
				creationIndices.insert(creationIndices.end(), meshGroup.meshInfo.indices.begin(), meshGroup.meshInfo.indices.end());

				dynamicVertexOffset += materialMesh.mesh.vertexCount;

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
				returnLOD.materialMeshes.push_back(materialMesh);
			}
			LODs.push_back(returnLOD);
		}
		
		vbo = createDeviceLocalBuffer(creationVerticesData.size(), creationVerticesData.data(), 
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
		ibo = createDeviceLocalBuffer(sizeof(uint32_t) * creationIndices.size(), creationIndices.data(), 
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);

		//set shader data and add to renderer
		setShaderData();
		renderer.addModelData(this);

		//create BLAS
		if(creationInfo.createBLAS && renderer.getDevice().getRTSupport())
		{
			defaultBLAS = std::make_unique<BLAS>(renderer, this, vbo.get());
			AccelerationStructureOp op = {
				.accelerationStructure = *defaultBLAS.get(),
                .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
				.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
				.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR
			};
			renderer.asBuilder.queueAs(op);
		}
	}

	Model::~Model()
	{
		renderer.removeModelData(this);
		
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
		shaderModel.vertexAddress = vbo->getBufferDeviceAddress();
		shaderModel.indexAddress = ibo->getBufferDeviceAddress();
		shaderModel.lodCount = LODs.size();
		shaderModel.lodsOffset = dynamicOffset;
		shaderModel.vertexStride = vertexDescription.stride;

		memcpy(newData.data(), &shaderModel, sizeof(ShaderModel));

		//model LODs data
		dynamicOffset += sizeof(ShaderModelLOD) * LODs.size();
		newData.resize(dynamicOffset);

		for(uint32_t lodIndex = 0; lodIndex < LODs.size(); lodIndex++)
		{
			ShaderModelLOD modelLOD = {};
			modelLOD.materialCount = LODs.at(lodIndex).materialMeshes.size();
			modelLOD.meshGroupsOffset = dynamicOffset;

			memcpy(newData.data() + shaderModel.lodsOffset + sizeof(ShaderModelLOD) * lodIndex, &modelLOD, sizeof(ShaderModelLOD));
			
			//LOD mesh groups data
			dynamicOffset += sizeof(ShaderModelLODMeshGroup) * LODs.at(lodIndex).materialMeshes.size();
			newData.resize(dynamicOffset);

			for(uint32_t matIndex = 0; matIndex < LODs.at(lodIndex).materialMeshes.size(); matIndex++)
			{
				ShaderModelLODMeshGroup materialMeshGroup = {};
				materialMeshGroup.iboOffset = LODs.at(lodIndex).materialMeshes.at(matIndex).mesh.iboOffset;
				materialMeshGroup.vboOffset = LODs.at(lodIndex).materialMeshes.at(matIndex).mesh.vboOffset;

				memcpy(newData.data() + modelLOD.meshGroupsOffset + sizeof(ShaderModelLODMeshGroup) * matIndex, &materialMeshGroup, sizeof(ShaderModelLODMeshGroup));
			}
		}
        
		shaderData = newData;
    }

    std::unique_ptr<Buffer> Model::createDeviceLocalBuffer(VkDeviceSize size, void *data, VkBufferUsageFlags2KHR usageFlags) const
    {
		//create staging buffer
		BufferInfo stagingBufferInfo = {};
		stagingBufferInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		stagingBufferInfo.size = size;
		stagingBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		Buffer vboStaging(renderer, stagingBufferInfo);

		//fill staging data
		BufferWrite write = {};
		write.data = data;
		write.size = size;
		write.offset = 0;
		vboStaging.writeToBuffer({ write });

		//create device local buffer
		BufferInfo bufferInfo = {};
		bufferInfo.allocationFlags = 0;
		bufferInfo.size = size;
		bufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | usageFlags;
		std::unique_ptr<Buffer> buffer = std::make_unique<Buffer>(renderer, bufferInfo);

		//copy
		VkBufferCopy copyRegion;
		copyRegion.dstOffset = 0;
		copyRegion.srcOffset = 0;
		copyRegion.size = size;

		SynchronizationInfo synchronizationInfo = {};
		synchronizationInfo.queueType = QueueType::TRANSFER;
		synchronizationInfo.fence = renderer.getDevice().getCommands().getUnsignaledFence();

		buffer->copyFromBufferRanges(vboStaging, { copyRegion }, synchronizationInfo);

		//wait for fence and destroy (potential for efficiency improvements here since this is technically brute force synchronization)
		vkWaitForFences(renderer.getDevice().getDevice(), 1, &synchronizationInfo.fence, VK_TRUE, UINT64_MAX);
		vkDestroyFence(renderer.getDevice().getDevice(), synchronizationInfo.fence, nullptr);

		return buffer;
    }

    void Model::bindBuffers(const VkCommandBuffer& cmdBuffer) const
	{
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vbo->getBuffer(), offsets);
		vkCmdBindIndexBuffer(cmdBuffer, ibo->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
	}

	//----------MODEL INSTANCE DEFINITIONS----------//

    ModelInstance::ModelInstance(RenderEngine& renderer, Model const* parentModel, bool uniqueGeometry)
        :renderer(renderer),
        modelPtr(parentModel)
    {
		renderer.addObject(this);
		uniqueGeometryData.isUsed = uniqueGeometry;
		
		//create unique VBO and BLAS if requested
		if((uniqueGeometry || !parentModel->defaultBLAS) && renderer.getDevice().getRTSupport())
		{
			BufferInfo bufferInfo = {};
			bufferInfo.allocationFlags = 0;
			bufferInfo.size = modelPtr->vbo->getSize();
			bufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
			uniqueGeometryData.uniqueVBO  = std::make_unique<Buffer>(renderer, bufferInfo);

			//copy
			VkBufferCopy copyRegion;
			copyRegion.dstOffset = 0;
			copyRegion.srcOffset = 0;
			copyRegion.size = modelPtr->vbo->getSize();

			SynchronizationInfo synchronizationInfo = {};
			synchronizationInfo.queueType = QueueType::TRANSFER;
			synchronizationInfo.fence = renderer.getDevice().getCommands().getUnsignaledFence();

			uniqueGeometryData.uniqueVBO->copyFromBufferRanges(*modelPtr->vbo, { copyRegion }, synchronizationInfo);

			//wait for fence and destroy (potential for efficiency improvements here since this is technically brute force synchronization)
			vkWaitForFences(renderer.getDevice().getDevice(), 1, &synchronizationInfo.fence, VK_TRUE, UINT64_MAX);
			vkDestroyFence(renderer.getDevice().getDevice(), synchronizationInfo.fence, nullptr);

			//create BLAS
			if(renderer.getDevice().getRTSupport())
			{
				uniqueGeometryData.blas = std::make_unique<BLAS>(renderer, modelPtr, uniqueGeometryData.uniqueVBO.get());
				AccelerationStructureOp op = {
					.accelerationStructure = *uniqueGeometryData.blas.get(),
					.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
					.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
					//if geometry is unique then allow update, otherwise geometry isn't unique, but a parent copy doesnt exist; assume static
					.flags = uniqueGeometry ? (VkBuildAccelerationStructureFlagsKHR)VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR : (VkBuildAccelerationStructureFlagsKHR)VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR
				};
				renderer.asBuilder.queueAs(op);
			}
		}
    }

    ModelInstance::~ModelInstance()
    {
		renderer.removeObject(this);
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
			dynamicOffset = Device::getAlignment(dynamicOffset, 8);

			LODMaterialData lodMaterialData = {};
			lodMaterialData.meshGroupsOffset = dynamicOffset;

			memcpy(newData.data() + sizeof(LODMaterialData) * lodIndex, &lodMaterialData, sizeof(LODMaterialData));
			
			//LOD mesh groups data
			dynamicOffset += sizeof(MaterialMeshGroup) * modelPtr->getLODs().at(lodIndex).materialMeshes.size();
			newData.resize(dynamicOffset);

			for(uint32_t matIndex = 0; matIndex < modelPtr->getLODs().at(lodIndex).materialMeshes.size(); matIndex++)
			{
				dynamicOffset = Device::getAlignment(dynamicOffset, 8);
				newData.resize(dynamicOffset);

				//pointers
				LODMesh const* lodMeshPtr = &modelPtr->getLODs().at(lodIndex).materialMeshes.at(matIndex).mesh;
				CommonMeshGroup const* meshGroupPtr = renderPassSelfReferences.at(renderPass).meshGroupReferences.at(&modelPtr->getLODs().at(lodIndex).materialMeshes.at(matIndex).mesh);

				//material mesh group data
				MaterialMeshGroup materialMeshGroup = {};
				materialMeshGroup.drawCommandAddress = 
					meshGroupPtr->getDrawCommandsBuffer().getBufferDeviceAddress() + 
					(meshGroupPtr->getMeshesData().at(lodMeshPtr).drawCommandIndex * sizeof(DrawCommand));
				materialMeshGroup.matricesBufferAddress = 
					meshGroupPtr->getModelMatricesBuffer().getBufferDeviceAddress() + 
					(meshGroupPtr->getMeshesData().at(lodMeshPtr).matricesStartIndex * sizeof(glm::mat4));

				memcpy(newData.data() + lodMaterialData.meshGroupsOffset + sizeof(MaterialMeshGroup) * matIndex, &materialMeshGroup, sizeof(MaterialMeshGroup));
			}
		}

		renderPassSelfReferences.at(renderPass).renderPassInstanceData = newData;
    }

    ModelInstance::ShaderModelInstance ModelInstance::getShaderInstance() const
    {
		ShaderModelInstance shaderModelInstance = {};
		shaderModelInstance.position = transform.position;
		shaderModelInstance.qRotation = transform.rotation;
		shaderModelInstance.scale = transform.scale;
		shaderModelInstance.modelDataOffset = modelPtr->shaderDataLocation;

		return shaderModelInstance;
    }

    void ModelInstance::setTransformation(const ModelTransformation &newTransformation)
    {
		this->transform = newTransformation;
		renderer.toUpdateModelInstances.push_front(this);
    }

    BLAS const* ModelInstance::getBLAS() const
    {
		if(uniqueGeometryData.blas)
		{
			return uniqueGeometryData.blas.get();
		}
		else if(modelPtr->getBlasPtr())
		{
			return modelPtr->getBlasPtr();
		}
		else
		{
			return NULL;
		}
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