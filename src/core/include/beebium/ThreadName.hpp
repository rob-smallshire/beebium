// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_THREAD_NAME_HPP
#define BEEBIUM_THREAD_NAME_HPP

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace beebium {

// Names the calling thread, so that per-thread tools (top -H, ps -L,
// /proc/<pid>/task/<tid>/comm, perf report --sort comm, Instruments, lldb)
// attribute its time by what it does rather than by the process name.
//
// Linux truncates a name to 15 characters, so a name should fit in that.
// Elsewhere this does nothing.
//
// The server's emulation loop runs on the main thread and is left unnamed:
// on Linux the main thread's name is the process name that pgrep and
// killall match, so it stays the executable's name.
inline void set_current_thread_name(const char* name) {
#if defined(__linux__)
    pthread_setname_np(pthread_self(), name);
#elif defined(__APPLE__)
    pthread_setname_np(name);
#else
    (void)name;
#endif
}

}  // namespace beebium

#endif  // BEEBIUM_THREAD_NAME_HPP
