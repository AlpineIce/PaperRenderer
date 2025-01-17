#include "Model.h"
#include "RenderPass.h"
#include "Material.h"
#include "AccelerationStructure.h"
#include "IndirectDraw.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
	//----------MODEL DEFINITIONS----------//

    Model::Model(RenderEngine& renderer, const ModelCreateInfo& creationInfo)
        :modelName(creationInfo.modelName),
		renderer(renderer)
    {
		//Timer
        Timer timer(renderer, "Create Model", IRREGULAR);

		//temporary variables for creating the singular vertex and index buffer
		std::vector<char> creationVerticesData;
		std::vector<uint32_t> creationIndices;

		//AABB processing
		const AABB defaultAABB = {};
		const bool constructAABB = creationInfo.bounds == defaultAABB;
		if(constructAABB)
		{
			aabb.posX = -1000000.0f;
			aabb.negX = 1000000.0f;
			aabb.posY = -1000000.0f;
			aabb.negY = 1000000.0f;
			aabb.posZ = -1000000.0f;
			aabb.negZ = 1000000.0f;
		}

		//vertex data
		vertexAttributes = creationInfo.vertexAttributes;
		vertexDescription = creationInfo.vertexDescription;

		//fill in variables with the input LOD data
		VkDeviceSize dynamicVertexOffset = 0;
		for(const ModelLODInfo& lod : creationInfo.LODs)
		{
			LOD returnLOD = {};
			returnLOD.materialMeshes.reserve(lod.lodData.size());

			//iterate materials in LOD
			for(const auto& [matIndex, meshGroup] : lod.lodData)
			{
				//process mesh data
				const MaterialMesh materialMesh = {
					.mesh = {
						.vboOffset = (uint32_t)dynamicVertexOffset,
						.vertexCount =  (uint32_t)meshGroup.verticesData.size() / vertexDescription.stride,
						.iboOffset = (uint32_t)creationIndices.size(),
						.indexCount =  (uint32_t)meshGroup.indices.size()
					},
					.invokeAnyHit = !meshGroup.opaque
				};

				creationVerticesData.insert(creationVerticesData.end(), meshGroup.verticesData.begin(), meshGroup.verticesData.end());
				creationIndices.insert(creationIndices.end(), meshGroup.indices.begin(), meshGroup.indices.end());

				dynamicVertexOffset += materialMesh.mesh.vertexCount;

				//AABB processing
				if(constructAABB)
				{
					uint32_t vertexCount = creationVerticesData.size() / vertexDescription.stride;
					for(uint32_t i = 0; i < vertexCount; i++)
					{
						const glm::vec3& vertexPosition = *(glm::vec3*)(creationVerticesData.data() + (i * vertexDescription.stride));

						aabb.posX = std::max(vertexPosition.x, aabb.posX);
						aabb.negX = std::min(vertexPosition.x, aabb.negX);
						aabb.posY = std::max(vertexPosition.y, aabb.posY);
						aabb.negY = std::min(vertexPosition.y, aabb.negY);
						aabb.posZ = std::max(vertexPosition.z, aabb.posZ);
						aabb.negZ = std::min(vertexPosition.z, aabb.negZ);
					}
				}

				//push data
				returnLOD.materialMeshes.push_back(materialMesh);
			}
			LODs.push_back(returnLOD);
		}
		
		vbo = createDeviceLocalBuffer(creationVerticesData.size(), creationVerticesData.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		ibo = createDeviceLocalBuffer(sizeof(uint32_t) * creationIndices.size(), creationIndices.data(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

		//set shader data and add to renderer
		setShaderData();
		renderer.addModelData(this);

		//create BLAS
		if(creationInfo.createBLAS && renderer.getDevice().getRTSupport())
		{
			defaultBLAS = std::make_unique<BLAS>(renderer, *this, vbo.get());
			const BLASBuildOp op = {
				.accelerationStructure = *defaultBLAS.get(),
				.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
				.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
			};
			renderer.getAsBuilder().queueBLAS(op);
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
		write.readData = data;
		write.size = size;
		write.offset = 0;
		vboStaging.writeToBuffer({ write });

		//create device local buffer
		BufferInfo bufferInfo = {};
		bufferInfo.allocationFlags = 0;
		bufferInfo.size = size;
		bufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | usageFlags;
		if(renderer.getDevice().getRTSupport()) bufferInfo.usageFlags = bufferInfo.usageFlags | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		std::unique_ptr<Buffer> buffer = std::make_unique<Buffer>(renderer, bufferInfo);

		//copy
		const VkBufferCopy copyRegion = {
			.srcOffset = 0,
			.dstOffset = 0,
			.size = size
		};

		const SynchronizationInfo synchronizationInfo = {
			.queueType = QueueType::TRANSFER
		};
		vkQueueWaitIdle(buffer->copyFromBufferRanges(vboStaging, { copyRegion }, synchronizationInfo).queue);

		return buffer;
    }

    void Model::bindBuffers(const VkCommandBuffer& cmdBuffer) const
	{
		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &vbo->getBuffer(), offsets);
		vkCmdBindIndexBuffer(cmdBuffer, ibo->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
	}

	//----------MODEL INSTANCE DEFINITIONS----------//

    ModelInstance::ModelInstance(RenderEngine &renderer, const Model &parentModel, bool uniqueGeometry, const VkBuildAccelerationStructureFlagsKHR flags)
        :uniqueGeometryData({.isUsed = uniqueGeometry}),
        renderer(renderer),
        parentModel(parentModel)
    {
		renderer.addObject(this);
		
		//create unique VBO and BLAS if requested
		if((uniqueGeometry || !parentModel.defaultBLAS) && renderer.getDevice().getRTSupport())
		{
			//new vertex buffer
			const BufferInfo bufferInfo = {
				.size = parentModel.vbo->getSize(),
				.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT |
					VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
				.allocationFlags = 0
			};
			uniqueGeometryData.uniqueVBO  = std::make_unique<Buffer>(renderer, bufferInfo);

			//copy vertex data
			const VkBufferCopy copyRegion = {
				.srcOffset = 0,
				.dstOffset = 0,
				.size = parentModel.vbo->getSize()
			};

			const SynchronizationInfo synchronizationInfo = {
				.queueType = QueueType::TRANSFER
			};
			vkQueueWaitIdle(uniqueGeometryData.uniqueVBO->copyFromBufferRanges(*parentModel.vbo, { copyRegion }, synchronizationInfo).queue);

			//create BLAS
			uniqueGeometryData.blas = std::make_unique<BLAS>(renderer, parentModel, uniqueGeometryData.uniqueVBO.get());
			queueBLAS(flags);
		}
    }

    ModelInstance::~ModelInstance()
    {
		renderer.removeObject(this);

		//remove from references
		while(renderPassSelfReferences.size())
		{
			((RenderPass*)renderPassSelfReferences.begin()->first)->removeInstance(*this);
		}

		while(rtRenderSelfReferences.size())
		{
			((RayTraceRender*)rtRenderSelfReferences.begin()->first)->removeInstance(*this);
		}

		//destroy unique geometry
		uniqueGeometryData = {};
    }

	void ModelInstance::setRenderPassInstanceData(RenderPass const* renderPass)
    {
		std::vector<char> newData;
		newData.reserve(renderPassSelfReferences.at(renderPass).renderPassInstanceData.size());
		uint32_t dynamicOffset = 0;

		dynamicOffset += sizeof(LODMaterialData) * parentModel.getLODs().size();
		newData.resize(dynamicOffset);

		for(uint32_t lodIndex = 0; lodIndex < parentModel.getLODs().size(); lodIndex++)
		{
			dynamicOffset = Device::getAlignment(dynamicOffset, 8);

			LODMaterialData lodMaterialData = {};
			lodMaterialData.meshGroupsOffset = dynamicOffset;

			memcpy(newData.data() + sizeof(LODMaterialData) * lodIndex, &lodMaterialData, sizeof(LODMaterialData));
			
			//LOD mesh groups data
			dynamicOffset += sizeof(MaterialMeshGroup) * parentModel.getLODs().at(lodIndex).materialMeshes.size();
			newData.resize(dynamicOffset);

			for(uint32_t matIndex = 0; matIndex < parentModel.getLODs().at(lodIndex).materialMeshes.size(); matIndex++)
			{
				dynamicOffset = Device::getAlignment(dynamicOffset, 8);
				newData.resize(dynamicOffset);

				//pointers
				LODMesh const* lodMeshPtr = &parentModel.getLODs().at(lodIndex).materialMeshes.at(matIndex).mesh;
				CommonMeshGroup const* meshGroupPtr = renderPassSelfReferences.at(renderPass).meshGroupReferences.at(&parentModel.getLODs().at(lodIndex).materialMeshes.at(matIndex).mesh);
				ModelInstance const* instancePtr = uniqueGeometryData.isUsed ? this : NULL;

				//material mesh group data
				MaterialMeshGroup materialMeshGroup = {};
				materialMeshGroup.drawCommandAddress = 
					meshGroupPtr->getDrawCommandsBuffer().getBufferDeviceAddress() + 
					(meshGroupPtr->getInstanceMeshesData().at(instancePtr).at(lodMeshPtr).drawCommandIndex * sizeof(DrawCommand));
				materialMeshGroup.matricesBufferAddress = 
					meshGroupPtr->getModelMatricesBuffer().getBufferDeviceAddress() + 
					(meshGroupPtr->getInstanceMeshesData().at(instancePtr).at(lodMeshPtr).matricesStartIndex * sizeof(ShaderOutputObject));

				memcpy(newData.data() + lodMaterialData.meshGroupsOffset + sizeof(MaterialMeshGroup) * matIndex, &materialMeshGroup, sizeof(MaterialMeshGroup));
			}
		}

		//pad to 8 byte alignment
		newData.resize(Device::getAlignment(newData.size(), 8));

		renderPassSelfReferences.at(renderPass).renderPassInstanceData = newData;
    }

    ModelInstance::ShaderModelInstance ModelInstance::getShaderInstance() const
    {
		const ShaderModelInstance shaderModelInstance = {
			.position = transform.position,
			.scale = transform.scale,
			.qRotation = transform.rotation,
			.modelDataOffset = (uint32_t)parentModel.shaderDataLocation
		};
		return shaderModelInstance;
    }

	void ModelInstance::queueBLAS(const VkBuildAccelerationStructureFlagsKHR flags) const
    {
		if(renderer.getDevice().getRTSupport() && uniqueGeometryData.isUsed)
		{
			//queue operation
			const BLASBuildOp op = {
				.accelerationStructure = *uniqueGeometryData.blas.get(),
				.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
				.flags = flags
			};
			renderer.getAsBuilder().queueBLAS(op);
		}
    }

    void ModelInstance::setTransformation(const ModelTransformation &newTransformation)
    {
		this->transform = newTransformation;
		renderer.toUpdateModelInstances.push_front(this);
    }

    void ModelInstance::invalidateGeometry(const VkBuildAccelerationStructureFlagsKHR flags) const
    {
		queueBLAS(flags);
    }

    void ModelInstance::bindBuffers(const VkCommandBuffer &cmdBuffer) const
    {
		if(uniqueGeometryData.isUsed)
		{
			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &uniqueGeometryData.uniqueVBO->getBuffer(), offsets);
			vkCmdBindIndexBuffer(cmdBuffer, parentModel.ibo->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
		}
		else
		{
			parentModel.bindBuffers(cmdBuffer);
		}
    }
}