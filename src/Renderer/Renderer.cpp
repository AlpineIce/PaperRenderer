#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "Renderer.h"

#include <iostream>
#include <filesystem>
#include <sstream>
#include <fstream>

namespace Renderer
{
    RenderEngine::RenderEngine(RendererCreationStruct creationInfo)
        :appName(creationInfo.appName),
        device(creationInfo.appName),
        window(WindowInformation(creationInfo.resX, creationInfo.resY, false), creationInfo.appName, &device),
        commands(&device),
        swapchain(&device, &window, false),
        descriptors(&device, &commands),
        pipelineBuilder(&device, &descriptors, &swapchain),
        rtAccelStructure(&device, &commands),
        rendering(&swapchain, &device, &commands, &descriptors)
    {
        loadPipelines();
        loadModels("resources/models");
        loadTextures("resources/textures");
        loadMaterials("resources/materials");

        if(rtEnabled) initRT();
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
    }

    void RenderEngine::loadPipelines()
    {
        //----------PBR PIPELINE----------//

        std::vector<ShaderPair> PBRshaderPairs;
        ShaderPair pbrVert = {
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .directory = "resources/shaders/PBR_vert.spv"
        };
        PBRshaderPairs.push_back(pbrVert);
        ShaderPair pbrFrag = {
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .directory = "resources/shaders/PBR_frag.spv"
        };
        PBRshaderPairs.push_back(pbrFrag);

        //descriptor set 1 (material)
        DescriptorSet set1Descriptors;
        
        VkDescriptorSetLayoutBinding uniformDescriptor = {};
        uniformDescriptor.binding = 0;
        uniformDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uniformDescriptor.descriptorCount = 1;
        uniformDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        set1Descriptors.descriptorBindings.push_back(uniformDescriptor);

        VkDescriptorSetLayoutBinding textureDescriptor = {};
        textureDescriptor.binding = 1;
        textureDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureDescriptor.descriptorCount = TEXTURE_ARRAY_SIZE;
        textureDescriptor.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        set1Descriptors.descriptorBindings.push_back(textureDescriptor);

        //descriptor set 2 (object)
        DescriptorSet set2Descriptors;

        VkDescriptorSetLayoutBinding objDescriptor = {};
        objDescriptor.binding = 0;
        objDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        objDescriptor.descriptorCount = 1;
        objDescriptor.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        set2Descriptors.descriptorBindings.push_back(objDescriptor);

        std::vector<DescriptorSet> PBRdescriptorSets = {set1Descriptors, set2Descriptors};

        PipelineBuildInfo pbrInfo;
        pbrInfo.shaderInfo = PBRshaderPairs;
        pbrInfo.useGlobalDescriptor = true;
        pbrInfo.descriptors = PBRdescriptorSets;
        pbrInfo.pipelineType = PBR;

        renderTree[PBR].pipeline = pipelineBuilder.buildRasterPipeline(pbrInfo);

        //----------TEXTURELESS PBR PIPELINE----------//

        std::vector<ShaderPair> texturelessPBRshaderPairs;
        ShaderPair texlessPBRvert = {
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .directory = "resources/shaders/TexturelessPBR_vert.spv"
        };
        texturelessPBRshaderPairs.push_back(texlessPBRvert);
        ShaderPair texlessPBRfrag = {
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .directory = "resources/shaders/TexturelessPBR_frag.spv"
        };
        texturelessPBRshaderPairs.push_back(texlessPBRfrag);

        //descriptor set 1 (material) same as PBR except for textures
        DescriptorSet TexturelessPBRset1Descriptors;
        TexturelessPBRset1Descriptors.descriptorBindings.push_back(uniformDescriptor);

        //descriptor set 2 (object) same as PBR
        DescriptorSet TexturelessPBRset2Descriptors;
        TexturelessPBRset2Descriptors.descriptorBindings.push_back(objDescriptor);

        std::vector<DescriptorSet> texturelessPBRdescriptorSets = {TexturelessPBRset1Descriptors, TexturelessPBRset2Descriptors};

        PipelineBuildInfo TexturelessPBRInfo;
        TexturelessPBRInfo.shaderInfo = texturelessPBRshaderPairs;
        TexturelessPBRInfo.useGlobalDescriptor = true;
        TexturelessPBRInfo.descriptors = texturelessPBRdescriptorSets;
        TexturelessPBRInfo.pipelineType = TexturelessPBR;

        renderTree[TexturelessPBR].pipeline = pipelineBuilder.buildRasterPipeline(TexturelessPBRInfo);

        //----------RAY TRACING PIPELINE----------//

        std::vector<ShaderPair> RTshaderPairs;
        ShaderPair anyHitShader = {
            .stage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
            .directory = "resources/shaders/RT/RTanyHit.spv"
        };
        RTshaderPairs.push_back(anyHitShader);
        ShaderPair missShader = {
            .stage = VK_SHADER_STAGE_MISS_BIT_KHR,
            .directory = "resources/shaders/RT/RTmiss.spv"
        };
        RTshaderPairs.push_back(missShader);
        ShaderPair closestShader = {
            .stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            .directory = "resources/shaders/RT/RTclosestHit.spv"
        };
        RTshaderPairs.push_back(closestShader);
        ShaderPair raygenShader = {
            .stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            .directory = "resources/shaders/RT/RTraygen.spv"
        };
        RTshaderPairs.push_back(raygenShader);
        ShaderPair intersectionShader = {
            .stage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
            .directory = "resources/shaders/RT/RTintersection.spv"
        };
        RTshaderPairs.push_back(intersectionShader);

        PipelineBuildInfo rtPipelineInfo;
        rtPipelineInfo.descriptors = std::vector<DescriptorSet>();
        rtPipelineInfo.pipelineType = PathTracing;
        rtPipelineInfo.shaderInfo = RTshaderPairs;
        rtPipelineInfo.useGlobalDescriptor = true;
    }

    void RenderEngine::loadModels(std::string modelsDir)
    {
        const std::filesystem::path models(modelsDir);
        for(const auto& model : std::filesystem::directory_iterator(models)) //iterate models
        {
            if(model.path().filename().string().find(".fbx") != std::string::npos ) //must be a valid .fbx file
            {
                std::cout << "loading model: " << model.path().stem().string() << std::endl;
                this->models.insert(std::make_pair(
                    model.path().stem().string(),
                    std::make_shared<Model>(&device, &commands, model.path().string())));
            }
        }
    }

    void RenderEngine::loadMaterials(std::string materialsDir)
    {
        //iterate materials path
        const std::filesystem::path materials(materialsDir);
        for(const auto& material : std::filesystem::directory_iterator(materials)) //iterate models
        {
            if(material.path().filename().string().find(".mat") != std::string::npos ) //must be a valid .fbx file
            {
                std::cout << "loading material: " << material.path().stem().string() << std::endl;
                createMaterial(material.path().string());
            }
        }
    }

    void RenderEngine::createMaterial(std::string filePath)
    {
        std::ifstream file(filePath);
        if(file.is_open())
        {
            std::string matName = "UNDEFINED";
            PipelineType type = UNDEFINED;
            std::vector<std::string> textureNames(TEXTURE_ARRAY_SIZE);
            std::vector<glm::vec4> vec4vars(TEXTURE_ARRAY_SIZE);
            
            for(std::string line; std::getline(file, line); )
            {
                if(line.find("#") != std::string::npos) //material name
                {
                    matName = line.substr(line.find("#") + 1, line.length() - line.find("#"));

                    continue;
                }

                if(line.find("pipeline") != std::string::npos && line.find("=") != std::string::npos) //pipeline equals
                {
                    if(line.find("\"PBR\"") != std::string::npos)
                    {
                        type = PipelineType::PBR;
                    }
                    else if(line.find("\"TexturelessPBR\"") != std::string::npos)
                    {
                        type = PipelineType::TexturelessPBR;
                    }
                    else
                    {
                        throw std::runtime_error("Unsupported pipeline specified at material: " + filePath);
                    }

                    continue;
                }
                
                if(line.find("Tex_") != std::string::npos && line.find("=") != std::string::npos)
                {
                    if(line.find("diffuse") != std::string::npos)
                    {
                        std::string name = line.substr(line.find("=") + 1, line.length() - line.find("="));
                        textureNames.at(0) = name.substr(name.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                    }
                    else if(line.find("metalness") != std::string::npos)
                    {
                        std::string name = line.substr(line.find("=") + 1, line.length() - line.find("="));
                        textureNames.at(1) = name.substr(name.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                    }
                    else if(line.find("normal") != std::string::npos)
                    {
                        std::string name = line.substr(line.find("=") + 1, line.length() - line.find("="));
                        textureNames.at(2) = name.substr(name.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                    }
                    else if(line.find("roughness") != std::string::npos)
                    {
                        std::string name = line.substr(line.find("=") + 1, line.length() - line.find("="));
                        textureNames.at(3) = name.substr(name.find("\"") + 1, line.rfind("\"") - line.find("\"") - 1);
                    }

                    continue;
                }
                else if(line.find("Color_") != std::string::npos && line.find("=") != std::string::npos)
                {
                    if(line.find("diffuse") != std::string::npos)
                    {
                        float r, g, b, a;
                        std::string content = line.substr(line.find("\"(") + 2, line.length() - line.find("\"("));
                        content = content.substr(0, content.find(")\""));
                        
                        std::stringstream ss(content);
                        ss >> r >> g >> b >> a;

                        vec4vars.at(0) = (glm::vec4(r, g, b, a));
                    }
                    else if(line.find("metalness") != std::string::npos)
                    {
                        float r, g, b, a;
                        std::string content = line.substr(line.find("\"(") + 2, line.length() - line.find("\"("));
                        content = content.substr(0, content.find(")\""));
                        
                        std::stringstream ss(content);
                        ss >> r >> g >> b >> a;

                        vec4vars.at(1) = (glm::vec4(r, g, b, a));
                    }
                    else if(line.find("normal") != std::string::npos)
                    {
                        float r, g, b, a;
                        std::string content = line.substr(line.find("\"(") + 2, line.length() - line.find("\"("));
                        content = content.substr(0, content.find(")\""));
                        
                        std::stringstream ss(content);
                        ss >> r >> g >> b >> a;

                        vec4vars.at(2) = (glm::vec4(r, g, b, a));
                    }
                    else if(line.find("roughness") != std::string::npos)
                    {
                        float r, g, b, a;
                        std::string content = line.substr(line.find("\"(") + 2, line.length() - line.find("\"("));
                        content = content.substr(0, content.find(")\""));
                        
                        std::stringstream ss(content);
                        ss >> r >> g >> b >> a;

                        vec4vars.at(3) = (glm::vec4(r, g, b, a));
                    }

                    continue;
                }
            }
            if(matName == "UNDEFINED" || renderTree.count(type) == 0) throw std::runtime_error("Not all material parameters specified at " + filePath);
            
            std::vector<Texture const*> textures;
            for(std::string name : textureNames)
            {
                textures.push_back(getTextureByName(name));
            }
            textures.resize(TEXTURE_ARRAY_SIZE);

            materials.insert(std::make_pair(
                matName,
                std::make_shared<Material>(&device, &commands, renderTree.at(type).pipeline.get(), matName, textures, vec4vars)));

            MaterialNode matParams;
            matParams.material = materials.at(matName).get();
            matParams.objectBuffer = std::make_shared<IndirectDrawBuffer>(&device, &commands, &descriptors, renderTree.at(type).pipeline.get());
            
            renderTree.at(type).materials.insert(std::make_pair(matName, matParams));
        }
        else throw std::runtime_error("Error opening material file at " + filePath);

    }

    void RenderEngine::loadTextures(std::string texturesDir)
    {
        const std::filesystem::path textures(texturesDir);
        for(const auto& texture : std::filesystem::recursive_directory_iterator(textures)) //iterate models RECURSIVE BECAUSE TEXTURES CAN BE IN FOLDERS
        {
            if(texture.path().filename().string().find(".png") != std::string::npos ||
                texture.path().filename().string().find(".jpeg") != std::string::npos) //must be a valid .png/jpg file
            {
                std::cout << "loading texture: " << texture.path().stem().string() << std::endl;
                Image imageData = loadImage(texture.path().string());

                this->textures.insert(std::make_pair(
                    texture.path().stem().string(),
                    std::make_shared<Texture>(&device, &commands, &imageData)));
                
                stbi_image_free(imageData.data);
            }
        }
    }

    void RenderEngine::initRT()
    {
        BottomAccelerationStructureData bottomData;
        for(auto& [name, model] : models)
        {
            AccelerationStructureModelReference modelRef;
            modelRef.modelPointer = model.get();
            for(const ModelMesh& mesh : model->getModelMeshes())
            {
                modelRef.meshes.push_back(&mesh);
            }
            bottomData.models.push_back(modelRef);
        }
        rtAccelStructure.createBottomLevel(bottomData);
    }

    Image RenderEngine::loadImage(std::string directory)
    {
        Image returnImg;
        returnImg.data = stbi_load(directory.c_str(), &returnImg.width, &returnImg.height, &returnImg.channels, STBI_rgb_alpha);
        returnImg.size = returnImg.width * returnImg.height * 4;

        if(!returnImg.data) //image is NULL by default
        {
            throw std::runtime_error("failed to load texture image!");
        }

        return returnImg;
    }

    void RenderEngine::addObject(ModelInstance& object)
    {
        if(object.modelPtr != NULL)
        {
            for(uint32_t i = 0; i < object.modelPtr->getModelMeshes().size(); i++)
            {
                if(object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)) //make sure a valid material is set
                {
                    PipelineType pipelineType = object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)->getPipelineType();
                    std::string matName = object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)->getMatName();
                    
                    object.objRefs[i] = {
                        .modelMatrix = &object.modelMatrix,
                        .mesh = object.modelPtr->getModelMeshes().at(i).mesh.get()
                    };

                    renderTree.at(pipelineType).materials.at(matName).objectBuffer->addElement(object.objRefs.at(i));
                }
            }
        }
    }

    void RenderEngine::removeObject(ModelInstance& object)
    {
        for(uint32_t i = 0; i < object.modelPtr->getModelMeshes().size(); i++)
        {
            if(object.objRefs.count(i))
            {
                PipelineType pipelineType = object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)->getPipelineType();
                std::string matName = object.materials.at(object.modelPtr->getModelMeshes().at(i).materialIndex)->getMatName();

                renderTree.at(pipelineType).materials.at(matName).objectBuffer->removeElement(object.objRefs.at(i));
            }
        }
    }

    void RenderEngine::drawAllReferences()
    {
        VkCommandBuffer cmdBuffer = rendering.startNewFrame();

        //RT pass
        /*AccelerationData accelData = {};
        for(const auto& [pipelineType, pipelineNode] : renderTree) //pipeline
        {
            for(const auto& [materialName, materialNode] : pipelineNode.materials) //material
            {
                for(const auto& [mesh, node] : materialNode.objectBuffer->getDrawCallTree()) //similar objects
                {
                    for(auto object = node.objects.begin(); object != node.objects.end(); object++)
                    {
                        VkTransformMatrixKHR matrix;
                        matrix.matrix[0][0] = (*((*object)->modelMatrix))[0][0];
                        matrix.matrix[0][1] = (*((*object)->modelMatrix))[0][1];
                        matrix.matrix[0][2] = (*((*object)->modelMatrix))[0][2];
                        matrix.matrix[0][3] = (*((*object)->modelMatrix))[0][3];

                        matrix.matrix[1][0] = (*((*object)->modelMatrix))[1][0];
                        matrix.matrix[1][1] = (*((*object)->modelMatrix))[1][1];
                        matrix.matrix[1][2] = (*((*object)->modelMatrix))[1][2];
                        matrix.matrix[1][3] = (*((*object)->modelMatrix))[1][3];

                        matrix.matrix[2][0] = (*((*object)->modelMatrix))[2][0];
                        matrix.matrix[2][1] = (*((*object)->modelMatrix))[2][1];
                        matrix.matrix[2][2] = (*((*object)->modelMatrix))[2][2];
                        matrix.matrix[2][3] = (*((*object)->modelMatrix))[2][3];
                    }
                }
            }
        }
        rtAccelStructure.buildStructure(accelData);*/
        
        //raster pass
        for(const auto& [pipelineType, pipelineNode] : renderTree) //pipeline
        {
            rendering.bindPipeline(pipelineNode.pipeline.get(), cmdBuffer);
            for(const auto& [materialName, materialNode] : pipelineNode.materials) //material
            {
                //if(materialNode.objectBuffer->getDrawCount() == 0) continue; potential for optimization

                rendering.bindMaterial(materialNode.material, cmdBuffer);
                rendering.drawIndexedIndirect(cmdBuffer, materialNode.objectBuffer.get());
            }
        }

        rendering.incrementFrameCounter(cmdBuffer);
        glfwPollEvents();
    }

    //----------GETTER/SETTER FUNCTIONS----------//

    Model const* RenderEngine::getModelByName(std::string name)
    {
        if(this->models.count(name))
        {
            return this->models.at(name).get();
        }
        return NULL;
    }

    Material const* RenderEngine::getMaterialByName(std::string name)
    {
        if(this->materials.count(name))
        {
            return this->materials.at(name).get();
        }
        return NULL;
    }

    void RenderEngine::setCamera(Camera* camera)
    {
        this->rendering.setCamera(camera);
    }

    Texture const* RenderEngine::getTextureByName(std::string name)
    {
        if(this->textures.count(name))
        {
            return this->textures.at(name).get();
        }
        return NULL; //default texture already exists in texture class
    }
}
