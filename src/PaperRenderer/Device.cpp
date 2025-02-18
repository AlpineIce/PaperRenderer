#include "Device.h"
#include "PaperRenderer.h"

#include <unordered_map>
#include <functional>

namespace PaperRenderer
{
    Device::Device(RenderEngine& renderer, const DeviceInstanceInfo& instanceInfo)
        :devicepNext(instanceInfo.devicepNext),
        renderer(renderer)
    {
        if(volkInitialize() != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = CRITICAL_ERROR,
                .text = "Failed to initialize Volk (vulkan function loader)"
            });
        }
        glfwInit();
        createContext(instanceInfo);
        findGPU(instanceInfo.extraDeviceExtensions);
    }

    Device::~Device()
    {
        commands.reset();
        vmaDestroyAllocator(allocator);
        if(debugUtilsMessenger) vkDestroyDebugUtilsMessengerEXT(instance, debugUtilsMessenger, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
        
        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "Device destructor initialized"
        });
    }

    void Device::createContext(const DeviceInstanceInfo& instanceData)
    {
        //----------INSTANCE CREATION----------//

        //reserved extensions
        std::vector<const char*> extensionNames = {
            VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
            VK_EXT_DEBUG_UTILS_EXTENSION_NAME
        };

        //glfw extensions
        unsigned int glfwExtensionCount = 0;
        glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector<const char*> glfwExtensions(glfwExtensionCount);
        for(int i = 0; i < glfwExtensionCount; i++)
        {
            glfwExtensions[i] = glfwGetRequiredInstanceExtensions(&glfwExtensionCount)[i];
        }

        //insert glfw and extra extensions
        extensionNames.insert(extensionNames.end(), glfwExtensions.begin(), glfwExtensions.end());
        extensionNames.insert(extensionNames.end(), instanceData.extraInstanceExtensions.begin(), instanceData.extraInstanceExtensions.end());

        //log all extension names
        for(const char* extension : extensionNames)
        {
            renderer.getLogger().recordLog({
                .type = INFO,
                .text = "Using instance extension: " + std::string(extension)
            });
        }
        
        //layers
        std::vector<const char*> layerNames = {};
        
        //application info
        VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = NULL,
            .pApplicationName = instanceData.appName.c_str(),
            .applicationVersion = instanceData.appVersion,
            .pEngineName = instanceData.engineName.c_str(),
            .engineVersion = instanceData.engineVersion
        };
        vkEnumerateInstanceVersion(&appInfo.apiVersion);

        //instance info
        const VkInstanceCreateInfo instanceInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .pApplicationInfo = &appInfo,
            .enabledLayerCount = (uint32_t)layerNames.size(),
            .ppEnabledLayerNames = layerNames.data(),
            .enabledExtensionCount = (uint32_t)extensionNames.size(),
            .ppEnabledExtensionNames = extensionNames.data()
        };

        //instance creation
        VkResult result = vkCreateInstance(&instanceInfo, nullptr, &instance);
        if(result != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = CRITICAL_ERROR,
                .text = "Failed to create Vulkan instance"
            });
        }
        volkLoadInstance(instance);
    }

    void Device::findGPU(std::vector<const char*> extensions)
    {
        //get list of physical devices
        uint32_t deviceCount;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());
        
        DeviceFeaturesAndProperties tempFeaturesAndProperties = {};

        bool deviceFound = false;
        for(VkPhysicalDevice physicalDevice : physicalDevices)
        {
            //get physical device properties
            VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
                .pNext = NULL
            };
            VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
                .pNext = &asProperties
            };
            VkPhysicalDeviceProperties2 properties = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = &rtPipelineProperties
            };
            vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
            
            //get physical device features
            VkPhysicalDeviceFeatures2 features = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                .pNext = NULL
            };
            vkGetPhysicalDeviceFeatures2(physicalDevice, &features);
            
            //get all available extensions
            uint32_t extensionCount;
            vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, NULL);
            std::vector<VkExtensionProperties> extensionProperties(extensionCount);
            vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &extensionCount, extensionProperties.data());

            //required extensions
            //extensions enabled by default
            bool hasSwapchain = false;
            bool hasDynamicState3 = false;
            extensions.insert(extensions.end(), {
                VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME
            });

            //rt extensions
            bool hasDeferredOps = false;
            bool hasAccelStructure = false;
            bool hasRTPipeline = false;
            bool hasRayQuery = false;
            bool hasMaintFeatures = false;
            extensions.insert(extensions.end(), {
                VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                VK_KHR_RAY_QUERY_EXTENSION_NAME,
                VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME
            });

            //check extensions
            std::set<const char*> enabledExtensions = {};
            for(const VkExtensionProperties& properties : extensionProperties)
            {
                for(const char* extensionName : extensions)
                {
                    if(std::string(properties.extensionName).find(extensionName) != std::string::npos)
                    {
                        enabledExtensions.insert(extensionName);
                    }
                }

                //required extensions
                hasSwapchain = hasSwapchain || std::string(properties.extensionName).find(VK_KHR_SWAPCHAIN_EXTENSION_NAME) != std::string::npos;
                hasDynamicState3 = hasDynamicState3 || std::string(properties.extensionName).find(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME) != std::string::npos;
                
                //required extensions for RT
                hasDeferredOps = hasDeferredOps || std::string(properties.extensionName).find(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) != std::string::npos;
                hasAccelStructure = hasAccelStructure || std::string(properties.extensionName).find(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) != std::string::npos;
                hasRTPipeline = hasRTPipeline || std::string(properties.extensionName).find(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) != std::string::npos;
                hasRayQuery = hasRayQuery || std::string(properties.extensionName).find(VK_KHR_RAY_QUERY_EXTENSION_NAME) != std::string::npos;
                hasMaintFeatures = hasMaintFeatures || std::string(properties.extensionName).find(VK_KHR_RAY_TRACING_MAINTENANCE_1_EXTENSION_NAME) != std::string::npos;
            }

            const auto setGPUData = [&]()
            {
                //set found "flag"
                deviceFound = true;

                //set GPU handle
                GPU = physicalDevice;

                //set features and properties struct
                featuresAndProperties = {
                    .gpuFeatures = features,
                    .gpuProperties = properties,
                    .asProperties = asProperties,
                    .rtPipelineProperties = rtPipelineProperties,
                    .enabledExtensions = std::vector<const char*>(enabledExtensions.begin(), enabledExtensions.end()),
                    .rtSupport = hasDeferredOps && hasAccelStructure && hasRTPipeline && hasRayQuery && hasMaintFeatures
                };
            };
            
            //handle break or not
            if(properties.properties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && hasSwapchain && hasDynamicState3)
            {
                setGPUData();

                break; //break prefers discrete gpu over other gpu types
            }
            else if(hasSwapchain && hasDynamicState3)
            {
                setGPUData();
            }
        }

        //log warning if requested extension sizes dont match
        if(featuresAndProperties.enabledExtensions.size() != extensions.size())
        {
            renderer.getLogger().recordLog({
                .type = WARNING,
                .text = "Not all requested extensions were found"
            });
        }

        //log error if no suitable GPU was found
        if(!deviceFound)
        {
            renderer.getLogger().recordLog({
                .type = CRITICAL_ERROR,
                .text = "Couldn't find suitable GPU"
            });
        }

        //record log
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = std::string("Using GPU: ") + featuresAndProperties.gpuProperties.properties.deviceName
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
            if(queueFamiliesProperties[i].queueFlags & VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT && !queues.count(QueueType::GRAPHICS))
            {
                queues[QueueType::GRAPHICS].queueFamilyIndex = i;
                continue;
            }
            if(queueFamiliesProperties[i].queueFlags & VkQueueFlagBits::VK_QUEUE_COMPUTE_BIT && !queues.count(QueueType::COMPUTE))
            {
                queues[QueueType::COMPUTE].queueFamilyIndex = i;
                continue;
            }
            if(queueFamiliesProperties[i].queueFlags & VkQueueFlagBits::VK_QUEUE_TRANSFER_BIT && !queues.count(QueueType::TRANSFER))
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
        if(!queues.count(QueueType::GRAPHICS))
        {
            renderer.getLogger().recordLog({
                .type = CRITICAL_ERROR,
                .text = "No suitable graphics queue family from selected GPU"
            });
            throw std::runtime_error("No suitable graphics queue family from selected GPU"); //error if no graphics
        }
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

            const auto queueTypeToString = [](QueueType type) -> std::string
            {
                switch(type)
                {
                    case GRAPHICS:
                        return "Graphics ";
                    case COMPUTE:
                        return "Compute ";
                    case TRANSFER:
                        return "Transfer ";
                    case PRESENT:
                        return "Present ";
                }
                return "";
            };
            
            //record log
            renderer.getLogger().recordLog({
                .type = INFO,
                .text = queueTypeToString(queueType) + std::string("queue group using ") + std::to_string(queuesInFamily.queues.size()) + " Queues on queue family index " + std::to_string(queuesInFamily.queueFamilyIndex)
            });
        }
    }

    VkBool32 Device::debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
    {
        //get renderer handle from pUserData
        RenderEngine& renderer = *((RenderEngine*)pUserData);

        //get message
        std::string debugMessage = std::to_string(pCallbackData->messageIdNumber) + pCallbackData->pMessageIdName + pCallbackData->pMessage;

        //get log type
        LogType type;
        if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
        {
            type = INFO;
        }
        else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            type = WARNING;
        }
        else if(messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        {
            type = CRITICAL_ERROR;
        }

        //log error
        renderer.getLogger().recordLog({
            .type = type,
            .text = debugMessage
        });

        return VK_FALSE;
    }

    void Device::createDevice()
    {
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

        //log RT support
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = featuresAndProperties.rtSupport ? "RT supported" : "RT not supported"
        });

        //log all extension names
        for(const char* extension : featuresAndProperties.enabledExtensions)
        {
            renderer.getLogger().recordLog({
                .type = INFO,
                .text = "Using device extension: " + std::string(extension)
            });
        }
        
        //RT features
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            .pNext = devicepNext,
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

        VkPhysicalDeviceRayTracingValidationFeaturesNV validationFeatures = { //NVIDIA only, for RT validation
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_VALIDATION_FEATURES_NV,
            .pNext = &rtMaintFeatures
        };

        //Core features
        VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dynamicState3Features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
            .pNext = featuresAndProperties.rtSupport ? &rtMaintFeatures : devicepNext,
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
            .dynamicRendering = VK_TRUE,
            .maintenance4 = VK_TRUE
        };

        VkPhysicalDeviceVulkan14Features vulkan14Features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
            .pNext = &vulkan13Features,
            .maintenance5 = VK_TRUE,
            .maintenance6 = VK_TRUE
        };

        VkPhysicalDeviceFeatures2 features2 = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &vulkan14Features
        };

        vkGetPhysicalDeviceFeatures2(GPU, &features2);

        const VkDeviceCreateInfo deviceCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features2,
            .flags = 0,
            .queueCreateInfoCount = (uint32_t)queueCreateInfoVector.size(),
            .pQueueCreateInfos = queueCreateInfoVector.data(),
            .enabledExtensionCount = (uint32_t)featuresAndProperties.enabledExtensions.size(),
            .ppEnabledExtensionNames = featuresAndProperties.enabledExtensions.data(),
            .pEnabledFeatures = NULL
        };

        VkResult result = vkCreateDevice(GPU, &deviceCreateInfo, NULL, &device);
        if(result != VK_SUCCESS)
        {
            if(result == VK_ERROR_EXTENSION_NOT_PRESENT)
            {
                renderer.getLogger().recordLog({
                    .type = WARNING,
                    .text = "One or more device extensions aren't present"
                });
            }
            else
            {
                renderer.getLogger().recordLog({
                    .type = CRITICAL_ERROR,
                    .text = "Device creation returned an error that's probably unrelated to missing extensions"
                });
            }
        }

        //create validation stuff
        const VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerInfo = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = NULL,
            .flags = 0,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugUtilsMessengerCallback,
            .pUserData = &renderer
        };

        result = vkCreateDebugUtilsMessengerEXT(instance, &debugUtilsMessengerInfo, nullptr, &debugUtilsMessenger);
        if(result != VK_SUCCESS)
        {
            renderer.getLogger().recordLog({
                .type = WARNING,
                .text = "Failed to create debug messenger"
            });
        }

        //volk
        volkLoadDevice(device);

        //vma
        const VmaVulkanFunctions vmaFunctions = {
            .vkGetInstanceProcAddr               = vkGetInstanceProcAddr,
            .vkGetDeviceProcAddr                 = vkGetDeviceProcAddr,
            .vkGetPhysicalDeviceProperties       = vkGetPhysicalDeviceProperties,
            .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
            .vkAllocateMemory                    = vkAllocateMemory,
            .vkFreeMemory                        = vkFreeMemory,
            .vkMapMemory                         = vkMapMemory,
            .vkUnmapMemory                       = vkUnmapMemory,
            .vkFlushMappedMemoryRanges           = vkFlushMappedMemoryRanges,
            .vkInvalidateMappedMemoryRanges      = vkInvalidateMappedMemoryRanges,
            .vkBindBufferMemory                  = vkBindBufferMemory,
            .vkBindImageMemory                   = vkBindImageMemory,
            .vkGetBufferMemoryRequirements       = vkGetBufferMemoryRequirements,
            .vkGetImageMemoryRequirements        = vkGetImageMemoryRequirements,
            .vkCreateBuffer                      = vkCreateBuffer,
            .vkDestroyBuffer                     = vkDestroyBuffer,
            .vkCreateImage                       = vkCreateImage,
            .vkDestroyImage                      = vkDestroyImage,
            .vkCmdCopyBuffer                     = vkCmdCopyBuffer
        };

        const VmaAllocatorCreateFlags vmaFlags = 
            VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT |
            VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT |
            VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
            VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT |
            VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;

        const VmaAllocatorCreateInfo allocatorCreateInfo = {
            .flags = vmaFlags,
            .physicalDevice = GPU,
            .device = device,
            //.preferredLargeHeapBlockSize
            .pAllocationCallbacks = nullptr,
            .pDeviceMemoryCallbacks = nullptr,
            .pHeapSizeLimit = nullptr,
            .pVulkanFunctions = &vmaFunctions,
            .instance = instance,
            .vulkanApiVersion = VK_API_VERSION_1_3
        };
        
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
        return {
            .graphicsFamilyIndex = (int)queues.at(QueueType::GRAPHICS).queueFamilyIndex,
            .computeFamilyIndex = (int)queues.at(QueueType::COMPUTE).queueFamilyIndex,
            .transferFamilyIndex = (int)queues.at(QueueType::TRANSFER).queueFamilyIndex,
            .presentationFamilyIndex = (int)queues.at(QueueType::PRESENT).queueFamilyIndex
        };
    }
}