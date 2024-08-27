#include "VulkanMemory.h"
#define VK_NO_PROTOTYPES
#include "volk.h"
#define VMA_IMPLEMENTATION
#define VMA_VULKAN_VERSION 1003000
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vk_mem_alloc.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE 
#include "glm/glm.hpp"

#include <stdexcept>

namespace PaperRenderer
{
    std::atomic<int> DeviceAllocation::allocationCount = 0;

    DeviceAllocation::DeviceAllocation(VkDevice device, VkPhysicalDevice gpu, const DeviceAllocationInfo& allocationInfo)
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
            if(memType.propertyFlags & allocationInfo.memoryProperties)
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
        VkPhysicalDeviceProperties2 deviceProperties = {};
        deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties.pNext = NULL;

        vkGetPhysicalDeviceProperties2(gpu, &deviceProperties);

        //check allocation limit
        if(allocationCount > deviceProperties.properties.limits.maxMemoryAllocationCount)
        {
            throw std::runtime_error("Memory allocation limit exceeded");
            return;
        }

        //create allocation and check result
        VkMemoryAllocateFlagsInfo allocFlags = {};
        allocFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlags.pNext = NULL;
        allocFlags.flags = allocationInfo.allocFlags;
        allocFlags.deviceMask = 0;
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = &allocFlags;
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

        //map memory if host visible
        if(memoryType.propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
        {
            mapped = true;
            if(memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) //vkFlushMappedMemoryRanges and vkInvalidateMappedMemoryRanges not needed
            {
                needsFlush = false;
            }
            vkMapMemory(device, allocation, 0, allocationSize, 0, &mappedData);
        }
    }

    DeviceAllocation::~DeviceAllocation()
    {
        if(mapped)
        {
            vkUnmapMemory(device, allocation);
        }
        
        vkFreeMemory(device, allocation, nullptr);
        
        allocationCount -= 1;
    }

    VkDeviceSize DeviceAllocation::padToMultiple(VkDeviceSize startingSize, VkDeviceSize multiple)
    {
        //from https://stackoverflow.com/questions/3407012/rounding-up-to-the-nearest-multiple-of-a-number assumes startingSize is always positive and a power of 2
        multiple = std::max(multiple, (VkDeviceSize)1);
        return (startingSize + multiple - 1) & -multiple;
    }

    bool DeviceAllocation::verifyAvaliableMemory(VkDeviceSize bindSize, VkDeviceSize alignment)
    {
        if(allocationSize >= bindSize + padToMultiple(currentOffset, alignment))
        {
            return true;
        }
        return false;
    }

    ResourceBindingInfo DeviceAllocation::bindBuffer(VkBuffer buffer, VkMemoryRequirements memoryRequirements)
    {
        ResourceBindingInfo resourceBindingInfo = {
            .allocationLocation = padToMultiple(currentOffset, memoryRequirements.alignment),
            .allocatedSize = memoryRequirements.size
        };

        //check if there is available space in the allocation
        if(verifyAvaliableMemory(memoryRequirements.size, memoryRequirements.alignment))
        {
            VkBindBufferMemoryInfo bindingInfo = {};
            bindingInfo.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
            bindingInfo.pNext = NULL;
            bindingInfo.buffer = buffer;
            bindingInfo.memory = allocation;
            bindingInfo.memoryOffset = resourceBindingInfo.allocationLocation;

            if(vkBindBufferMemory2(device, 1, &bindingInfo) != VK_SUCCESS)
            {
                //returns null location and 0 size
                resourceBindingInfo.allocationLocation = 0;
                resourceBindingInfo.allocatedSize = 0;

                return resourceBindingInfo;
            }

            //move "memory pointer"
            currentOffset += memoryRequirements.size;
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
        ResourceBindingInfo resourceBindingInfo = {
            .allocationLocation = padToMultiple(currentOffset, memoryRequirements.alignment),
            .allocatedSize = memoryRequirements.size
        };
        //check if there is available space in the allocation
        if(verifyAvaliableMemory(memoryRequirements.size, memoryRequirements.alignment))
        {
            VkBindImageMemoryInfo bindingInfo;
            bindingInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
            bindingInfo.pNext = NULL;
            bindingInfo.image = image;
            bindingInfo.memory = allocation;
            bindingInfo.memoryOffset = resourceBindingInfo.allocationLocation;

            vkBindImageMemory2(device, 1, &bindingInfo);

            //move "memory pointer"
            currentOffset += memoryRequirements.size;

            return resourceBindingInfo;
        }
        else
        {
            throw std::runtime_error("New image binding will exceed available memory in allocation");
        }
    }
}