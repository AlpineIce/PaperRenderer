#pragma once
#define VK_NO_PROTOTYPES
#include "Volk/volk.h"

#include <atomic>

namespace PaperRenderer
{
    namespace PaperMemory
    {
        //struct for allocation constructor parameters
        struct DeviceAllocationInfo
        {
            VkDeviceSize allocationSize = 0;
            VkMemoryPropertyFlags memoryProperties;
            VkMemoryAllocateFlags allocFlags;
        };

        //return struct for binding functions
        struct ResourceBindingInfo
        {
            VkDeviceSize allocationLocation;
            VkDeviceSize allocatedSize = 0;
        };

        //class for a vulkan memory allocation
        class DeviceAllocation
        {
        private:
            DeviceAllocationInfo allocationInfo;
            VkDeviceMemory allocation;
            VkMemoryType memoryType;
            VkDeviceSize currentOffset;
            const VkDeviceSize allocationSize;
            bool mapped = false;
            bool needsFlush = true;
            void* mappedData = NULL;

            //TODO LIST OF MEMORY FRAGMENTS

            static std::atomic<int> allocationCount;

            VkDevice device;
            VkPhysicalDevice gpu;

            //check if a new binding will "overflow" from the allocation. returns true if there is suitable available memory
            bool verifyAvaliableMemory(VkDeviceSize bindSize, VkDeviceSize alignment);

        public:
            DeviceAllocation(VkDevice device, VkPhysicalDevice gpu, const DeviceAllocationInfo& allocationInfo);
            ~DeviceAllocation();

            static VkDeviceSize padToMultiple(VkDeviceSize startingSize, VkDeviceSize multiple);

            ResourceBindingInfo bindBuffer(VkBuffer buffer, VkMemoryRequirements memoryRequirements);
            ResourceBindingInfo bindImage(VkImage image, VkMemoryRequirements memoryRequirements);
            
            const VkDeviceMemory& getAllocation() const { return allocation; }
            const VkMemoryType& getMemoryType() const { return memoryType; }
            void* getMappedPtr() const { return mappedData; } //returns null if unmapped
            const bool& getFlushRequirement() const { return needsFlush; }

            VkDeviceSize getMemorySize() const { return allocationSize; }
            VkDeviceSize getAvailableMemorySize() const { return allocationSize - currentOffset; }
        };
    }
}