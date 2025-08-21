#include "Model.h"
#include "RenderPass.h"
#include "Material.h"
#include "AccelerationStructure.h"
#include "RayTrace.h"
#include "IndirectDraw.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
	//----------MODEL DEFINITIONS----------//

    Model::Model(RenderEngine& renderer, const ModelCreateInfo& creationInfo)
        :modelName(creationInfo.modelName),
		aabb(creationInfo.bounds),
		renderer(renderer)
    {
		//Timer
        Timer timer(renderer, "Create Model", IRREGULAR);

		//temporary variables for creating the singular vertex and index buffer
		std::vector<char> creationVertices;
		std::vector<char> creationIndices;

		//fill in variables with the input LOD data
		for(const ModelLODInfo& lod : creationInfo.LODs)
		{
			LOD returnLOD = {};
			returnLOD.materialMeshes.reserve(lod.lodData.size());

			//iterate materials in LOD
			for(const auto& [matIndex, meshGroup] : lod.lodData)
			{
				//get IBO stride
				uint32_t iboStride = 0;
				switch(meshGroup.indexType)
				{
				case VK_INDEX_TYPE_UINT16:
					iboStride = sizeof(uint16_t);
					break;
				case VK_INDEX_TYPE_UINT32:
					iboStride = sizeof(uint32_t);
					break;
				case VK_INDEX_TYPE_UINT8:
					iboStride = sizeof(uint8_t);
					break;
				default:
					renderer.getLogger().recordLog({
						.type = CRITICAL_ERROR,
						.text = "Invalid VkIndexType used for model " + modelName
					});
				}

				//process mesh data
				const LODMesh materialMesh = {
					.vertexStride = meshGroup.vertexStride,
					.indexStride = iboStride,
					.vboOffset = (uint32_t)creationVertices.size(),
					.verticesSize =  (uint32_t)meshGroup.verticesData.size(),
					.iboOffset = (uint32_t)creationIndices.size(),
					.indicesSize =  (uint32_t)meshGroup.indicesData.size(),
					.invokeAnyHit = !meshGroup.opaque,
					.indexType = meshGroup.indexType
				};

				creationVertices.insert(creationVertices.end(), meshGroup.verticesData.begin(), meshGroup.verticesData.end());
				creationIndices.insert(creationIndices.end(), meshGroup.indicesData.begin(), meshGroup.indicesData.end());

				//push data
				returnLOD.materialMeshes.push_back(materialMesh);
			}
			LODs.push_back(returnLOD);
		}
		
		vbo = createDeviceLocalBuffer(creationVertices.size(), creationVertices.data(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
		ibo = createDeviceLocalBuffer(creationIndices.size(), creationIndices.data(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

		//set shader data and add to renderer
		setShaderData();
		renderer.addModelData(this);

		//create BLAS
		if(creationInfo.createBLAS && renderer.getDevice().getGPUFeaturesAndProperties().rtSupport)
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

		const ShaderModel shaderModel = {
			.bounds = aabb,
			.vertexAddress = vbo->getBufferDeviceAddress(),
			.indexAddress = ibo->getBufferDeviceAddress(),
			.lodCount = (uint32_t)LODs.size(),
			.lodsOffset = dynamicOffset
		};

		memcpy(newData.data(), &shaderModel, sizeof(ShaderModel));

		//model LODs data
		dynamicOffset += sizeof(ShaderModelLOD) * LODs.size();
		newData.resize(dynamicOffset);

		for(uint32_t lodIndex = 0; lodIndex < LODs.size(); lodIndex++)
		{
			const ShaderModelLOD modelLOD = {
				.materialCount = (uint32_t)LODs[lodIndex].materialMeshes.size(),
				.meshGroupsOffset = dynamicOffset
			};

			memcpy(newData.data() + shaderModel.lodsOffset + sizeof(ShaderModelLOD) * lodIndex, &modelLOD, sizeof(ShaderModelLOD));
			
			//LOD mesh groups data
			dynamicOffset += sizeof(ShaderModelLODMeshGroup) * LODs.at(lodIndex).materialMeshes.size();
			newData.resize(dynamicOffset);

			for(uint32_t matIndex = 0; matIndex < LODs.at(lodIndex).materialMeshes.size(); matIndex++)
			{
				//fill in material mesh group data
				const ShaderModelLODMeshGroup materialMeshGroup = {
					.vboOffset = LODs[lodIndex].materialMeshes[matIndex].vboOffset,
					.vboSize = LODs[lodIndex].materialMeshes[matIndex].verticesSize,
					.vboStride = LODs[lodIndex].materialMeshes[matIndex].vertexStride,
					.iboOffset = LODs[lodIndex].materialMeshes[matIndex].iboOffset,
					.iboSize = LODs[lodIndex].materialMeshes[matIndex].indicesSize,
					.iboStride = LODs[lodIndex].materialMeshes[matIndex].indexStride
				};

				memcpy(newData.data() + modelLOD.meshGroupsOffset + sizeof(ShaderModelLODMeshGroup) * matIndex, &materialMeshGroup, sizeof(ShaderModelLODMeshGroup));
			}
		}
        
		shaderData = newData;
    }

    std::unique_ptr<Buffer> Model::createDeviceLocalBuffer(VkDeviceSize size, void *data, VkBufferUsageFlags2KHR usageFlags) const
    {
		//create staging buffer (i should profile the consequences of this)
		const BufferInfo stagingBufferInfo = {
			.size = size,
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
		};
		const Buffer vboStaging(renderer, stagingBufferInfo);

		//fill staging data
		const BufferWrite write = {
			.offset = 0,
			.size = size,
			.readData = data
		};
		vboStaging.writeToBuffer({ write });

		//create device local buffer
		BufferInfo bufferInfo = {
			.size = size,
			.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | usageFlags,
			.allocationFlags = 0
		};
		if(renderer.getDevice().getGPUFeaturesAndProperties().rtSupport) bufferInfo.usageFlags = bufferInfo.usageFlags | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
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

	//----------MODEL INSTANCE DEFINITIONS----------//

    ModelInstance::ModelInstance(RenderEngine &renderer, const Model &parentModel, bool uniqueGeometry, const VkBuildAccelerationStructureFlagsKHR flags)
        :uniqueGeometryData({.isUsed = uniqueGeometry}),
        renderer(renderer),
        parentModel(parentModel)
    {
		renderer.addObject(this);
		
		//create unique VBO and BLAS if requested
		if((uniqueGeometry || !parentModel.defaultBLAS) && renderer.getDevice().getGPUFeaturesAndProperties().rtSupport)
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

			const LODMaterialData lodMaterialData = {
				.meshGroupsOffset = dynamicOffset
			};

			memcpy(newData.data() + sizeof(LODMaterialData) * lodIndex, &lodMaterialData, sizeof(LODMaterialData));
			
			//LOD mesh groups data
			dynamicOffset += sizeof(MaterialMeshGroup) * parentModel.getLODs().at(lodIndex).materialMeshes.size();
			newData.resize(dynamicOffset);

			for(uint32_t matIndex = 0; matIndex < parentModel.getLODs().at(lodIndex).materialMeshes.size(); matIndex++)
			{
				dynamicOffset = Device::getAlignment(dynamicOffset, 8);
				newData.resize(dynamicOffset);

				//pointers
				LODMesh const* lodMeshPtr = &parentModel.getLODs().at(lodIndex).materialMeshes.at(matIndex);
				CommonMeshGroup const* meshGroupPtr = renderPassSelfReferences.at(renderPass).meshGroupReferences.at(&parentModel.getLODs().at(lodIndex).materialMeshes.at(matIndex));
				ModelInstance const* instancePtr = uniqueGeometryData.isUsed ? this : NULL;

				//material mesh group data
				const MaterialMeshGroup materialMeshGroup = {
					.drawCommandAddress = 
						meshGroupPtr->getDrawCommandsBuffer().getBufferDeviceAddress() + 
						(meshGroupPtr->getInstanceMeshesData().at(instancePtr).at(lodMeshPtr).drawCommandIndex * sizeof(DrawCommand)),
					.matricesBufferAddress = 
						meshGroupPtr->getModelMatricesBuffer().getBufferDeviceAddress() + 
						(meshGroupPtr->getInstanceMeshesData().at(instancePtr).at(lodMeshPtr).matricesStartIndex * sizeof(ShaderOutputObject))
				};

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
		if(renderer.getDevice().getGPUFeaturesAndProperties().rtSupport && uniqueGeometryData.isUsed)
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
}