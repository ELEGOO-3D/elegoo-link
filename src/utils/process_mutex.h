#pragma once

#include <string>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace elink
{
    /**
     * Cross-platform process mutex lock
     * Used to ensure that the same user's connection can only be active in one process
     */
    class ProcessMutex
    {
    public:
        /**
         * Constructor
         * @param name Mutex lock name, recommended format: "service_userid"
         */
        explicit ProcessMutex(const std::string &name);

        /**
         * Destructor, automatically releases the lock
         */
        ~ProcessMutex();

        /**
         * Try to acquire the mutex lock
         * @return true if lock acquired successfully, false if another process already holds the lock
         */
        bool tryLock();

        /**
         * Release the mutex lock
         */
        void unlock();

        /**
         * Check if the lock is held by the current process
         * @return true if the current process holds the lock
         */
        bool isLocked() const;

        /**
         * Get the mutex lock name
         * @return Mutex lock name
         */
        const std::string &getName() const;

    private:
        std::string m_name;
        bool m_locked;

#ifdef _WIN32
        HANDLE m_mutex;
#else
        int m_lockFile;
        std::string m_lockFilePath;
#endif

        // Disallow copy construction and assignment
        ProcessMutex(const ProcessMutex &) = delete;
        ProcessMutex &operator=(const ProcessMutex &) = delete;
    };

    /**
     * RAII-style process mutex guard
     */
    class ProcessMutexGuard
    {
    public:
        /**
         * Constructor, attempts to acquire the lock
         * @param mutex Mutex lock object
         */
        explicit ProcessMutexGuard(ProcessMutex &mutex);

        /**
         * Destructor, automatically releases the lock
         */
        ~ProcessMutexGuard();

        /**
         * Check if the lock was acquired successfully
         * @return true if lock acquired successfully
         */
        bool isLocked() const;

    private:
        ProcessMutex &m_mutex;
        bool m_locked;

        // Disallow copy construction and assignment
        ProcessMutexGuard(const ProcessMutexGuard &) = delete;
        ProcessMutexGuard &operator=(const ProcessMutexGuard &) = delete;
    };
}
