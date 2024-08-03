#pragma once
#include "VulkanResources.h"
#include "GLFW/glfw3.h"

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <stdexcept>

namespace PaperRenderer
{
    class Device
    {
    private:
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice GPU = VK_NULL_HANDLE;
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

        void createContext(std::string appName);
        void findGPU();
        void findQueueFamilies(uint32_t& queueFamilyCount, std::vector<VkQueueFamilyProperties>& queueFamiliesProperties);
        void createQueues(std::unordered_map<uint32_t, VkDeviceQueueCreateInfo>& queuesCreationInfo,
                          const std::vector<VkQueueFamilyProperties>& queueFamiliesProperties, 
                          float* queuePriority);
        void retrieveQueues(std::unordered_map<uint32_t, VkDeviceQueueCreateInfo>& queuecreationInfo);
        
    public:
        Device(std::string appName);
        ~Device();

        void createDevice();

        VkDevice getDevice() const { return device; }
        VkSurfaceKHR* getSurfacePtr() { return &surface; }
        VkInstance getInstance() const { return instance; }
        VkPhysicalDevice getGPU() const { return GPU; }
        VkPhysicalDeviceProperties2 getGPUProperties() const { return gpuProperties; }
        VkPhysicalDeviceFeatures getGPUFeatures() const { return gpuFeatures; }
        const bool& getRTSupport() const { return rtSupport; }
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR getRTproperties() const { return rtPipelineProperties; }
        VkPhysicalDeviceAccelerationStructurePropertiesKHR getASproperties() const { return asProperties; }
        const std::unordered_map<QueueType, QueuesInFamily>& getQueues() const { return queues; }
        QueueFamiliesIndices getQueueFamiliesIndices() const;
    };
}