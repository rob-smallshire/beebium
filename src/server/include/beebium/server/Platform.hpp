// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#ifndef BEEBIUM_SERVER_PLATFORM_HPP
#define BEEBIUM_SERVER_PLATFORM_HPP

#include <beebium/PlatformUtils.hpp>

#include <atomic>
#include <functional>
#include <optional>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <io.h>
#include <chrono>
#include <string>
#include <thread>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <thread>
#include <unistd.h>
#endif

namespace beebium::server::platform {

// Callback type for shutdown handlers
using ShutdownCallback = std::function<void()>;

#ifdef _WIN32

// Windows implementation using SetConsoleCtrlHandler.
// Note: Console control handlers run in a separate thread on Windows.

namespace detail {
    inline ShutdownCallback g_shutdown_callback;
    inline CRITICAL_SECTION g_callback_lock;
    inline bool g_callback_lock_initialized = false;
    inline HANDLE g_child_process = INVALID_HANDLE_VALUE;

    inline BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
        switch (ctrl_type) {
            case CTRL_C_EVENT:
            case CTRL_BREAK_EVENT:
            case CTRL_CLOSE_EVENT:
            case CTRL_SHUTDOWN_EVENT:
                EnterCriticalSection(&g_callback_lock);
                // Terminate child subprocess before invoking the shutdown callback,
                // so it receives the signal even if this process is killed shortly after.
                if (g_child_process != INVALID_HANDLE_VALUE) {
                    TerminateProcess(g_child_process, 1);
                }
                if (g_shutdown_callback) {
                    g_shutdown_callback();
                }
                LeaveCriticalSection(&g_callback_lock);
                return TRUE;
            default:
                return FALSE;
        }
    }
}  // namespace detail

inline void install_shutdown_handler(ShutdownCallback callback) {
    if (!detail::g_callback_lock_initialized) {
        InitializeCriticalSection(&detail::g_callback_lock);
        detail::g_callback_lock_initialized = true;
    }
    EnterCriticalSection(&detail::g_callback_lock);
    detail::g_shutdown_callback = std::move(callback);
    LeaveCriticalSection(&detail::g_callback_lock);
    SetConsoleCtrlHandler(detail::console_ctrl_handler, TRUE);
}

inline void remove_shutdown_handler() {
    SetConsoleCtrlHandler(detail::console_ctrl_handler, FALSE);
    EnterCriticalSection(&detail::g_callback_lock);
    detail::g_shutdown_callback = nullptr;
    LeaveCriticalSection(&detail::g_callback_lock);
}

/// Register a child process handle for automatic termination when this
/// process receives a shutdown signal. The handle is NOT owned by this
/// module; the caller must keep it alive until unregister_child_process().
inline void register_child_process(HANDLE handle) {
    EnterCriticalSection(&detail::g_callback_lock);
    detail::g_child_process = handle;
    LeaveCriticalSection(&detail::g_callback_lock);
}

inline void unregister_child_process() {
    EnterCriticalSection(&detail::g_callback_lock);
    detail::g_child_process = INVALID_HANDLE_VALUE;
    LeaveCriticalSection(&detail::g_callback_lock);
}

inline bool is_stdin_tty() {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    return GetConsoleMode(h, &mode) != 0;
}

/// Wait for a line on stdin, giving up if `keep_waiting` becomes false.
/// Returns true if a line (or end of input) arrived, false if the wait was
/// abandoned. The console control handler runs on its own thread, so it can
/// clear `keep_waiting` while this is waiting -- but only a polling wait can
/// notice: a blocking read of stdin is not interrupted by a console event.
inline bool wait_for_line_or_abandon(const std::atomic<bool>& keep_waiting) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE) {
        return true;  // No stdin to wait on; do not stall startup.
    }
    const bool console = is_stdin_tty();
    for (;;) {
        if (!keep_waiting.load()) {
            return false;
        }
        if (console) {
            // A console handle is signalled for every input event (key up,
            // mouse, focus), so consume events until RETURN goes down rather
            // than blocking in a read that any event could unblock.
            if (WaitForSingleObject(h, 100) != WAIT_OBJECT_0) {
                continue;
            }
            INPUT_RECORD record;
            DWORD read_count = 0;
            while (PeekConsoleInputW(h, &record, 1, &read_count) && read_count > 0) {
                if (!ReadConsoleInputW(h, &record, 1, &read_count) || read_count == 0) {
                    break;
                }
                if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown &&
                    record.Event.KeyEvent.wVirtualKeyCode == VK_RETURN) {
                    return true;
                }
            }
        } else {
            // A pipe or file handle is not usefully waitable, so poll for
            // available bytes and sleep between attempts.
            DWORD available = 0;
            if (!PeekNamedPipe(h, NULL, 0, NULL, &available, NULL)) {
                // Not a pipe (a redirected file, say): a read will not block.
                char byte = 0;
                DWORD read_count = 0;
                while (ReadFile(h, &byte, 1, &read_count, NULL) && read_count > 0) {
                    if (byte == '\n') {
                        return true;
                    }
                }
                return true;  // End of input, or an error: treat as "go".
            }
            if (available == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            char byte = 0;
            DWORD read_count = 0;
            while (ReadFile(h, &byte, 1, &read_count, NULL) && read_count > 0) {
                if (byte == '\n') {
                    return true;
                }
            }
            return true;
        }
    }
}

inline bool is_stdout_tty() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    return GetConsoleMode(h, &mode) != 0;
}

#else  // POSIX

// The signal handler itself may do only async-signal-safe work, so it hands
// the signal to a dedicated dispatch thread over a self-pipe. That thread runs
// the shutdown callback, which means shutdown is delivered whatever the main
// thread is doing -- including when it is blocked in a paused machine's wait,
// or reading a line from stdin. Dispatching from the emulation loop instead
// would make "the process can be asked to stop" conditional on the emulation
// loop still cycling, which is exactly what a paused machine does not do.

namespace detail {
    inline std::atomic<pid_t> g_child_pid{-1};

    // Write end of the self-pipe, read by the dispatch thread. Atomic because
    // the signal handler may run on any thread at any time; -1 until the pipe
    // exists. The pipe and its thread are created once and last for the life of
    // the process, so the handler can never write to a stale descriptor.
    inline std::atomic<int> g_wakeup_write_fd{-1};
    inline int g_wakeup_read_fd = -1;
    // The process the dispatch thread belongs to. fork() does not carry threads
    // into the child, but it does carry this module's state, so a child that
    // installs a handler must be given a thread of its own rather than inherit
    // the belief that one is already running.
    inline pid_t g_dispatch_thread_pid = -1;

    // The callback runs with this held, so clearing it (which also takes the
    // mutex) waits out an invocation already in progress -- the caller can then
    // safely destroy whatever the callback captured.
    inline std::mutex g_callback_mutex;
    inline ShutdownCallback g_shutdown_callback;  // guarded by g_callback_mutex

    inline void signal_handler(int /*signal*/) {
        // Forward the signal to the child subprocess (if any) so it begins
        // shutting down immediately, even if this process is killed before
        // reaching the normal cleanup path. kill(), atomic loads and write()
        // are all async-signal-safe.
        pid_t child = g_child_pid.load(std::memory_order_relaxed);
        if (child > 0) {
            kill(child, SIGTERM);
        }
        int fd = g_wakeup_write_fd.load(std::memory_order_relaxed);
        if (fd >= 0) {
            const char byte = 'S';
            ssize_t written = ::write(fd, &byte, 1);
            (void)written;  // Nothing useful to do about a failure here.
        }
    }

    /// Body of the dispatch thread: block until the signal handler writes to
    /// the self-pipe, then run the shutdown callback.
    inline void dispatch_loop() {
        for (;;) {
            char byte = 0;
            ssize_t count = ::read(g_wakeup_read_fd, &byte, 1);
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return;
            }
            if (count == 0) {
                return;  // Write end closed: nothing can wake us again.
            }
            std::lock_guard<std::mutex> lock(g_callback_mutex);
            if (g_shutdown_callback) {
                g_shutdown_callback();
            }
        }
    }

    /// Create the self-pipe and start the dispatch thread, unless this process
    /// already has one. The thread is detached: it outlives every handler
    /// installed on it and spends its life blocked in read().
    inline void ensure_dispatch_thread() {
        const pid_t self = ::getpid();
        if (g_dispatch_thread_pid == self) {
            return;
        }
        if (g_dispatch_thread_pid != -1) {
            // Inherited across a fork: the descriptors are open but the thread
            // that read them is not in this process.
            int stale_write_fd = g_wakeup_write_fd.exchange(-1, std::memory_order_relaxed);
            if (stale_write_fd >= 0) {
                ::close(stale_write_fd);
            }
            if (g_wakeup_read_fd >= 0) {
                ::close(g_wakeup_read_fd);
                g_wakeup_read_fd = -1;
            }
        }
        int fds[2];
        if (::pipe(fds) != 0) {
            return;
        }
        g_wakeup_read_fd = fds[0];
        // Non-blocking write end: a signal must never block, however unlikely a
        // full one-byte-at-a-time pipe is.
        int flags = ::fcntl(fds[1], F_GETFL, 0);
        ::fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
        g_wakeup_write_fd.store(fds[1], std::memory_order_relaxed);
        g_dispatch_thread_pid = self;
        std::thread(dispatch_loop).detach();
    }
}  // namespace detail

inline void install_shutdown_handler(ShutdownCallback callback) {
    {
        std::lock_guard<std::mutex> lock(detail::g_callback_mutex);
        detail::g_shutdown_callback = std::move(callback);
        detail::ensure_dispatch_thread();
    }
    std::signal(SIGINT, detail::signal_handler);
    std::signal(SIGTERM, detail::signal_handler);
}

inline void remove_shutdown_handler() {
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
    // Blocks until any callback currently running has returned.
    std::lock_guard<std::mutex> lock(detail::g_callback_mutex);
    detail::g_shutdown_callback = nullptr;
}

/// Register a child process PID for automatic SIGTERM when this process
/// receives a shutdown signal. This ensures the child is signalled even
/// if the parent is killed before reaching normal cleanup code.
inline void register_child_process(pid_t pid) {
    detail::g_child_pid.store(pid, std::memory_order_relaxed);
}

inline void unregister_child_process() {
    detail::g_child_pid.store(-1, std::memory_order_relaxed);
}

/// Wait for a line on stdin, giving up if `keep_waiting` becomes false.
/// Returns true if a line (or end of input) arrived, false if the wait was
/// abandoned. Polling rather than blocking in read() is what lets a shutdown
/// signal end the wait: a blocking read is restarted after a handled signal
/// (BSD semantics), so it would never return.
inline bool wait_for_line_or_abandon(const std::atomic<bool>& keep_waiting) {
    for (;;) {
        if (!keep_waiting.load()) {
            return false;
        }
        struct pollfd pfd{STDIN_FILENO, POLLIN, 0};
        int ready = ::poll(&pfd, 1, 100);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return true;  // Cannot wait on this stdin; do not stall startup.
        }
        if (ready == 0) {
            continue;  // Timed out; re-check keep_waiting.
        }
        char byte = 0;
        for (;;) {
            ssize_t count = ::read(STDIN_FILENO, &byte, 1);
            if (count <= 0) {
                return true;  // End of input, or an error: treat as "go".
            }
            if (byte == '\n') {
                return true;
            }
        }
    }
}

inline bool is_stdin_tty() {
    return isatty(STDIN_FILENO) != 0;
}

inline bool is_stdout_tty() {
    return isatty(STDOUT_FILENO) != 0;
}

#endif  // _WIN32

// Delegate to beebium::platform utilities in core.
inline int get_pid() { return beebium::platform::get_pid(); }
inline std::optional<std::string> get_env(const char* name) { return beebium::platform::get_env(name); }

}  // namespace beebium::server::platform

#endif  // BEEBIUM_SERVER_PLATFORM_HPP
