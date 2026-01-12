#include "utils/process_mutex.h"
#include "utils/logger.h"
#include <sstream>

#ifdef _WIN32
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")
#else
#include <sys/stat.h>
#include <cerrno>
#include <cstring>
#endif

namespace elink
{
    ProcessMutex::ProcessMutex(const std::string &name)
        : m_name(name), m_locked(false)
#ifdef _WIN32
          ,
          m_mutex(nullptr)
#else
          ,
          m_lockFile(-1)
#endif
    {
        // Ensure the name doesn't contain illegal characters
        std::string safeName = name;
        for (char &c : safeName)
        {
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            {
                c = '_';
            }
        }
        m_name = safeName;

#ifndef _WIN32
        // On Unix systems, create lock file path
        m_lockFilePath = "/tmp/elegoo_" + m_name + ".lock";
#endif
    }

    ProcessMutex::~ProcessMutex()
    {
        unlock();
    }

    bool ProcessMutex::tryLock()
    {
        if (m_locked)
        {
            return true; // Already holding the lock
        }

#ifdef _WIN32
        // Windows implementation
        std::string mutexName = "Global\\ELINK_" + m_name;

        m_mutex = CreateMutexA(nullptr, TRUE, mutexName.c_str());
        if (m_mutex == nullptr)
        {
            ELEGOO_LOG_ERROR("Failed to create mutex: {}", GetLastError());
            return false;
        }

        DWORD result = GetLastError();
        if (result == ERROR_ALREADY_EXISTS)
        {
            // Another process already holds this mutex
            CloseHandle(m_mutex);
            m_mutex = nullptr;
            ELEGOO_LOG_INFO("Process mutex '{}' is already held by another process", m_name);
            return false;
        }

        m_locked = true;
        ELEGOO_LOG_INFO("Successfully acquired process mutex '{}'", m_name);
        return true;

#else
        // Unix/Linux implementation
        m_lockFile = open(m_lockFilePath.c_str(), O_CREAT | O_WRONLY, 0644);
        if (m_lockFile == -1)
        {
            ELEGOO_LOG_ERROR("Failed to create lock file '{}': {}", m_lockFilePath, strerror(errno));
            return false;
        }

        // Try to acquire file lock
        if (flock(m_lockFile, LOCK_EX | LOCK_NB) == -1)
        {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                // Another process already holds the lock
                close(m_lockFile);
                m_lockFile = -1;
                ELEGOO_LOG_INFO("Process mutex '{}' is already held by another process", m_name);
                return false;
            }
            else
            {
                ELEGOO_LOG_ERROR("Failed to lock file '{}': {}", m_lockFilePath, strerror(errno));
                close(m_lockFile);
                m_lockFile = -1;
                return false;
            }
        }

        // Write process ID to lock file
        std::string pidStr = std::to_string(getpid()) + "\n";
        if (write(m_lockFile, pidStr.c_str(), pidStr.length()) == -1)
        {
            ELEGOO_LOG_WARN("Failed to write PID to lock file: {}", strerror(errno));
        }

        m_locked = true;
        ELEGOO_LOG_INFO("Successfully acquired process mutex '{}'", m_name);
        return true;
#endif
    }

    void ProcessMutex::unlock()
    {
        if (!m_locked)
        {
            return;
        }

#ifdef _WIN32
        if (m_mutex != nullptr)
        {
            ReleaseMutex(m_mutex);
            CloseHandle(m_mutex);
            m_mutex = nullptr;
        }
#else
        if (m_lockFile != -1)
        {
            // Release file lock
            flock(m_lockFile, LOCK_UN);
            close(m_lockFile);
            m_lockFile = -1;

            // Delete lock file
            unlink(m_lockFilePath.c_str());
        }
#endif

        m_locked = false;
        ELEGOO_LOG_INFO("Released process mutex '{}'", m_name);
    }

    bool ProcessMutex::isLocked() const
    {
        return m_locked;
    }

    const std::string &ProcessMutex::getName() const
    {
        return m_name;
    }

    // ProcessMutexGuard implementation
    ProcessMutexGuard::ProcessMutexGuard(ProcessMutex &mutex)
        : m_mutex(mutex), m_locked(false)
    {
        m_locked = m_mutex.tryLock();
    }

    ProcessMutexGuard::~ProcessMutexGuard()
    {
        if (m_locked)
        {
            m_mutex.unlock();
        }
    }

    bool ProcessMutexGuard::isLocked() const
    {
        return m_locked;
    }
}
