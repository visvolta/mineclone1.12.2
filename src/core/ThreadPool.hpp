#pragma once

#include <atomic>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// Small priority-aware worker pool shared by terrain, lighting, and CPU mesh jobs.
// OpenGL work is deliberately excluded because contexts are main-thread owned.
enum class WorkerTaskClass : std::size_t {
    General = 0,
    Terrain = 1,
    Meshing = 2,
    Lighting = 3,
    Count = 4
};

class ThreadPool {
public:
    explicit ThreadPool(std::size_t workerCount = recommendedWorkerCount());
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    [[nodiscard]] static std::size_t recommendedWorkerCount();
    [[nodiscard]] std::size_t workerCount() const { return workers_.size(); }
    [[nodiscard]] std::size_t pendingCount() const { return pendingCount_.load(std::memory_order_relaxed); }

    void enqueue(int priority, std::function<void()> task);
    void enqueue(WorkerTaskClass taskClass, int priority, std::function<void()> task);

    template <typename Function>
    auto submit(int priority, Function&& function) -> std::future<std::invoke_result_t<Function>> {
        return submit(WorkerTaskClass::General, priority, std::forward<Function>(function));
    }

    template <typename Function>
    auto submit(WorkerTaskClass taskClass, int priority, Function&& function)
        -> std::future<std::invoke_result_t<Function>> {
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
        std::future<Result> future = task->get_future();
        enqueue(taskClass, priority, [task] { (*task)(); });
        return future;
    }

private:
    struct WorkItem {
        int priority = 0;
        std::uint64_t sequence = 0;
        std::function<void()> task;
    };

    struct LowerPriority {
        bool operator()(const WorkItem& left, const WorkItem& right) const {
            if (left.priority != right.priority) return left.priority < right.priority;
            return left.sequence > right.sequence;
        }
    };

    using TaskQueue = std::priority_queue<WorkItem, std::vector<WorkItem>, LowerPriority>;

    [[nodiscard]] bool hasTasks() const;
    [[nodiscard]] bool canTake(WorkerTaskClass taskClass) const;
    [[nodiscard]] WorkerTaskClass chooseTaskClass();
    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::array<TaskQueue, static_cast<std::size_t>(WorkerTaskClass::Count)> tasks_;
    std::array<std::size_t, static_cast<std::size_t>(WorkerTaskClass::Count)> active_{};
    std::array<std::size_t, static_cast<std::size_t>(WorkerTaskClass::Count)> limits_{};
    std::vector<std::thread> workers_;
    std::atomic_size_t pendingCount_{0};
    std::uint64_t nextSequence_ = 0;
    WorkerTaskClass nextSharedClass_ = WorkerTaskClass::Terrain;
    bool stopping_ = false;
};
