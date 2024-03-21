#include "Model.h"
#include "Renderer.h"

namespace Renderer
{
	//----------MODEL DEFINITIONS----------//

    Model::Model(RenderEngine* renderer, const ModelCreateInfo& creationInfo)
		:rendererPtr(renderer)
	{
		//temporary variables for creating the singular vertex and index buffer
		std::vector<Vertex> creationVertices;
		std::vector<uint32_t> creationIndices;

		//fill in variables with the input LOD data
		for(const LODInfo& lod : creationInfo.LODs)
		{
			LOD returnLOD;
			for(const auto& [matIndex, meshes] : lod.meshes)
			{
				std::vector<LODMesh> returnMeshes;
				for(const MeshInfo& mesh : meshes)
				{
					LODMesh returnMesh;
					returnMesh.vboOffset = creationVertices.size();
					returnMesh.vertexCount =  mesh.vertices->size();
					returnMesh.iboOffset = creationIndices.size();
					returnMesh.indexCount =  mesh.indices->size();

					creationVertices.insert(creationVertices.end(), mesh.vertices->begin(), mesh.vertices->end());
					creationIndices.insert(creationIndices.end(), mesh.indices->begin(), mesh.indices->end());

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

		vbo = std::make_shared<VertexBuffer>(rendererPtr->getDevice(), rendererPtr->getCommandsHandler(), &creationVertices);
		ibo = std::make_shared<IndexBuffer>(rendererPtr->getDevice(), rendererPtr->getCommandsHandler(), &creationIndices);
	}

	Model::~Model()
	{

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

    ModelInstance::ModelInstance(RenderEngine* renderer, Model* parentModel, std::vector<std::unordered_map<uint32_t, Material const*>> materials)
		:rendererPtr(renderer),
		modelPtr(parentModel),
		materials(materials)
    {
		if(parentModel && renderer)
		{
			meshReferences.resize(modelPtr->getLODs().size());
			rendererPtr->addObject(*this, meshReferences, objectReference);
		}
    }

    ModelInstance::~ModelInstance()
    {
		if(modelPtr && rendererPtr)
		{
			rendererPtr->removeObject(*this, meshReferences, objectReference);
		}
    }

    void ModelInstance::transform(const ModelTransform &newTransform)
    {
		transformation = newTransform;
    }
}