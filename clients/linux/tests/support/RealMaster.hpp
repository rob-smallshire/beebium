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

#ifndef BEEBIUM_LINUX_TESTS_REAL_MASTER_HPP
#define BEEBIUM_LINUX_TESTS_REAL_MASTER_HPP

#include "servers/ServerLauncher.hpp"

#include "RealServer.hpp"
#include "WaitUntil.hpp"

#include <grpcpp/grpcpp.h>

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>

namespace beebium::linux_client::testing {

// A real beebium-master-128, the one beside the configured beebium-model-b
// (RealServer), started from a preset as Create starts a machine (5.1) but
// not advertised on the network, and stopped in teardown.
class RealMaster {
public:
    RealMaster() = default;
    RealMaster(const RealMaster&) = delete;
    RealMaster& operator=(const RealMaster&) = delete;
    ~RealMaster() {
        if (pid_ != 0) {
            terminate_process(pid_);
            wait_until([&] { return !process_alive(pid_); }, std::chrono::seconds(10));
        }
    }

    // Empty when there is none, and the case skips.
    static QString executable_filepath() {
        const QString model_b = RealServer::executable_filepath();
        if (model_b.isEmpty()) {
            return {};
        }
        const QString filepath =
            QFileInfo(model_b).absoluteDir().filePath(QStringLiteral("beebium-master-128"));
        const QFileInfo info(filepath);
        return info.isFile() && info.isExecutable() ? filepath : QString();
    }

    // A preset's id or file. The server waits for a Run, as a launched one does.
    void start(const QString& preset) {
        REQUIRE(logs_.isValid());
        const QString provenance_uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QStringList arguments = preset_launch_arguments(preset, 1, provenance_uuid, QString());
        arguments.removeAll(QStringLiteral("--advertise"));
        ServerLauncher launcher(logs_.path());
        std::optional<LaunchResult> result;
        launcher.launch(executable_filepath(), arguments, provenance_uuid,
                        [&](const LaunchResult& launched) { result = launched; }, 30000);
        REQUIRE(wait_until([&] { return result.has_value(); }, std::chrono::seconds(35)));
        const auto* server = std::get_if<LaunchedServer>(&*result);
        if (!server) {
            FAIL(std::get<LaunchFailure>(*result).server_message.toStdString());
        }
        pid_ = server->pid;
        channel_ = grpc::CreateChannel("127.0.0.1:" + std::to_string(server->port),
                                       grpc::InsecureChannelCredentials());
    }

    std::shared_ptr<grpc::Channel> channel() const { return channel_; }

private:
    QTemporaryDir logs_;
    qint64 pid_ = 0;
    std::shared_ptr<grpc::Channel> channel_;
};

}  // namespace beebium::linux_client::testing

#endif  // BEEBIUM_LINUX_TESTS_REAL_MASTER_HPP
