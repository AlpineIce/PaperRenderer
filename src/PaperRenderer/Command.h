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

    // Asynchronous command pool which can be used to perform operations that extend beyond the scope of a single frame. As an example, you could use 
    // this for offline rendering while the main rendering context previews it (be careful with windows auto killing queue submissions that take too long).
    // To allocate a command buffer, pass the pool as a parameter into CommandBuffer's constructor. 
    //** NOT THREAD SAFE **
    class CommandPool
    {
    private:
        VkCommandPool cmdPool = VK_NULL_HANDLE;
        std::deque<VkCommandBuffer> cmdBuffers = {};
        uint32_t cmdBufferStackLocation = 0;

        VkCommandBuffer getCommandBuffer();

        class RenderEngine* rendererPtr;

        friend class CommandBuffer;

    public:
        CommandPool(class RenderEngine& renderer, const QueueType type);
        CommandPool(const CommandPool&) = delete;
        CommandPool(CommandPool&& other) noexcept;
        CommandPool& operator=(CommandPool&& other) noexcept;
        ~CommandPool();

        void reset();
    };

    // RAII command buffer wrapper; automatically unlocks command buffer at the end of its scope
    class CommandBuffer
    {
    private:
        VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;

        class Commands* commands;
    public:
        // Create command buffer with single frame lifetime (use this most of the time for frame synchronous operations)
        CommandBuffer(Commands& commands, const QueueType type);
        // Create command buffer with lifetime of custom pool (use this to perform asynchronous to frame rendering operations)
        CommandBuffer(CommandPool& pool);
        ~CommandBuffer();
        CommandBuffer(const CommandBuffer&) = delete;
        CommandBuffer(CommandBuffer&& other) noexcept;
        CommandBuffer& operator=(CommandBuffer&& other) noexcept;
        
        operator VkCommandBuffer() const { return cmdBuffer; }
        VkCommandBuffer getCommandBuffer() const { return cmdBuffer; }
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

        VkCommandBuffer getCommandBuffer(QueueType type);
        void unlockCommandBuffer(VkCommandBuffer cmdBuffer);

        class RenderEngine& renderer;

        friend CommandBuffer;
        
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
    };
}