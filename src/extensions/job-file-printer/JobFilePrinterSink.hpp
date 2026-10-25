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

#ifndef BEEBIUM_EXT_JOB_FILE_PRINTER_SINK_HPP
#define BEEBIUM_EXT_JOB_FILE_PRINTER_SINK_HPP

#include <beebium/extension/PrinterPortDevice.hpp>
#include <beebium/printer/CentronicsReceiver.hpp>
#include <beebium/printer/SinkOptions.hpp>

#include <moodycamel/readerwriterqueue.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

namespace beebium::printer {

// Writes what the BBC prints as one file per print job.
//
// A job begins with the first byte after quiet and ends when no byte has been
// accepted for idle_ticks VIA ticks (1 MHz), that is, in emulated time: a
// paused machine or a debugger stop does not tick the port, so it never
// splits a job. The emulation thread counts the quiet in device(), the
// PrinterPortDevice attached to the port, which passes every call through to
// the CentronicsReceiver and, when a job has been quiet long enough, hands
// the sink thread the count of bytes received by its end. The sink thread
// drains the receiver as file-printer's does, opening a job file at a job's
// first byte, writing and flushing bytes as they arrive, and closing the file
// once every byte of the job is written. The destructor writes what is still
// queued and closes the open job.
//
// Job files are named job-<YYYYMMDDThhmmssZ>.prn in UTC, stamped when the
// job's first byte is written, with -2, -3, ... before the extension when a
// file of that name already exists.
class JobFilePrinterSink {
public:
    struct Options {
        std::filesystem::path dirpath;       // created if missing
        uint64_t idle_ticks = 5'000'000;     // quiet that ends a job, in VIA ticks
        LineEnding line_ending = LineEnding::Raw;
        std::size_t buffer = 256;            // receiver queue depth in bytes
        // The wall-clock time a job file is stamped with.
        std::function<std::chrono::system_clock::time_point()> now =
            [] { return std::chrono::system_clock::now(); };
        // Called on the sink thread after each drain pass and each job end,
        // for a panel that wants to redraw.
        std::function<void()> on_activity;
    };

    struct Status {
        std::string dirpath;
        std::string current_job_filepath;    // empty between jobs
        uint64_t bytes_in_job = 0;           // written to the current job file
        uint64_t jobs_completed = 0;
        uint64_t total_bytes = 0;            // written to all job files
    };

    explicit JobFilePrinterSink(Options options)
        : options_(std::move(options))
        , receiver_(receiver_options(options_))
        , device_(*this)
        , translator_(options_.line_ending) {
        std::error_code ec;
        if (std::filesystem::exists(options_.dirpath, ec) &&
            !std::filesystem::is_directory(options_.dirpath, ec)) {
            throw std::runtime_error("job-file-printer: '" + options_.dirpath.string() +
                                     "' is a file, not a directory");
        }
        std::filesystem::create_directories(options_.dirpath, ec);
        if (!std::filesystem::is_directory(options_.dirpath)) {
            throw std::runtime_error("job-file-printer: cannot create directory '" +
                                     options_.dirpath.string() + "'");
        }
        if (options_.idle_ticks == 0) {
            throw std::runtime_error("job-file-printer: idle must be positive");
        }
        sink_thread_ = std::thread([this] { sink_loop(); });
    }

    ~JobFilePrinterSink() {
        stop_.store(true, std::memory_order_release);
        if (sink_thread_.joinable()) {
            sink_thread_.join();
        }
    }

    JobFilePrinterSink(const JobFilePrinterSink&) = delete;
    JobFilePrinterSink& operator=(const JobFilePrinterSink&) = delete;

    // The device to attach to the printer port.
    PrinterPortDevice& device() { return device_; }

    CentronicsReceiver& receiver() { return receiver_; }

    // Safe from any thread.
    Status status() const {
        Status status;
        status.dirpath = options_.dirpath.string();
        {
            std::lock_guard<std::mutex> lock(job_filepath_mutex_);
            status.current_job_filepath = job_filepath_.string();
        }
        status.bytes_in_job = bytes_in_job_.load(std::memory_order_relaxed);
        status.jobs_completed = jobs_completed_.load(std::memory_order_relaxed);
        status.total_bytes = total_bytes_.load(std::memory_order_relaxed);
        return status;
    }

    const Options& options() const { return options_; }

    // The file name a job stamped at `time` takes, before any collision suffix.
    static std::string job_filename(std::chrono::system_clock::time_point time,
                                    int suffix = 1) {
        const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
        std::tm utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &seconds);
#else
        gmtime_r(&seconds, &utc);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%Y%m%dT%H%M%SZ", &utc);
        std::string name = std::string("job-") + stamp;
        if (suffix > 1) {
            name += "-" + std::to_string(suffix);
        }
        return name + ".prn";
    }

private:
    // Passes the port through to the receiver and counts the quiet after the
    // last accepted byte. Emulation thread only.
    class JobBoundaryDevice final : public PrinterPortDevice {
    public:
        explicit JobBoundaryDevice(JobFilePrinterSink& sink) : sink_(sink) {}

        void update_data_lines(uint8_t data) override {
            sink_.receiver_.update_data_lines(data);
        }

        uint8_t update_control_lines(uint8_t strobe, uint8_t data) override {
            const uint8_t ack = sink_.receiver_.update_control_lines(strobe, data);
            const uint64_t received = sink_.receiver_.bytes_received();
            if (received != last_received_) {
                last_received_ = received;
                quiet_ticks_ = 0;
                in_job_ = true;
            } else if (in_job_ && ++quiet_ticks_ >= sink_.options_.idle_ticks) {
                in_job_ = false;
                // At most one boundary per idle period reaches the queue, and
                // the sink takes them every few milliseconds of host time, so
                // it does not fill. If it ever did, the job would run on into
                // the next one's file rather than stall the emulation thread.
                sink_.job_ends_.try_enqueue(received);
            }
            return ack;
        }

    private:
        JobFilePrinterSink& sink_;
        uint64_t last_received_ = 0;
        uint64_t quiet_ticks_ = 0;
        bool in_job_ = false;
    };

    static CentronicsReceiver::Options receiver_options(const Options& options) {
        CentronicsReceiver::Options rx;
        rx.queue_depth = options.buffer;
        return rx;
    }

    void sink_loop() {
        using namespace std::chrono_literals;
        uint8_t byte = 0;
        while (!stop_.load(std::memory_order_acquire)) {
            if (receiver_.wait_dequeue_timed(byte, 20ms)) {
                drain_from(byte);
            }
            take_job_ends();
        }
        // A final pass for anything strobed before the stop, then the open
        // job is closed: shutting down ends it.
        if (receiver_.try_dequeue(byte)) {
            drain_from(byte);
        }
        take_job_ends();
        close_job();
    }

    // Write `first` and everything else queued, then flush once. The bytes
    // count as written only once a flush or a close has put them in the file.
    void drain_from(uint8_t first) {
        write_byte(first);
        uint8_t byte = 0;
        while (receiver_.try_dequeue(byte)) {
            write_byte(byte);
        }
        if (job_open_) {
            file_.flush();
            count_flushed_bytes();
        }
        if (options_.on_activity) {
            options_.on_activity();
        }
    }

    // Close the job for every job end at or before the bytes written so far.
    // A job end records the bytes received by the job's end, and those were
    // all queued before it, so a job is closed only after its last byte.
    void take_job_ends() {
        bool closed = false;
        while (const uint64_t* end = job_ends_.peek()) {
            if (*end > bytes_dequeued_) {
                break;
            }
            job_ends_.pop();
            closed = close_job() || closed;
        }
        if (closed && options_.on_activity) {
            options_.on_activity();
        }
    }

    void write_byte(uint8_t byte) {
        // A job end at this byte's position belongs before it.
        take_job_ends();
        if (!job_open_) {
            open_job();
        }
        translator_.translate(byte, [this](uint8_t out) {
            file_.put(static_cast<char>(out));
            ++unflushed_bytes_;
        });
        ++bytes_dequeued_;
    }

    void count_flushed_bytes() {
        bytes_in_job_.fetch_add(unflushed_bytes_, std::memory_order_relaxed);
        total_bytes_.fetch_add(unflushed_bytes_, std::memory_order_relaxed);
        unflushed_bytes_ = 0;
    }

    void open_job() {
        const auto time = options_.now();
        std::filesystem::path filepath;
        for (int suffix = 1;; ++suffix) {
            filepath = options_.dirpath / job_filename(time, suffix);
            std::error_code ec;
            if (!std::filesystem::exists(filepath, ec)) {
                break;
            }
        }
        file_.open(filepath, std::ios::binary | std::ios::trunc);
        if (!file_) {
            // The bytes have nowhere to go; say so once per job rather than
            // stall the guest, which is waiting for /ACK.
            std::fprintf(stderr, "job-file-printer: cannot open '%s' for writing\n",
                         filepath.string().c_str());
        }
        translator_.reset();
        job_open_ = true;
        bytes_in_job_.store(0, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(job_filepath_mutex_);
        job_filepath_ = filepath;
    }

    // Close the open job, if there is one; true if there was.
    bool close_job() {
        if (!job_open_) {
            return false;
        }
        job_open_ = false;
        {
            std::lock_guard<std::mutex> lock(job_filepath_mutex_);
            job_filepath_.clear();
        }
        file_.close();
        file_.clear();
        count_flushed_bytes();
        bytes_in_job_.store(0, std::memory_order_relaxed);
        jobs_completed_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    Options options_;
    CentronicsReceiver receiver_;
    JobBoundaryDevice device_;
    // Job ends, as the count of bytes received by each: the emulation thread
    // produces, the sink thread consumes.
    moodycamel::ReaderWriterQueue<uint64_t> job_ends_{64};

    // Sink thread only
    LineEndingTranslator translator_;
    std::ofstream file_;
    bool job_open_ = false;
    uint64_t bytes_dequeued_ = 0;
    uint64_t unflushed_bytes_ = 0;  // written to file_ but not yet flushed

    mutable std::mutex job_filepath_mutex_;
    std::filesystem::path job_filepath_;  // empty between jobs
    std::atomic<uint64_t> bytes_in_job_{0};
    std::atomic<uint64_t> jobs_completed_{0};
    std::atomic<uint64_t> total_bytes_{0};
    std::atomic<bool> stop_{false};
    std::thread sink_thread_;
};

}  // namespace beebium::printer

#endif  // BEEBIUM_EXT_JOB_FILE_PRINTER_SINK_HPP
