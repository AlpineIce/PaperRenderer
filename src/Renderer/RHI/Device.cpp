#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE 
#include "glm/glm.hpp"
#include "Device.h"

#include <iostream>
#include <list>
#include <unordered_map>

namespace Renderer
{
    Device::Device(std::string appName)
    {
        glfwInit();
        createContext(appName);
        findGPU();
    }

    Device::~Device()
    {
        vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    }

    void Device::createContext(std::string appName)
    {
        //----------INSTANCE CREATION----------//


        //extensions
        unsigned int glfwExtensionCount = 0;
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> glfwExtensions(glfwExtensionCount);
        for(int i = 0; i < glfwExtensionCount; i++)
        {
            glfwExtensions[i] = glfwGetRequiredInstanceExtensions(&glfwExtensionCount)[i];
        }
        
        //layers
        std::vector<const char*> layerNames;
#ifndef NDEBUG
        layerNames.push_back("VK_LAYER_KHRONOS_validation");
#endif

        std::vector<const char*> extensionNames;
        extensionNames.insert(extensionNames.end(), glfwExtensions.begin(), glfwExtensions.end());

        VkApplicationInfo appInfo;
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pNext = NULL;
        vkEnumerateInstanceVersion(&appInfo.apiVersion);
        appInfo.applicationVersion = VK_API_VERSION_MAJOR(1);
        appInfo.engineVersion = VK_API_VERSION_MAJOR(1);
        appInfo.pApplicationName = appName.c_str();
        appInfo.pEngineName = appName.c_str();

        VkInstanceCreateInfo instanceInfo;
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.flags = 0;
        instanceInfo.pApplicationInfo = &appInfo;
        instanceInfo.pNext = NULL;
        instanceInfo.enabledExtensionCount = extensionNames.size();
        instanceInfo.ppEnabledExtensionNames = extensionNames.data();
        instanceInfo.enabledLayerCount = 0;
        instanceInfo.ppEnabledLayerNames = layerNames.data();

        //instance creation
        VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance);
        if(result != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan instance");
    }

    void Device::findGPU()
    {
        uint32_t deviceCount;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

        bool deviceFound = false;
        for(VkPhysicalDevice physicalDevice : physicalDevices)
        {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(physicalDevice, &properties);

            if(properties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                gpuProperties = properties;
                GPU = physicalDevice;
                vkGetPhysicalDeviceFeatures(GPU, &gpuFeatures);
                deviceFound = true;

                break; //break prefers discrete gpu over integrated if available
            }

            if(properties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            {
                gpuProperties = properties;
                GPU = physicalDevice;
                vkGetPhysicalDeviceFeatures(GPU, &gpuFeatures);
                deviceFound = true;
            }
        }

        if(!deviceFound && physicalDevices.size() > 0)
        {
            vkGetPhysicalDeviceProperties(physicalDevices.at(0), &gpuProperties);
            GPU = physicalDevices.at(0);
            vkGetPhysicalDeviceFeatures(GPU, &gpuFeatures);
        }
        else if(!deviceFound)
        {
            throw std::runtime_error("Couldn't find suitable GPU");
        }
    }

    void Device::findQueueFamilies(uint32_t& queueFamilyCount, std::vector<VkQueueFamilyProperties>& queueFamiliesProperties)
    {
        vkGetPhysicalDeviceQueueFamilyProperties(GPU, &queueFamilyCount, nullptr);
        queueFamiliesProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(GPU, &queueFamilyCount, queueFamiliesProperties.data());

        //0 = graphics
        //1 = compute
        //2 = transfer
        //3 = present
                           //type,   list of families with type
        std::unordered_map<uint32_t, std::list<uint32_t>> queueFamilyMap;
        for(int i = 0; i < queueFamiliesProperties.size(); i++)
        {
            if((queueFamiliesProperties[i].queueFlags & VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT) && (queueFamilies.graphicsFamilyIndex == -1)) //graphics
            {
                queueFamilyMap[0].push_back(i);
            }
            if((queueFamiliesProperties[i].queueFlags & VkQueueFlagBits::VK_QUEUE_COMPUTE_BIT) && (queueFamilies.computeFamilyIndex == -1)) //compute
            {
                queueFamilyMap[1].push_back(i);
            }
            if((queueFamiliesProperties[i].queueFlags & VkQueueFlagBits::VK_QUEUE_TRANSFER_BIT) && (queueFamilies.transferFamilyIndex == -1)) //transfer
            {
                queueFamilyMap[2].push_back(i);
            }

            VkBool32 presentSupport;
            vkGetPhysicalDeviceSurfaceSupportKHR(GPU, i, surface, &presentSupport);
            if(presentSupport) //present on graphics queue by default
            {
                queueFamilyMap[3].push_back(i);
            }
        }

        //filter graphics queues
        if(queueFamilyMap[0].size() == 0) throw std::runtime_error("No graphics support from auto-selected GPU");
        queueFamilies.graphicsFamilyIndex = queueFamilyMap[0].front();
        for(auto& [type, families] : queueFamilyMap)
        {
            if(type == 0) continue;

            int removeFamily = -1;
            if(families.size() > 1)
            {
                for(uint32_t family : families)
                {
                    if((int)family == queueFamilies.graphicsFamilyIndex)
                    {
                        removeFamily = family;
                        break;
                    }
                }
            }

            if(removeFamily != -1)
            {
                families.remove(removeFamily);
                continue;
            }
        }

        //filter compute queues
        if(queueFamilyMap[1].size() == 0) throw std::runtime_error("No compute support from auto-selected GPU");
        queueFamilies.computeFamilyIndex = queueFamilyMap[1].front();
        for(auto& [type, families] : queueFamilyMap)
        {
            if(type == 0 || type == 1) continue;

            int removeFamily = -1;
            if(families.size() > 1)
            {
                for(uint32_t family : families)
                {
                    if((int)family == queueFamilies.computeFamilyIndex)
                    {
                        removeFamily = family;
                        break;
                    }
                }
            }

            if(removeFamily != -1)
            {
                families.remove(removeFamily);
                continue;
            }
        }

        //filter presentation from leftover transfer
        if(queueFamilyMap[3].size() == 0) throw std::runtime_error("No presentation support from auto-selected GPU");
        queueFamilies.presentationFamilyIndex = queueFamilyMap[3].front();
        for(auto& [type, families] : queueFamilyMap)
        {
            if(type == 0 || type == 1 || type == 3) continue;

            int removeFamily = -1;
            if(families.size() > 1)
            {
                for(uint32_t family : families)
                {
                    if((int)family == queueFamilies.presentationFamilyIndex)
                    {
                        removeFamily = family;
                        break;
                    }
                }
            }

            if(removeFamily != -1)
            {
                families.remove(removeFamily);
                continue;
            }

            //transfer gets leftovers (lmao)
            queueFamilies.transferFamilyIndex = queueFamilyMap[2].front();
        }
    }

    void Device::createQueues(std::vector<VkDeviceQueueCreateInfo>& queuesCreationInfo,
                              const std::vector<VkQueueFamilyProperties>& queueFamiliesProperties, 
                              float* queuePriority)
    {
        familyOwnerships[queueFamilies.graphicsFamilyIndex]++;
        familyOwnerships[queueFamilies.computeFamilyIndex]++;
        familyOwnerships[queueFamilies.transferFamilyIndex]++;
        familyOwnerships[queueFamilies.presentationFamilyIndex]++;

        for(auto const& [queueFamily, owners] : familyOwnerships)
        {
            if(owners > 0)
            {
                queuesCreationInfo.push_back({
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .pNext = NULL,
                    .flags = 0,
                    .queueFamilyIndex = queueFamily,
                    .queueCount = queueFamiliesProperties[queueFamily].queueCount,
                    .pQueuePriorities = queuePriority
                });
            }
        }
    }

    void Device::retrieveQueues(const std::vector<VkQueueFamilyProperties>& queueFamiliesProperties)
    {   
        //only graphics queue for this family
        if(familyOwnerships[queueFamilies.graphicsFamilyIndex] == 1) 
        {
            queues.graphics.resize(queueFamiliesProperties[queueFamilies.graphicsFamilyIndex].queueCount);
            for(int i = 0; i < queues.graphics.size(); i++)
            {
                vkGetDeviceQueue(device, queueFamilies.graphicsFamilyIndex, i, &queues.graphics[i]);
            }
        }
        //only transfer queue for this family (usually guaranteed)
        if(familyOwnerships[queueFamilies.transferFamilyIndex] == 1) 
        {
            queues.transfer.resize(queueFamiliesProperties[queueFamilies.transferFamilyIndex].queueCount);
            for(int i = 0; i < queues.transfer.size(); i++)
            {
                vkGetDeviceQueue(device, queueFamilies.transferFamilyIndex, i, &queues.transfer[i]);
            }
        }
        //one transfer, 3 for compute, graphics, and present (screw intel GPU users), or just a shared present queue
        if(familyOwnerships[queueFamilies.presentationFamilyIndex] < 4)
        {
            unsigned int poolSize = queueFamiliesProperties[queueFamilies.presentationFamilyIndex].queueCount;
            unsigned int numPresentQueues = 1;

            //grab the queue at the end of the family
            queues.present.resize(numPresentQueues);
            for(int i = 0; i < numPresentQueues; i++)
            {
                vkGetDeviceQueue(device, queueFamilies.presentationFamilyIndex, poolSize - numPresentQueues + i, &queues.present[i]);
            }
            
            //split compute and graphics if 3 shared queues
            unsigned int numComputeQueues = 0;
            if(familyOwnerships[queueFamilies.presentationFamilyIndex] == 3)
            {
                numComputeQueues = 2;
                queues.compute.resize(numComputeQueues);
                //get compute queues
                for(int i = 0; i < queues.compute.size(); i++)
                {
                    vkGetDeviceQueue(device, queueFamilies.computeFamilyIndex, poolSize - (numPresentQueues + numComputeQueues) + i, &queues.compute[i]);
                }
                //get remaining graphics queues
                queues.graphics.resize(poolSize - (numComputeQueues + numPresentQueues));
                for(int i = 0; i < queues.graphics.size(); i++)
                {
                    vkGetDeviceQueue(device, queueFamilies.graphicsFamilyIndex, i,  &queues.graphics[i]);
                }

                return;
            }

            //otherwise, presentation and compute
            if(queueFamilies.presentationFamilyIndex == queueFamilies.computeFamilyIndex)
            {
                //grab remaining compute queues
                queues.compute.resize(poolSize - numPresentQueues);
                for(int i = 0; i < queues.compute.size(); i++)
                {
                    vkGetDeviceQueue(device, queueFamilies.computeFamilyIndex, i, &queues.compute[i]);
                }
            }

            //presentation and graphics
            if(queueFamilies.presentationFamilyIndex == queueFamilies.graphicsFamilyIndex)
            {
                //grab remaining graphics queues
                queues.graphics.resize(poolSize - numPresentQueues);
                for(int i = 0; i < queues.graphics.size(); i++)
                {
                    vkGetDeviceQueue(device, queueFamilies.graphicsFamilyIndex, i, &queues.graphics[i]);
                }
            }
        }
    }

    void Device::initVma()
    {
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo vmaInfo = {};
        vmaInfo.physicalDevice = GPU;
        vmaInfo.device = device;
        vmaInfo.instance = instance;
        vmaInfo.pVulkanFunctions = &vulkanFunctions;

        VkResult result = vmaCreateAllocator(&vmaInfo, &allocator);
    }

    void Device::createDevice()
    {
        //enable anisotropy
        gpuFeatures.samplerAnisotropy = VK_TRUE;

        //----------QUEUE SETUP----------//

        uint32_t queueFamilyCount;
        std::vector<VkQueueFamilyProperties> queueFamiliesProperties;
        findQueueFamilies(queueFamilyCount, queueFamiliesProperties);
        
        std::vector<VkDeviceQueueCreateInfo> queuesCreationInfo;
        float queuePriority[16] = {0.5f};   //TODO fix this because i didnt know what the spec was telling at the time
        createQueues(queuesCreationInfo, queueFamiliesProperties, queuePriority);

        //----------LOGICAL DEVICE CREATION----------//

        std::vector<const char*> extensionNames;
        extensionNames.insert(extensionNames.end(), {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME});

        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures = {};
        accelerationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        accelerationFeatures.pNext = NULL;
        accelerationFeatures.accelerationStructure = VK_TRUE;

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR  RTfeatures = {};
        RTfeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        RTfeatures.pNext = &accelerationFeatures;
        RTfeatures.rayTracingPipeline = VK_TRUE;

        VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderFeatures = {};
        dynamicRenderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
        dynamicRenderFeatures.pNext = &RTfeatures;
        dynamicRenderFeatures.dynamicRendering = VK_TRUE;

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pNext = &dynamicRenderFeatures;
        deviceCreateInfo.flags = 0;
        deviceCreateInfo.queueCreateInfoCount = queuesCreationInfo.size();
        deviceCreateInfo.pQueueCreateInfos = queuesCreationInfo.data();
        deviceCreateInfo.pEnabledFeatures = NULL;
        deviceCreateInfo.enabledExtensionCount = extensionNames.size();
        deviceCreateInfo.ppEnabledExtensionNames = extensionNames.data();
        deviceCreateInfo.pEnabledFeatures = &gpuFeatures;

        VkResult result = vkCreateDevice(GPU, &deviceCreateInfo, NULL, &device);
        if(result != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan device");

        retrieveQueues(queueFamiliesProperties);
        initVma();
    }
}