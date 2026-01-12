#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>

namespace elink
{
    /**
     * Thread pool for handling WebSocket requests asynchronously
     */
    class ThreadPool
    {
    public:
        /**
         * Rejection policy when queue is full
         */
        enum class RejectionPolicy
        {
            BLOCK,          // Block until queue has space (default)
            DISCARD_OLDEST, // Discard the oldest task
            DISCARD_NEWEST, // Discard the new task
            THROW_EXCEPTION // Throw exception
        };

        /**
         * Constructor
         * @param numThreads Number of worker threads (default: hardware concurrency)
         * @param maxQueueSize Maximum queue size (0 = unlimited)
         * @param policy Rejection policy when queue is full
         */
        explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency(),
                            size_t maxQueueSize = 1000,
                            RejectionPolicy policy = RejectionPolicy::DISCARD_OLDEST)
            : m_stop(false), m_maxQueueSize(maxQueueSize), m_rejectionPolicy(policy)
        {
            if (numThreads == 0)
            {
                numThreads = 1;
            }

            for (size_t i = 0; i < numThreads; ++i)
            {
                m_workers.emplace_back([this]
                                       {
                    while (true)
                    {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(m_queueMutex);
                            m_condition.wait(lock, [this] { 
                                return m_stop || !m_tasks.empty(); 
                            });

                            if (m_stop && m_tasks.empty())
                            {
                                return;
                            }

                            task = std::move(m_tasks.front());
                            m_tasks.pop();
                        }

                        try
                        {
                            task();
                        }
                        catch (const std::exception& e)
                        {
                            // Log error but don't crash the thread
                            // ELEGOO_LOG_ERROR("Thread pool task error: {}", e.what());
                        }
                        catch (...)
                        {
                            // Catch all other exceptions
                        }
                    } });
            }
        }

        /**
         * Destructor - waits for all tasks to complete
         */
        ~ThreadPool()
        {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_stop = true;
            }

            m_condition.notify_all();

            for (auto &worker : m_workers)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }
        }

        /**
         * Enqueue a task for execution
         * @param f Function to execute
         * @param args Arguments to pass to the function
         * @return Future for the result (may be invalid if task was rejected)
         */
        template <typename F, typename... Args>
        auto enqueue(F &&f, Args &&...args)
            -> std::future<typename std::invoke_result<F, Args...>::type>
        {
            using return_type = typename std::invoke_result<F, Args...>::type;

            auto task = std::make_shared<std::packaged_task<return_type()>>(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...));

            std::future<return_type> result = task->get_future();
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);

                if (m_stop)
                {
                    throw std::runtime_error("Cannot enqueue on stopped ThreadPool");
                }

                // Check queue size limit
                if (m_maxQueueSize > 0 && m_tasks.size() >= m_maxQueueSize)
                {
                    switch (m_rejectionPolicy)
                    {
                    case RejectionPolicy::BLOCK:
                        // Wait until queue has space
                        m_condition.wait(lock, [this]
                                         { return m_stop || m_tasks.size() < m_maxQueueSize; });
                        if (m_stop)
                        {
                            throw std::runtime_error("ThreadPool stopped while waiting");
                        }
                        break;

                    case RejectionPolicy::DISCARD_OLDEST:
                        // Remove oldest task
                        m_tasks.pop();
                        m_rejectedTasks++;
                        break;

                    case RejectionPolicy::DISCARD_NEWEST:
                        // Don't add new task
                        m_rejectedTasks++;
                        return result; // Return invalid future
                        break;

                    case RejectionPolicy::THROW_EXCEPTION:
                        throw std::runtime_error("ThreadPool queue is full");
                        break;
                    }
                }

                m_tasks.emplace([task]()
                                { (*task)(); });
            }

            m_condition.notify_one();
            return result;
        }

        /**
         * Get the number of pending tasks
         */
        size_t pendingTasks() const
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            return m_tasks.size();
        }

        /**
         * Get the number of worker threads
         */
        size_t workerCount() const
        {
            return m_workers.size();
        }

        /**
         * Get the number of rejected tasks
         */
        size_t rejectedTasks() const
        {
            return m_rejectedTasks.load();
        }

        /**
         * Get the maximum queue size
         */
        size_t maxQueueSize() const
        {
            return m_maxQueueSize;
        }

    private:
        std::vector<std::thread> m_workers;
        std::queue<std::function<void()>> m_tasks;

        mutable std::mutex m_queueMutex;
        std::condition_variable m_condition;
        std::atomic<bool> m_stop;

        size_t m_maxQueueSize;
        RejectionPolicy m_rejectionPolicy;
        std::atomic<size_t> m_rejectedTasks{0};
    };

} // namespace elink
