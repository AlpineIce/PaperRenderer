#include "Model.h"
#include "RenderPass.h"
#include "Material.h"
#include "AccelerationStructure.h"
#include "RayTrace.h"
#include "IndirectDraw.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
	//----------MODEL GEOMETRY DATA DEFINITIONS----------//

	struct ShaderModel
	{
		AABB bounds = {};
		uint64_t vertexAddress = 0;
		uint64_t indexAddress = 0;
		uint32_t lodCount = 0;
		uint32_t lodsOffset = 0;
	};

	struct ShaderModelLOD
	{
		uint32_t materialCount = 0;
		uint32_t meshGroupsOffset = 0;
	};

	struct ShaderModelLODMeshGroup
	{
		uint32_t vboOffset = 0;
		uint32_t vboSize = 0;
		uint32_t vboStride = 0;
		uint32_t iboOffset = 0;
		uint32_t iboSize = 0;
		uint32_t iboStride = 0;
	};

	ModelGeometryData::ModelGeometryData(RenderEngine& renderer, const AABB& aabb, const std::vector<uint8_t>& vertices, Model& parentModel, const bool createBLAS, const VkBuildAccelerationStructureFlagsKHR blasFlags)
		:aabb(aabb),
		vbo([&] {
			// Create buffer
			const BufferInfo bufferInfo = {
				.size = vertices.size(),
				.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
					(renderer.getDevice().getGPUFeaturesAndProperties().rtSupport ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : (VkBufferUsageFlagBits2KHR)0),
				.allocationFlags = renderer.getDevice().getGPUFeaturesAndProperties().reBAR ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : (VmaAllocationCreateFlags)0
			};
			Buffer buffer(renderer, bufferInfo);

			buffer.writeToBuffer({{
				.offset = 0,
				.size = vertices.size(),
				.readData = vertices.data()
			}});

			return buffer;
		} ()),
		blasFlags(blasFlags),
		blas([&] {
			if(createBLAS && renderer.getDevice().getGPUFeaturesAndProperties().rtSupport)
			{
				std::unique_ptr<BLAS> blas = std::make_unique<BLAS>(renderer, parentModel, vbo);
				const BLASBuildOp op = {
					.accelerationStructure = blas.get(),
					.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
					.flags = blasFlags
				};
				renderer.getAsBuilder().queueBLAS(op);

				return blas;
			}

			return std::unique_ptr<BLAS>();
		} ()),
		shaderData(createShaderData(parentModel.getIBO().getBufferDeviceAddress(), vbo.getBufferDeviceAddress(), aabb, parentModel.getLODs())),
		parentModel(parentModel),
		renderer(renderer)
	{
		renderer.addModelData(this);
	}

	ModelGeometryData::ModelGeometryData(RenderEngine& renderer, const ModelGeometryData& geometryData, const bool createBLAS)
		:aabb(geometryData.aabb),
		vbo([&] {
			const BufferInfo bufferInfo = {
				.size = geometryData.getVBO().getSize(),
				.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
					(renderer.getDevice().getGPUFeaturesAndProperties().rtSupport ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : (VkBufferUsageFlagBits2KHR)0),
				.allocationFlags = renderer.getDevice().getGPUFeaturesAndProperties().reBAR ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : (VmaAllocationCreateFlags)0
			};
			Buffer buffer(renderer, bufferInfo);

			const VkBufferCopy copy = {
				.srcOffset = 0,
				.dstOffset = 0,
				.size = geometryData.getVBO().getSize()
			};
			buffer.copyFromBufferRanges(geometryData.vbo, { copy }, {}).idle();

			return buffer;
		} ()),
		blasFlags(geometryData.blasFlags),
		blas([&] {
			if(createBLAS && renderer.getDevice().getGPUFeaturesAndProperties().rtSupport)
			{
				std::unique_ptr<BLAS> blas = std::make_unique<BLAS>(renderer, geometryData.parentModel, vbo);
				const BLASBuildOp op = {
					.accelerationStructure = blas.get(),
					.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
					.flags = blasFlags
				};
				renderer.getAsBuilder().queueBLAS(op);

				return blas;
			}

			return std::unique_ptr<BLAS>();
		} ()),
		shaderData(createShaderData(geometryData.parentModel.getIBO().getBufferDeviceAddress(), vbo.getBufferDeviceAddress(), aabb, geometryData.parentModel.getLODs())),
		parentModel(geometryData.parentModel),
		renderer(renderer)
	{
		renderer.addModelData(this);
	}

    ModelGeometryData::~ModelGeometryData()
    {
		renderer.removeModelData(this);
    }

    std::vector<uint8_t> ModelGeometryData::createShaderData(const VkDeviceAddress iboAddress, const VkDeviceAddress vboAddress, const AABB& bounds, const std::vector<LOD>& LODs) const
    {
		//model data
		uint32_t dynamicOffset = sizeof(ShaderModel);
		std::vector<uint8_t> newData(dynamicOffset);

		const ShaderModel shaderModel = {
			.bounds = bounds,
			.vertexAddress = vboAddress,
			.indexAddress = iboAddress,
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
        
		return newData;
    }

	void ModelGeometryData::updateShaderData(const VkDeviceAddress iboAddress, const VkDeviceAddress vboAddress, const AABB& bounds, const std::vector<LOD>& LODs)
	{
		shaderData = createShaderData(iboAddress, vboAddress, bounds, LODs);
	}

    //----------MODEL DEFINITIONS----------//

    Model::Model(RenderEngine& renderer, const ModelCreateInfo& creationInfo)
        :modelName(creationInfo.modelName),
		LODs([&] {
			// Return data
			std::vector<LOD> returnData = {};

			//fill in variables with the input LOD data
			uint32_t vertexIndex = 0;
			uint32_t indexIndex = 0;
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
						.vboOffset = vertexIndex,
						.verticesSize =  (uint32_t)meshGroup.verticesData.size(),
						.iboOffset = indexIndex,
						.indicesSize =  (uint32_t)meshGroup.indicesData.size(),
						.invokeAnyHit = !meshGroup.opaque,
						.indexType = meshGroup.indexType
					};

					vertexIndex += meshGroup.verticesData.size();
					indexIndex += meshGroup.indicesData.size();

					//push data
					returnLOD.materialMeshes.push_back(materialMesh);
				}
				returnData.push_back(returnLOD);
			}

			return returnData;
		} ()),
		ibo([&] {
			// Get index data
			std::vector<uint8_t> creationIndicesData = {};
			for(const ModelLODInfo& lod : creationInfo.LODs)
			{
				for(const auto& [matIndex, meshGroup] : lod.lodData)
				{
					creationIndicesData.insert(creationIndicesData.end(), meshGroup.indicesData.begin(), meshGroup.indicesData.end());
				}
			}

			// Create buffer
			const BufferInfo bufferInfo = {
				.size = creationIndicesData.size(),
				.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
					(renderer.getDevice().getGPUFeaturesAndProperties().rtSupport ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : (VkBufferUsageFlagBits2KHR)0),
				.allocationFlags = renderer.getDevice().getGPUFeaturesAndProperties().reBAR ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : (VmaAllocationCreateFlags)0
			};
			Buffer buffer(renderer, bufferInfo);

			buffer.writeToBuffer({{
				.offset = 0,
				.size = creationIndicesData.size(),
				.readData = creationIndicesData.data()
			}});

			return buffer;
		} ()),
		geometry(renderer, creationInfo.bounds, [&] {
			// Get vertex data
			std::vector<uint8_t> creationVertices = {};
			for(const ModelLODInfo& lod : creationInfo.LODs)
			{
				for(const auto& [matIndex, meshGroup] : lod.lodData)
				{
					creationVertices.insert(creationVertices.end(), meshGroup.verticesData.begin(), meshGroup.verticesData.end());
				}
			}

			// Return buffer of vertex data
			return creationVertices;
		} (), *this, creationInfo.createBLAS, creationInfo.blasFlags),
		renderer(renderer)
    {
	}

	Model::~Model()
	{
	}

	//----------MODEL INSTANCE DEFINITIONS----------//

    ModelInstance::ModelInstance(RenderEngine &renderer, const Model &parentModel, bool uniqueGeometry, const VkBuildAccelerationStructureFlagsKHR flags)
        :uniqueGeometryData(uniqueGeometry ? std::make_unique<ModelGeometryData>(renderer, parentModel.getGeometryData(), true) : NULL),
        renderer(renderer),
        parentModel(parentModel)
    {
		renderer.addObject(this);
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
		uniqueGeometryData.reset();
    }

	void ModelInstance::setRenderPassInstanceData(RenderPass const* renderPass)
    {
		std::vector<uint8_t> newData;
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

				//material mesh group data
				const MaterialMeshGroup materialMeshGroup = {
					.drawCommandAddress = 
						meshGroupPtr->getDrawCommandsBuffer().getBufferDeviceAddress() + 
						(meshGroupPtr->getInstanceMeshesData().at(&getGeometryData()).at(lodMeshPtr).drawCommandIndex * sizeof(DrawCommand)),
					.matricesBufferAddress = 
						meshGroupPtr->getModelMatricesBuffer().getBufferDeviceAddress() + 
						(meshGroupPtr->getInstanceMeshesData().at(&getGeometryData()).at(lodMeshPtr).matricesStartIndex * sizeof(ShaderOutputObject))
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
			.selfModelDataOffset = uniqueGeometryData ? (uint32_t)uniqueGeometryData->getShaderDataReference().shaderDataLocation : 0xFFFFFFFF,
			.parentModelDataOffset = (uint32_t)parentModel.getGeometryData().getShaderDataReference().shaderDataLocation
		};
		return shaderModelInstance;
    }

	void ModelInstance::queueBLAS(const VkBuildAccelerationStructureFlagsKHR flags) const
    {
		if(uniqueGeometryData && renderer.getDevice().getGPUFeaturesAndProperties().rtSupport)
		{
			//queue operation
			const BLASBuildOp op = {
				.accelerationStructure = uniqueGeometryData->getBlasPtr(),
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
}