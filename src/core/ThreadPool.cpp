#include "core/ThreadPool.hpp"

#include <algorithm>
#include <stdexcept>

std::size_t ThreadPool::recommendedWorkerCount() {
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads == 0) return 2;
    return std::max(1U, hardwareThreads > 2 ? hardwareThreads - 2 : 1U);
}

ThreadPool::ThreadPool(std::size_t workerCount) {
    workerCount = std::max<std::size_t>(1, workerCount);
    limits_[static_cast<std::size_t>(WorkerTaskClass::General)] = workerCount;
    limits_[static_cast<std::size_t>(WorkerTaskClass::Terrain)] = std::max<std::size_t>(1, workerCount / 2);
    limits_[static_cast<std::size_t>(WorkerTaskClass::Meshing)] = workerCount > 1
        ? workerCount - limits_[static_cast<std::size_t>(WorkerTaskClass::Terrain)] : 1;
    limits_[static_cast<std::size_t>(WorkerTaskClass::Lighting)] = std::max<std::size_t>(1, workerCount / 2);
    workers_.reserve(workerCount);
    for (std::size_t index = 0; index < workerCount; ++index)
        workers_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    for (std::thread& worker : workers_)
        if (worker.joinable()) worker.join();
}

void ThreadPool::enqueue(int priority, std::function<void()> task) {
    enqueue(WorkerTaskClass::General, priority, std::move(task));
}

void ThreadPool::enqueue(WorkerTaskClass taskClass, int priority, std::function<void()> task) {
    if (!task) throw std::invalid_argument("Cannot enqueue an empty worker task");
    if (taskClass == WorkerTaskClass::Count) throw std::invalid_argument("Invalid worker task class");
    {
        std::lock_guard lock(mutex_);
        if (stopping_) throw std::runtime_error("Cannot enqueue work after ThreadPool shutdown");
        tasks_[static_cast<std::size_t>(taskClass)].push(
            WorkItem{priority, nextSequence_++, std::move(task)});
        pendingCount_.fetch_add(1, std::memory_order_relaxed);
    }
    condition_.notify_one();
}

bool ThreadPool::hasTasks() const {
    return std::any_of(tasks_.begin(), tasks_.end(), [](const TaskQueue& queue) { return !queue.empty(); });
}

bool ThreadPool::canTake(WorkerTaskClass taskClass) const {
    const std::size_t index = static_cast<std::size_t>(taskClass);
    if (tasks_[index].empty()) return false;
    if (taskClass == WorkerTaskClass::General || active_[index] < limits_[index]) return true;

    // Quotas are soft: when all other specialist lanes are empty, idle workers
    // may help the remaining lane instead of being artificially parked.
    for (std::size_t other = static_cast<std::size_t>(WorkerTaskClass::Terrain);
         other < static_cast<std::size_t>(WorkerTaskClass::Count); ++other) {
        if (other != index && !tasks_[other].empty()) return false;
    }
    return true;
}

WorkerTaskClass ThreadPool::chooseTaskClass() {
    if (canTake(WorkerTaskClass::General)) return WorkerTaskClass::General;
    constexpr std::size_t firstSpecialist = static_cast<std::size_t>(WorkerTaskClass::Terrain);
    constexpr std::size_t specialistCount =
        static_cast<std::size_t>(WorkerTaskClass::Count) - firstSpecialist;
    const std::size_t start = static_cast<std::size_t>(nextSharedClass_) - firstSpecialist;
    for (std::size_t offset = 0; offset < specialistCount; ++offset) {
        const std::size_t index = firstSpecialist + ((start + offset) % specialistCount);
        const WorkerTaskClass candidate = static_cast<WorkerTaskClass>(index);
        if (!canTake(candidate)) continue;
        nextSharedClass_ = static_cast<WorkerTaskClass>(
            firstSpecialist + ((index - firstSpecialist + 1) % specialistCount));
        return candidate;
    }
    return WorkerTaskClass::Count;
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        WorkerTaskClass taskClass = WorkerTaskClass::Count;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return (stopping_ && !hasTasks()) ||
                    canTake(WorkerTaskClass::General) ||
                    canTake(WorkerTaskClass::Terrain) ||
                    canTake(WorkerTaskClass::Meshing) ||
                    canTake(WorkerTaskClass::Lighting);
            });
            if (stopping_ && !hasTasks()) return;
            taskClass = chooseTaskClass();
            if (taskClass == WorkerTaskClass::Count) continue;
            TaskQueue& queue = tasks_[static_cast<std::size_t>(taskClass)];
            task = queue.top().task;
            queue.pop();
            ++active_[static_cast<std::size_t>(taskClass)];
        }
        try {
            task();
        } catch (...) {
            // Fire-and-forget jobs report through their result queues. Packaged
            // tasks retain exceptions in their futures, so a worker must live on.
        }
        pendingCount_.fetch_sub(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(mutex_);
            --active_[static_cast<std::size_t>(taskClass)];
        }
        condition_.notify_all();
    }
}
