#pragma once
#include "vulkan/vulkan.hpp"
#include "Buffer.h"

#include <unordered_map>
#include <memory>

namespace Renderer
{
    //----------DYNAMIC UBO STRUCTS----------//

    const uint32_t TEXTURE_ARRAY_SIZE = 8;
    const uint32_t MAX_POINT_LIGHTS = 8;

    struct UniformBufferObject
    {
        struct Globals
        {
            glm::mat4 view;
            glm::mat4 projection;
        } globals;

        struct SceneInfo
        {
            glm::vec3 pointLights[MAX_POINT_LIGHTS];
            glm::vec2 sunDirection;
        } sceneInfo;
        
        struct PBRpipeline
        {
            VkSampler textures[TEXTURE_ARRAY_SIZE];
        } PBR;

        struct TexturelessPBRpipeline
        {
            glm::vec4 inColors;
        } texturelessPBR;
    };
    
    class Descriptors
    {
    private:
        
        VkDescriptorPool descriptorPool;
        VkDescriptorSetLayout descriptorLayout;
        VkDescriptorSet descriptorSet;
        std::shared_ptr<UniformBuffer> UBO;
        std::vector<uint32_t> offsets;

        Device* devicePtr;
        Commands* commandsPtr;
        std::shared_ptr<Texture> defaultTexture;

        uint32_t getOffsetOf(uint32_t bytesSize);

        void createLayout();
        void createDescriptorPool();
        void allocateDescriptors();
        void writeUniform();

    public:
        Descriptors(Device* device, Commands* commands);
        ~Descriptors();

        void updateUBO(void* updateData, uint32_t offset, uint32_t size);
        void updateTextures(std::vector<Texture const*> textures);
        void addUniform(uint32_t size);

        VkDescriptorSetLayout const* getSetLayoutPtr() const { return &descriptorLayout; }
        VkDescriptorSet const* getDescriptorSetPtr() const { return &descriptorSet; }
        std::vector<uint32_t> getOffsets() const { return offsets; }

    };
}