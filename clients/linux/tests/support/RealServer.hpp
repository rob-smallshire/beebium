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

#ifndef BEEBIUM_LINUX_TESTS_REAL_SERVER_HPP
#define BEEBIUM_LINUX_TESTS_REAL_SERVER_HPP

#include "WaitUntil.hpp"

#include <grpcpp/grpcpp.h>

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTcpServer>

#include <chrono>
#include <memory>

namespace beebium::linux_client::testing {

// A real beebium-model-b for the tests that need the machine itself, as
// the Python client's launch helpers start one: from a build tree, on a
// free port, stopped in teardown. The executable is
// BEEBIUM_TEST_SERVER_FILEPATH from the environment, else the one the
// build was configured with; a test skips when neither exists.
class RealServer {
public:
    RealServer() = default;
    RealServer(const RealServer&) = delete;
    RealServer& operator=(const RealServer&) = delete;
    ~RealServer() { stop(); }

    static QString executable_filepath() {
        const QString configured =
            QProcessEnvironment::systemEnvironment().value(QStringLiteral("BEEBIUM_TEST_SERVER_FILEPATH"));
        if (!configured.isEmpty()) {
            return configured;
        }
#ifdef BEEBIUM_TEST_SERVER_FILEPATH
        return QStringLiteral(BEEBIUM_TEST_SERVER_FILEPATH);
#else
        return {};
#endif
    }
    static bool available() {
        const QFileInfo info(executable_filepath());
        return info.isFile() && info.isExecutable();
    }

    // The server's own blank tape templates, found as it finds them: beside
    // the executable in a build tree, else in the installed share tree.
    static QString blank_tapes_dirpath() {
        const QDir exe_dirpath = QFileInfo(executable_filepath()).absoluteDir();
        const QString build_dirpath = exe_dirpath.filePath(QStringLiteral("blank-tapes"));
        if (QFileInfo(build_dirpath).isDir()) {
            return build_dirpath;
        }
        return QDir(exe_dirpath.filePath(QStringLiteral(".."))).filePath(QStringLiteral("share/beebium/blank-tapes"));
    }

    // Started and answering on its port, or false with the server's words
    // in `output()`.
    bool start(std::chrono::milliseconds timeout = std::chrono::seconds(30)) {
        port_ = free_port();
        process_.setProgram(executable_filepath());
        process_.setArguments({QStringLiteral("--port"), QString::number(port_)});
        process_.setProcessChannelMode(QProcess::MergedChannels);
        process_.start();
        if (!process_.waitForStarted(5000)) {
            return false;
        }
        channel_ = grpc::CreateChannel("127.0.0.1:" + std::to_string(port_),
                                       grpc::InsecureChannelCredentials());
        return wait_until(
            [&] {
                if (process_.state() != QProcess::Running) {
                    return true;
                }
                return channel_->WaitForConnected(std::chrono::system_clock::now() +
                                                  std::chrono::milliseconds(100));
            },
            timeout) &&
               process_.state() == QProcess::Running;
    }

    void stop() {
        if (process_.state() == QProcess::NotRunning) {
            return;
        }
        process_.terminate();
        if (!process_.waitForFinished(5000)) {
            process_.kill();
            process_.waitForFinished(5000);
        }
    }

    std::shared_ptr<grpc::Channel> channel() const { return channel_; }
    QString output() { return QString::fromLocal8Bit(process_.readAll()); }

private:
    static quint16 free_port() {
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        return probe.serverPort();
    }

    QProcess process_;
    quint16 port_ = 0;
    std::shared_ptr<grpc::Channel> channel_;
};

}  // namespace beebium::linux_client::testing

#endif  // BEEBIUM_LINUX_TESTS_REAL_SERVER_HPP
