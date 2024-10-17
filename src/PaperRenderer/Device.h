#pragma once
#include "volk.h"
#include "vk_mem_alloc.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "Command.h"

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <atomic>

namespace PaperRenderer
{
    class Device
    {
    private:
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice GPU = VK_NULL_HANDLE;
        VmaAllocator allocator = NULL;
        std::vector<VkExtensionProperties> extensions;
        VkPhysicalDeviceProperties2 gpuProperties;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties = {};
        VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties = {};
        VkPhysicalDeviceFeatures gpuFeatures;
        std::unordered_map<uint32_t, std::vector<Queue>> familyQueues;
        std::unordered_map<QueueType, QueuesInFamily> queues;
        std::unique_ptr<Commands> commands;
        VkDevice device = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        bool rtSupport = false;

        class RenderEngine* rendererPtr;

        void createContext(std::string appName);
        void findGPU();
        void findQueueFamilies(uint32_t& queueFamilyCount, std::vector<VkQueueFamilyProperties>& queueFamiliesProperties);
        void createQueues(std::unordered_map<uint32_t, VkDeviceQueueCreateInfo>& queuesCreationInfo,
                          const std::vector<VkQueueFamilyProperties>& queueFamiliesProperties, 
                          float* queuePriority);
        void retrieveQueues(std::unordered_map<uint32_t, VkDeviceQueueCreateInfo>& queuecreationInfo);
        
    public:
        Device(class RenderEngine* renderer, std::string appName);
        ~Device();
        Device(const Device&) = delete;

        void createDevice();

        static VkDeviceSize getAlignment(VkDeviceSize size, VkDeviceSize alignment) { return (size + (alignment - 1)) & ~(alignment - 1); };
        const VkDevice& getDevice() const { return device; }
        const VmaAllocator& getAllocator() const { return allocator; }
        const VkSurfaceKHR& getSurface() { return surface; }
        const VkInstance& getInstance() const { return instance; }
        const VkPhysicalDevice& getGPU() const { return GPU; }
        const VkPhysicalDeviceProperties2& getGPUProperties() const { return gpuProperties; }
        const VkPhysicalDeviceFeatures& getGPUFeatures() const { return gpuFeatures; }
        const bool& getRTSupport() const { return rtSupport; }
        const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& getRTproperties() const { return rtPipelineProperties; }
        const VkPhysicalDeviceAccelerationStructurePropertiesKHR& getASproperties() const { return asProperties; }
        const std::unordered_map<QueueType, QueuesInFamily>& getQueues() const { return queues; }
        Commands* getCommandsPtr() { return commands.get(); }
        QueueFamiliesIndices getQueueFamiliesIndices() const;
    };
}