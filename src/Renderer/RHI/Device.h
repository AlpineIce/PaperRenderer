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
    struct Queues
    {
        std::vector<VkQueue> graphics;
        std::vector<VkQueue> compute;
        std::vector<VkQueue> transfer;
        std::vector<VkQueue> present;
    };

    class Device
    {
    private:
        VkInstance instance;
        VkPhysicalDevice GPU;
        VkPhysicalDeviceProperties2 gpuProperties;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties = {};
        VkPhysicalDeviceFeatures gpuFeatures;
        PaperMemory::QueueFamiliesIndices queueFamilies;
        std::unique_ptr<PaperMemory::Commands> commands;
        VkDevice device;
        VkSurfaceKHR surface;
        Queues queues;
        std::map<unsigned int, unsigned int> familyOwnerships;

        void createContext(std::string appName);
        void findGPU();
        void findQueueFamilies(uint32_t& queueFamilyCount, std::vector<VkQueueFamilyProperties>& queueFamiliesProperties);
        void createQueues(std::vector<VkDeviceQueueCreateInfo>& queuesCreationInfo,
                          const std::vector<VkQueueFamilyProperties>& queueFamiliesProperties, 
                          float* queuePriority);
        void retrieveQueues(const std::vector<VkQueueFamilyProperties>& queueFamiliesProperties);
        
    public:
        Device(std::string appName);
        ~Device();

        VkSurfaceKHR* getSurfacePtr() { return &surface; }
        VkInstance getInstance() const { return instance; }
        VkPhysicalDevice getGPU() const { return GPU; }
        VkPhysicalDeviceProperties2 getGPUProperties() const { return gpuProperties; }
        VkPhysicalDeviceFeatures getGPUFeatures() const { return gpuFeatures; }
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR getRTproperties() const { return rtPipelineProperties; }
        PaperMemory::QueueFamiliesIndices getQueueFamilies() const { return queueFamilies; }
        VkDevice getDevice() const { return device; }
        Queues getQueues() const { return queues; }

        void createDevice();
    };
}