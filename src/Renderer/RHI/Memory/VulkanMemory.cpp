#include "VulkanMemory.h"


namespace Renderer
{
    uint32_t DeviceAllocation::allocationCount = 0;

    DeviceAllocation::DeviceAllocation(Device * device, DeviceAllocationInfo allocationInfo)
        :devicePtr(device),
        allocationInfo(allocationInfo)
    {
        VkPhysicalDeviceMemoryProperties2 memoryProperties = getDeviceMemoryProperties();

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
        uint32_t a = devicePtr->getGPUProperties().properties.limits.maxMemoryAllocationCount;
        if(allocationCount > devicePtr->getGPUProperties().properties.limits.maxMemoryAllocationCount)
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

        VkResult result = vkAllocateMemory(devicePtr->getDevice(), &allocInfo, nullptr, &allocation);
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
        allocationCount -= 1;
    }

    VkPhysicalDeviceMemoryProperties2 DeviceAllocation::getDeviceMemoryProperties() const
    {
        VkPhysicalDeviceMemoryProperties2 memProperties = {};
        memProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        memProperties.pNext = NULL;

        vkGetPhysicalDeviceMemoryProperties2(devicePtr->getGPU(), &memProperties);

        return memProperties;
    }
}