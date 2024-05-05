#pragma once
#include "VulkanMemory.h"

#include <unordered_map>
#include <vector>

namespace PaperRenderer
{
    namespace PaperMemory
    {
        struct QueueFamiliesIndices
        {
            int graphicsFamilyIndex = -1;
            int computeFamilyIndex = -1;
            int transferFamilyIndex = -1;
            int presentationFamilyIndex = -1;
        };

        enum CmdPoolType
        {
            GRAPHICS = 0,
            COMPUTE = 1,
            TRANSFER = 2,
            PRESENT = 3
        };

        struct SemaphorePair
        {
            VkSemaphore semaphore;
            VkPipelineStageFlagBits2 stage;
        };

        //generic parameters for synchronization in queues
        struct SynchronizationInfo
        {
            VkQueue queue;
            std::vector<SemaphorePair> waitPairs;
            std::vector<SemaphorePair> signalPairs;
            VkFence fence;
        };

        struct CommandBuffer
        {
            VkCommandBuffer buffer;
            CmdPoolType type;
        };

        class Commands
        {
        private:
            VkDevice device;

            static QueueFamiliesIndices queueFamilyIndices;
            static std::unordered_map<CmdPoolType, VkCommandPool> commandPools;
            static const uint32_t frameCount = 2;

            void createCommandPools();

            static bool isInit;
            
        public:
            Commands(VkDevice device, const QueueFamiliesIndices& queueFamilyIndices);
            ~Commands();

            static void freeCommandBuffer(VkDevice device, CommandBuffer& commandBuffer);
            static void submitToQueue(VkDevice device, const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers);
            static uint32_t getFrameCount() { return frameCount; }

            static VkSemaphore getSemaphore(VkDevice device);
            static VkFence getSignaledFence(VkDevice device);
            static VkFence getUnsignaledFence(VkDevice device);
            static VkCommandBuffer getCommandBuffer(VkDevice device, CmdPoolType type);
        };
    }
}