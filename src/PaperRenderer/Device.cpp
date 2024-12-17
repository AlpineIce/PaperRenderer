#include "Device.h"
#include "PaperRenderer.h"

#include <iostream>
#include <unordered_map>

namespace PaperRenderer
{
    Device::Device(RenderEngine& renderer, std::string appName)
        :renderer(renderer)
    {
        //volk
        VkResult result = volkInitialize();
        glfwInit();
        createContext(appName);
        findGPU();
    }

    Device::~Device()
    {
        commands.reset();
        vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);

        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Device destructor initialized"
        });
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
        std::vector<const char*> layerNames = {};
        std::vector<const char*> extensionNames = {
            VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME
        };
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
        instanceInfo.enabledLayerCount = layerNames.size();
        instanceInfo.ppEnabledLayerNames = layerNames.data();

        //instance creation
        VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance);
        if(result != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan instance");

        volkLoadInstance(instance);
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
            asProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
            asProperties.pNext = NULL;
            
            rtPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            rtPipelineProperties.pNext = &asProperties;

            VkPhysicalDeviceProperties2 properties;
            properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            properties.pNext = &rtPipelineProperties;
            vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

            uint32_t extensionCount;
            vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, NULL);
            extensions.resize(extensionCount);
            vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, extensions.data());

            //raster extensions
            bool hasSwapchain = false;
            bool hasDynamicRendering = false;
            bool hasSync2 = false;

            //rt extensions
            bool hasDeferredOps = false;
            bool hasAccelStructure = false;
            bool hasRTPipeline = false;
            bool hasRayQuery = false;
            bool hasMaintFeatures = false;

            //check extensions
            for(VkExtensionProperties properties : extensions)
            {
                //required for raster
                hasSwapchain = hasSwapchain || std::string(properties.extensionName).find(VK_KHR_SWAPCHAIN_EXTENSION_NAME) != std::string::npos;
                hasDynamicRendering = hasDynamicRendering || std::string(properties.extensionName).find(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) != std::string::npos;
                hasSync2 = hasSync2 || std::string(properties.extensionName).find(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) != std::string::npos;

                //optional extensions for RT
                hasDeferredOps = hasDeferredOps || std::string(properties.extensionName).find(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) != std::string::npos;
                hasAccelStructure = hasAccelStructure || std::string(properties.extensionName).find(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) != std::string::npos;
                hasRTPipeline = hasRTPipeline || std::string(properties.extensionName).find(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) != std::string::npos;
                hasRayQuery = hasRayQuery || std::string(properties.extensionName).find(VK_KHR_RAY_QUERY_EXTENSION_NAME) != std::string::npos;
                hasMaintFeatures = hasMaintFeatures || std::string(properties.extensionName).find(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME) != std::string::npos;
            }

            bool hasRequiredRasterExtensions = hasSwapchain && hasDynamicRendering && hasSync2;
            bool hasRequiredRTExtensions = hasDeferredOps && hasAccelStructure && hasRTPipeline && hasRayQuery && hasMaintFeatures;
            if(hasRequiredRasterExtensions) //only needs raster extensions to run
            {
                rtSupport = hasRequiredRTExtensions; //enable rt if all required extensions are satisfied
                if(properties.properties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                {
                    gpuProperties = properties;
                    GPU = physicalDevice;
                    vkGetPhysicalDeviceFeatures(GPU, &gpuFeatures);
                    deviceFound = true;

                    break; //break prefers discrete gpu over other gpu types
                }
                else
                {
                    gpuProperties = properties;
                    GPU = physicalDevice;
                    vkGetPhysicalDeviceFeatures(GPU, &gpuFeatures);
                    deviceFound = true;
                }
            }
        }

        if(!deviceFound && physicalDevices.size() > 0)
        {
            gpuProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            gpuProperties.pNext = &rtPipelineProperties;
            
            vkGetPhysicalDeviceProperties2(physicalDevices.at(0), &gpuProperties);
            GPU = physicalDevices.at(0);
            vkGetPhysicalDeviceFeatures(GPU, &gpuFeatures);
        }
        else if(!deviceFound)
        {
            throw std::runtime_error("Couldn't find suitable GPU");
        }

        //record log
        renderer.getLogger().recordLog({
                .type = INFO,
                .text = std::string("Using GPU: ") + gpuProperties.properties.deviceName
            });
    }

    void Device::findQueueFamilies(uint32_t& queueFamilyCount, std::vector<VkQueueFamilyProperties>& queueFamiliesProperties)
    {
        vkGetPhysicalDeviceQueueFamilyProperties(GPU, &queueFamilyCount, nullptr);
        queueFamiliesProperties.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(GPU, &queueFamilyCount, queueFamiliesProperties.data());

        //About queue family selection, queues are selected from importance, with graphics being the highest, and present being the lowest.
        for(int i = 0; i < queueFamiliesProperties.size(); i++)
        {
            if(queueFamiliesProperties.at(i).queueFlags & VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT && !queues.count(QueueType::GRAPHICS))
            {
                queues[QueueType::GRAPHICS].queueFamilyIndex = i;
                continue;
            }
            if(queueFamiliesProperties.at(i).queueFlags & VkQueueFlagBits::VK_QUEUE_COMPUTE_BIT && !queues.count(QueueType::COMPUTE))
            {
                queues[QueueType::COMPUTE].queueFamilyIndex = i;
                continue;
            }
            if(queueFamiliesProperties.at(i).queueFlags & VkQueueFlagBits::VK_QUEUE_TRANSFER_BIT && !queues.count(QueueType::TRANSFER))
            {
                queues[QueueType::TRANSFER].queueFamilyIndex = i;
                continue;
            }

            VkBool32 presentSupport;
            vkGetPhysicalDeviceSurfaceSupportKHR(GPU, i, surface, &presentSupport);
            if(presentSupport && !queues.count(QueueType::PRESENT))
            {
                queues[QueueType::PRESENT].queueFamilyIndex = i;
                continue;
            }
        }

        //now fill in any queues that need to be filled
        if(!queues.count(QueueType::GRAPHICS)) throw std::runtime_error("No suitable graphics queue family from selected GPU"); //error if no graphics
        if(!queues.count(QueueType::COMPUTE))
        {
            queues[QueueType::COMPUTE].queueFamilyIndex = queues.at(QueueType::GRAPHICS).queueFamilyIndex; //shared graphics/compute queue family
        }
        if(!queues.count(QueueType::TRANSFER))
        {
            queues[QueueType::TRANSFER].queueFamilyIndex = queues.at(QueueType::COMPUTE).queueFamilyIndex; //shared compute/transfer queue family
        }
        if(!queues.count(QueueType::PRESENT))
        {
            //loop back through until a present queue is found
            for(int i = 0; i < queueFamiliesProperties.size(); i++)
            {
                VkBool32 presentSupport;
                vkGetPhysicalDeviceSurfaceSupportKHR(GPU, i, surface, &presentSupport);
                if(presentSupport && !queues.count(QueueType::PRESENT))
                {
                    queues[QueueType::PRESENT].queueFamilyIndex = i;
                    break;
                }
            }
            if(!queues.count(QueueType::PRESENT))
            {
                throw std::runtime_error("No surface support");
            }
        }
    }

    void Device::createQueues(std::unordered_map<uint32_t, VkDeviceQueueCreateInfo>& queuesCreationInfo,
                              const std::vector<VkQueueFamilyProperties>& queueFamiliesProperties, 
                              float* queuePriority)
    {
        //Set queues creation info based on queue families which are used. Queues will be created, then distributed amongst any "queue types" sharing the same family
        for(const auto& [queueType, queuesInFamily] : queues)
        {
            if(!queuesCreationInfo.count(queuesInFamily.queueFamilyIndex))
            {
                queuesCreationInfo[queuesInFamily.queueFamilyIndex] = {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .pNext = NULL,
                    .flags = 0,
                    .queueFamilyIndex = queuesInFamily.queueFamilyIndex,
                    .queueCount = queueFamiliesProperties.at(queuesInFamily.queueFamilyIndex).queueCount,
                    .pQueuePriorities = queuePriority
                };

                //record log
                renderer.getLogger().recordLog({
                    .type = INFO,
                    .text = std::string("Using ") + std::to_string(queuesInFamily.queues.size()) + " Queues on queue family index " + std::to_string(queuesInFamily.queueFamilyIndex)
                });
            }
        }
    }

    void Device::retrieveQueues(std::unordered_map<uint32_t, VkDeviceQueueCreateInfo>& queuecreationInfo)
    {   
        //get queues
        for(auto& [familyIndex, properties] : queuecreationInfo)
        {
            familyQueues[familyIndex] = std::vector<Queue>(properties.queueCount);
            for(uint32_t i = 0; i < properties.queueCount; i++)
            {
                vkGetDeviceQueue(device, familyIndex, i, &familyQueues.at(familyIndex).at(i).queue);
            }
        }

        //fill in queues. This just gives pointers to a queue created earlier, which can be shared between different QueueType
        for(auto& [queueType, queuesInFamily] : queues)
        {
            for(uint32_t i = 0; i < familyQueues.at(queuesInFamily.queueFamilyIndex).size(); i++)
            {
                queuesInFamily.queues.push_back(&familyQueues.at(queuesInFamily.queueFamilyIndex).at(i));
            }
        }
    }

    void Device::createDevice()
    {
        //enable anisotropy
        gpuFeatures.samplerAnisotropy = VK_TRUE;

        //----------QUEUE SETUP----------//

        uint32_t queueFamilyCount;
        std::vector<VkQueueFamilyProperties> queueFamiliesProperties;
        findQueueFamilies(queueFamilyCount, queueFamiliesProperties);
        
        std::unordered_map<uint32_t, VkDeviceQueueCreateInfo> queuesCreationInfo;
        float queuePriority[16] = { 0.5f };
        createQueues(queuesCreationInfo, queueFamiliesProperties, queuePriority);

        //queues in array form
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfoVector;
        for(const auto& [index, info] : queuesCreationInfo)
        {
            queueCreateInfoVector.push_back(info);
        }

        //----------LOGICAL DEVICE CREATION----------//

        std::vector<const char*> extensionNames = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME
        };
        if(rtSupport)
        {
            //record log
            renderer.getLogger().recordLog({
                .type = INFO,
                .text = "RT supported"
            });

            //insert extension names
            extensionNames.insert(extensionNames.end(), {
                VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                VK_KHR_RAY_QUERY_EXTENSION_NAME,
                VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME
            });
        }

        //log all extension names
        for(const char* extension : extensionNames)
        {
            renderer.getLogger().recordLog({
                .type = INFO,
                .text = "Using device extension: " + std::string(extension)
            });
        }
        
        //RT features
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            .pNext = NULL,
            .accelerationStructure = VK_TRUE
        };

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR  RTfeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
            .pNext = &accelerationFeatures,
            .rayTracingPipeline = VK_TRUE
        };
        
        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
            .pNext = &RTfeatures,
            .rayQuery = VK_TRUE
        };

        VkPhysicalDeviceRayTracingMaintenance1FeaturesKHR rtMaintFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_MAINTENANCE_1_FEATURES_KHR,
            .pNext = &rayQueryFeatures,
            .rayTracingMaintenance1 = VK_TRUE
        };

        //Core features
        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamicState3Features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
            .pNext = rtSupport ? &rtMaintFeatures : NULL,
            .extendedDynamicState3RasterizationSamples = VK_TRUE
        };

        VkPhysicalDeviceVulkan11Features vulkan11Features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &dynamicState3Features
        };

        VkPhysicalDeviceVulkan12Features vulkan12Features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &vulkan11Features,
            .scalarBlockLayout = VK_TRUE,
            .timelineSemaphore = VK_TRUE,
            .bufferDeviceAddress = VK_TRUE
        };
        
        VkPhysicalDeviceVulkan13Features vulkan13Features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &vulkan12Features,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE
        };

        VkPhysicalDeviceFeatures2 features2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &vulkan13Features
        };

        vkGetPhysicalDeviceFeatures2(GPU, &features2);

        const VkDeviceCreateInfo deviceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features2,
            .flags = 0,
            .queueCreateInfoCount = (uint32_t)queueCreateInfoVector.size(),
            .pQueueCreateInfos = queueCreateInfoVector.data(),
            .enabledExtensionCount = (uint32_t)extensionNames.size(),
            .ppEnabledExtensionNames = extensionNames.data(),
            .pEnabledFeatures = NULL
        };

        VkResult result = vkCreateDevice(GPU, &deviceCreateInfo, NULL, &device);
        if(result != VK_SUCCESS)
        {
            if(result == VK_ERROR_EXTENSION_NOT_PRESENT)
            {
                //record log
                renderer.getLogger().recordLog({
                    .type = ERROR,
                    .text = "A required device extension isn't present. Terminating"
                });
            }
            throw std::runtime_error("Failed to create Vulkan device");
        }

        //volk
        volkLoadDevice(device);

        //vma
        VmaVulkanFunctions vmaFunctions{};
        vmaFunctions.vkAllocateMemory                    = vkAllocateMemory;
        vmaFunctions.vkBindBufferMemory                  = vkBindBufferMemory;
        vmaFunctions.vkBindImageMemory                   = vkBindImageMemory;
        vmaFunctions.vkCreateBuffer                      = vkCreateBuffer;
        vmaFunctions.vkCreateImage                       = vkCreateImage;
        vmaFunctions.vkDestroyBuffer                     = vkDestroyBuffer;
        vmaFunctions.vkDestroyImage                      = vkDestroyImage;
        vmaFunctions.vkFlushMappedMemoryRanges           = vkFlushMappedMemoryRanges;
        vmaFunctions.vkFreeMemory                        = vkFreeMemory;
        vmaFunctions.vkGetBufferMemoryRequirements       = vkGetBufferMemoryRequirements;
        vmaFunctions.vkGetDeviceProcAddr                 = vkGetDeviceProcAddr;
        vmaFunctions.vkGetImageMemoryRequirements        = vkGetImageMemoryRequirements;
        vmaFunctions.vkGetInstanceProcAddr               = vkGetInstanceProcAddr;
        vmaFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
        vmaFunctions.vkGetPhysicalDeviceProperties       = vkGetPhysicalDeviceProperties;
        vmaFunctions.vkInvalidateMappedMemoryRanges      = vkInvalidateMappedMemoryRanges;
        vmaFunctions.vkMapMemory                         = vkMapMemory;
        vmaFunctions.vkUnmapMemory                       = vkUnmapMemory;
        vmaFunctions.vkCmdCopyBuffer                     = vkCmdCopyBuffer;

        VmaAllocatorCreateInfo allocatorCreateInfo = {};
        allocatorCreateInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        allocatorCreateInfo.vulkanApiVersion = VK_API_VERSION_1_3;
        allocatorCreateInfo.physicalDevice = GPU;
        allocatorCreateInfo.device = device;
        allocatorCreateInfo.instance = instance;
        allocatorCreateInfo.pVulkanFunctions = &vmaFunctions;
        
        vmaCreateAllocator(&allocatorCreateInfo, &allocator);

        //queues
        retrieveQueues(queuesCreationInfo);

        //command pools init
        commands = std::make_unique<Commands>(renderer, &queues);

        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Device creation finished"
        });
    }

    QueueFamiliesIndices Device::getQueueFamiliesIndices() const
    {
        QueueFamiliesIndices queueFamiliesIndices = {};
        queueFamiliesIndices.graphicsFamilyIndex = queues.at(QueueType::GRAPHICS).queueFamilyIndex;
        queueFamiliesIndices.computeFamilyIndex = queues.at(QueueType::COMPUTE).queueFamilyIndex;
        queueFamiliesIndices.transferFamilyIndex = queues.at(QueueType::TRANSFER).queueFamilyIndex;
        queueFamiliesIndices.presentationFamilyIndex = queues.at(QueueType::PRESENT).queueFamilyIndex;

        return queueFamiliesIndices;
    }
}