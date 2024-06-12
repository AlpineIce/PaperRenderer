#pragma once
#include "Memory/VulkanResources.h"
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
        VkInstance instance;
        VkPhysicalDevice GPU;
        std::vector<VkExtensionProperties> extensions;
        VkPhysicalDeviceProperties2 gpuProperties;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties = {};
        VkPhysicalDeviceFeatures gpuFeatures;
        std::unordered_map<uint32_t, std::vector<PaperMemory::Queue>> familyQueues;
        std::unordered_map<PaperMemory::QueueType, PaperMemory::QueuesInFamily> queues;
        std::unique_ptr<PaperMemory::Commands> commands;
        VkDevice device;
        VkSurfaceKHR surface;
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
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR getRTproperties() const { return rtPipelineProperties; }
        const std::unordered_map<PaperMemory::QueueType, PaperMemory::QueuesInFamily>& getQueues() const { return queues; }
        PaperMemory::QueueFamiliesIndices getQueueFamiliesIndices() const;
    };
}