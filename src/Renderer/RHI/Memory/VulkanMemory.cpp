#include "VulkanMemory.h"
#define VOLK_IMPLEMENTATION
#define VK_NO_PROTOTYPES
#include "volk.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE 
#include "glm/glm.hpp"

#include <stdexcept>

namespace PaperRenderer
{
    namespace PaperMemory
    {
        std::atomic<int> DeviceAllocation::allocationCount = 0;

        DeviceAllocation::DeviceAllocation(VkDevice device, VkPhysicalDevice gpu, DeviceAllocationInfo allocationInfo)
            : device(device),
            gpu(gpu),
            allocationInfo(allocationInfo),
            currentOffset(0),
            allocationSize(allocationInfo.allocationSize)
        {
            VkPhysicalDeviceMemoryProperties2 memoryProperties = {};
            memoryProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
            memoryProperties.pNext = NULL;

            vkGetPhysicalDeviceMemoryProperties2(gpu, &memoryProperties);

            //retrieve a fitting heap to use
            int heapIndex = -1;
            int memoryTypeIndex = -1;
            for(int i = 0; i < memoryProperties.memoryProperties.memoryTypeCount; i++)
            {
                VkMemoryType memType = memoryProperties.memoryProperties.memoryTypes[i];
                if(memType.propertyFlags = allocationInfo.memoryProperties)
                {
                    this->memoryType = memType;
                    heapIndex = memType.heapIndex;
                    memoryTypeIndex = i;
                    
                    break;
                }
            }

            //check if a heap was found
            if(heapIndex == -1)
            {
                throw std::runtime_error("Couldn't find valid heap with selected memory properties or size");
            }

            //safety for creating too many allocations
            allocationCount += 1;

            //grab GPU properties
            VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtPipelineProperties = {};
            rtPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            rtPipelineProperties.pNext = NULL;

            VkPhysicalDeviceProperties2 deviceProperties = {};
            deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            deviceProperties.pNext = &rtPipelineProperties;

            vkGetPhysicalDeviceProperties2(gpu, &deviceProperties);

            //check allocation limit
            if(allocationCount > deviceProperties.properties.limits.maxMemoryAllocationCount)
            {
                throw std::runtime_error("Memory allocation limit exceeded");
                return;
            }

            //create allocation and check result
            VkMemoryAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.pNext = NULL;
            allocInfo.allocationSize = allocationInfo.allocationSize;
            allocInfo.memoryTypeIndex = memoryTypeIndex;

            VkResult result = vkAllocateMemory(device, &allocInfo, nullptr, &allocation);
            if(result != VK_SUCCESS)
            {
                if(result == VK_ERROR_OUT_OF_DEVICE_MEMORY || VK_ERROR_OUT_OF_HOST_MEMORY)
                {
                    throw std::runtime_error("Memory allocation failed, out of memory");
                }
                else if(result == VK_ERROR_TOO_MANY_OBJECTS)
                {
                    throw std::runtime_error("Memory allocation failed, too many allocations");
                }
                else
                {
                    throw std::runtime_error("Memory allocation failed");
                }
            }
        }

        DeviceAllocation::~DeviceAllocation()
        {
            vkFreeMemory(device, allocation, nullptr);
            
            allocationCount -= 1;
        }

        bool DeviceAllocation::verifyAvaliableMemory(VkDeviceSize bindSize)
        {
            if(allocationSize >= bindSize + currentOffset)
            {
                return true;
            }
            return false;
        }

        ResourceBindingInfo DeviceAllocation::bindBuffer(VkBuffer buffer, VkMemoryRequirements memoryRequirements)
        {

            VkDeviceSize bindSize = ((memoryRequirements.size - memoryRequirements.size % memoryRequirements.alignment) + memoryRequirements.alignment);
            ResourceBindingInfo resourceBindingInfo = {
                .allocationLocation = currentOffset,
                .allocatedSize = bindSize
            };

            //check if there is available space in the allocation
            if(verifyAvaliableMemory(bindSize))
            {
                VkBindBufferMemoryInfo bindingInfo = {};
                bindingInfo.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
                bindingInfo.pNext = NULL;
                bindingInfo.buffer = buffer;
                bindingInfo.memory = allocation;
                bindingInfo.memoryOffset = currentOffset;

                if(vkBindBufferMemory2(device, 1, &bindingInfo) != VK_SUCCESS)
                {
                    //returns null location and 0 size
                    resourceBindingInfo.allocationLocation = 0;
                    resourceBindingInfo.allocatedSize = 0;

                    return resourceBindingInfo;
                }

                //move "memory pointer"
                currentOffset += bindSize;
            }
            else
            {
                //returns null location and 0 size
                resourceBindingInfo.allocationLocation = 0;
                resourceBindingInfo.allocatedSize = 0;
            }

            return resourceBindingInfo;
        }

        ResourceBindingInfo DeviceAllocation::bindImage(VkImage image, VkMemoryRequirements memoryRequirements)
        {
            VkDeviceSize bindSize = ((memoryRequirements.size - memoryRequirements.size % memoryRequirements.alignment) + memoryRequirements.alignment);
            ResourceBindingInfo resourceBindingInfo = {
                .allocationLocation = currentOffset,
                .allocatedSize = bindSize
            };
            //check if there is available space in the allocation
            if(verifyAvaliableMemory(bindSize))
            {
                VkBindImageMemoryInfo bindingInfo;
                bindingInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
                bindingInfo.pNext = NULL;
                bindingInfo.image = image;
                bindingInfo.memory = allocation;
                bindingInfo.memoryOffset = currentOffset;

                vkBindImageMemory2(device, 1, &bindingInfo);

                //move "memory pointer"
                currentOffset += bindSize;

                return resourceBindingInfo;
            }
            else
            {
                throw std::runtime_error("New image binding will exceed available memory in allocation");
            }
        }
    }
}