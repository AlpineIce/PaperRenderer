#pragma once
#include "volk.h"

#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <array>
#include <thread>

namespace PaperRenderer
{
    enum QueueType
    {
        GRAPHICS = 0,
        COMPUTE = 1,
        TRANSFER = 2,
        PRESENT = 3
    };

    enum SemaphoreType
    {
        BINARY = 0,
        TIMELINE = 1
    };
    
    struct Queue
    {
        VkQueue queue = VK_NULL_HANDLE;
        std::recursive_mutex threadLock;

        void idle()
        {
            const std::lock_guard guard(threadLock);
            vkQueueWaitIdle(queue);
        }
    };

    struct QueuesInFamily
    {
        uint32_t queueFamilyIndex = 0xFFFFFFFF;
        std::vector<Queue*> queues;
    };

    struct QueueFamiliesIndices
    {
        int graphicsFamilyIndex = -1;
        int computeFamilyIndex = -1;
        int transferFamilyIndex = -1;
        int presentationFamilyIndex = -1;
    };

    struct BinarySemaphorePair
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkPipelineStageFlagBits2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    };

    struct TimelineSemaphorePair
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkPipelineStageFlagBits2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        uint64_t value = 0;
    };

    //generic parameters for synchronization in queues
    struct SynchronizationInfo
    {
        std::vector<BinarySemaphorePair> binaryWaitPairs = {};
        std::vector<BinarySemaphorePair> binarySignalPairs = {};
        std::vector<TimelineSemaphorePair> timelineWaitPairs = {};
        std::vector<TimelineSemaphorePair> timelineSignalPairs = {};
        VkFence fence = VK_NULL_HANDLE;
    };

    class Commands
    {
    private:
        struct CommandPoolData
        {
            VkCommandPool cmdPool = VK_NULL_HANDLE;
            std::recursive_mutex threadLock = {};
            std::deque<VkCommandBuffer> cmdBuffers = {};
            uint32_t cmdBufferStackLocation = 0;
        };
        std::mutex cmdBuffersLockedPoolMutex;
        uint64_t lockedCmdBufferCount = 0; //protected by mutex
        std::unordered_map<QueueType, QueuesInFamily>* queuesPtr;
        std::array<std::unordered_map<QueueType, std::vector<CommandPoolData>>, 2> commandPools;
        std::unordered_map<VkCommandBuffer, CommandPoolData*> cmdBuffersLockedPool;
        const uint32_t coreCount = std::thread::hardware_concurrency();

        void createCommandPools();

        class RenderEngine& renderer;
        
    public:
        Commands(class RenderEngine& renderer, std::unordered_map<QueueType, QueuesInFamily>* queuesPtr);
        ~Commands();
        Commands(const Commands&) = delete;

        void resetCommandPools();
        Queue& submitToQueue(const QueueType queueType, const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers);
        void submitToQueue(Queue& queue, const SynchronizationInfo &synchronizationInfo, const std::vector<VkCommandBuffer> &commandBuffers);

        VkSemaphore getSemaphore();
        VkSemaphore getTimelineSemaphore(uint64_t initialValue);
        VkFence getSignaledFence();
        VkFence getUnsignaledFence();
        
        //MUST CALL unlockCommandBuffer() BEFORE THREAD SUBMISSION, AND ON SAME THREAD
        VkCommandBuffer getCommandBuffer(QueueType type);

        //MUST BE CALLED ON SAME THREAD getCommandBuffer() WAS CALLED ON
        void unlockCommandBuffer(VkCommandBuffer cmdBuffer);
    };
}