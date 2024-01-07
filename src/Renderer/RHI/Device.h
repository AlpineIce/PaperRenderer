#pragma once
#include "vulkan/vulkan.hpp"
#include "vk_mem_alloc.h"
#include "GLFW/glfw3.h"

#include <vector>
#include <string>
#include <map>

namespace Renderer
{
    struct QueueFamiliesIndices
    {
        int graphicsFamilyIndex = -1;
        int computeFamilyIndex = -1;
        int transferFamilyIndex = -1;
        int presentationFamilyIndex = -1;
    };

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
        VmaAllocator allocator;
        VkInstance instance;
        VkPhysicalDevice GPU;
        VkPhysicalDeviceProperties gpuProperties;
        VkPhysicalDeviceFeatures gpuFeatures;
        QueueFamiliesIndices queueFamilies;
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
        void initVma();
        
    public:
        Device(std::string appName);
        ~Device();

        VkSurfaceKHR* getSurfacePtr() { return &surface; }
        VmaAllocator getAllocator() const { return allocator; }
        VkInstance getInstance() const { return instance; }
        VkPhysicalDevice getGPU() const { return GPU; }
        VkPhysicalDeviceProperties getGPUProperties() const { return gpuProperties; }
        VkPhysicalDeviceFeatures getGPUFeatures() const { return gpuFeatures; }
        QueueFamiliesIndices getQueueFamilies() const { return queueFamilies; }
        VkDevice getDevice() const { return device; }
        Queues getQueues() const { return queues; }

        
        

        void createDevice();
    };
}