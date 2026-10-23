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

// The POSIX half of PrinterDevice: open(2), poll(2) and write(2) on the
// device file. The Linux lp and usblp drivers report an absent or offline
// printer as ENXIO, ENODEV, EIO or the like, from open() or from a write;
// a FIFO with no reader reports ENXIO from open() and EPIPE from a write,
// which is how the tests stand in for a printer. Every such error means the
// printer has gone. EAGAIN is not one of them: it is a busy printer, and the
// write is simply retried.

#include "PrinterDevice.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <system_error>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

namespace beebium::printer {

namespace {

std::string errno_text(int error_number) {
    return std::strerror(error_number);
}

// A write that failed with EPIPE leaves a SIGPIPE pending on this thread,
// where prepare_writer_thread() blocked it; take it so it is never delivered.
void discard_pending_sigpipe() {
    sigset_t pending;
    sigemptyset(&pending);
    if (sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1) {
        sigset_t sigpipe;
        sigemptyset(&sigpipe);
        sigaddset(&sigpipe, SIGPIPE);
        int taken = 0;
        sigwait(&sigpipe, &taken);
    }
}

class PosixPrinterDevice final : public PrinterDevice {
public:
    explicit PosixPrinterDevice(int fd) : fd_(fd) {}
    ~PosixPrinterDevice() override { ::close(fd_); }

    WriteResult write(const uint8_t* data, std::size_t size,
                      std::chrono::milliseconds timeout) override {
        WriteResult result;
        pollfd pfd{fd_, POLLOUT, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (ready < 0) {
            if (errno != EINTR) {
                result.failed = true;
                result.error = errno_text(errno);
            }
            return result;
        }
        if (ready == 0) {
            return result;  // busy: nothing taken yet
        }
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            result.failed = true;
            result.error = "the device reported an error";
            return result;
        }
        const ssize_t written = ::write(fd_, data, size);
        if (written >= 0) {
            result.bytes = static_cast<std::size_t>(written);
            return result;
        }
        const int error_number = errno;
        if (error_number == EAGAIN || error_number == EWOULDBLOCK || error_number == EINTR) {
            return result;  // busy
        }
        if (error_number == EPIPE) {
            discard_pending_sigpipe();
        }
        result.failed = true;
        result.error = errno_text(error_number);
        return result;
    }

    bool has_failed() override {
        pollfd pfd{fd_, POLLOUT, 0};
        return ::poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
    }

private:
    int fd_;
};

}  // namespace

std::vector<PrinterDeviceInfo> list_printer_devices() {
    namespace fs = std::filesystem;
    std::vector<PrinterDeviceInfo> devices;
    // lp followed by digits only, in `dirpath`.
    auto add_nodes = [&](const fs::path& dirpath, const std::string& kind) {
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dirpath, ec)) {
            const std::string filename = entry.path().filename().string();
            if (filename.size() > 2 && filename.compare(0, 2, "lp") == 0 &&
                filename.find_first_not_of("0123456789", 2) == std::string::npos) {
                devices.push_back({entry.path().string(), kind + " " + filename});
            }
        }
    };
    add_nodes("/dev/usb", "USB printer");
    add_nodes("/dev", "Parallel port");
    std::sort(devices.begin(), devices.end(),
              [](const auto& a, const auto& b) { return a.path < b.path; });
    return devices;
}

std::unique_ptr<PrinterDevice> PrinterDevice::open(const std::string& path, std::string& error) {
    // Non-blocking, so neither open() nor a write can park the thread on a
    // printer that never answers: writes wait in poll() with a timeout.
    const int fd = ::open(path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (fd < 0) {
        error = "cannot open " + path + ": " + errno_text(errno);
        return nullptr;
    }
#ifdef F_SETNOSIGPIPE
    // macOS: EPIPE without a SIGPIPE at all.
    ::fcntl(fd, F_SETNOSIGPIPE, 1);
#endif
    return std::make_unique<PosixPrinterDevice>(fd);
}

void PrinterDevice::cancel_blocked_write(std::thread& /*writer*/) {}

void PrinterDevice::prepare_writer_thread() {
    sigset_t sigpipe;
    sigemptyset(&sigpipe);
    sigaddset(&sigpipe, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &sigpipe, nullptr);
}

}  // namespace beebium::printer
