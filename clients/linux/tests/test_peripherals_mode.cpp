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

// The Peripherals mode (client specification 12.3, 12.4, 13.1): the
// server's peripheral tree with each UI-bearing node's panel embedded,
// subscribed only while the mode shows, and the serial interface line in
// the serial port's section.

#include "app/ExtensionPanel.hpp"
#include "app/MainWindow.hpp"
#include "app/PeripheralsSidebar.hpp"
#include "app/SessionExtensions.hpp"
#include "app/SessionPeripherals.hpp"
#include "app/Sidebar.hpp"

#include "FakeBeebiumServer.hpp"

#include <QLabel>

#include <catch2/catch_test_macros.hpp>

#include <grpcpp/grpcpp.h>

using namespace beebium::linux_client;
using namespace beebium::linux_client::testing;

namespace {

void attach(beebium::ExtensionInfo* e, const char* point, const char* provider = "") {
    e->add_attaches_to(point);
    auto* a = e->add_attachments();
    a->set_point(point);
    a->set_provider_id(provider);
}

beebium::ListExtensionsResponse listing() {
    beebium::ListExtensionsResponse r;
    for (const auto& [id, name] : std::vector<std::pair<const char*, const char*>>{
             {"1mhz-bus", "1 MHz Bus"}, {"serial-port", "Serial Port"}, {"scsi-bus", "SCSI Bus"},
             {"analogue-port", "Analogue Port"}}) {
        auto* p = r.add_attachment_points();
        p->set_id(id);
        p->set_display_name(name);
    }
    auto* adapter = r.add_extensions();
    adapter->set_id("adapter");
    adapter->set_label("Acorn SCSI Host Adapter");
    adapter->set_description("Connects SCSI hard discs");
    attach(adapter, "1mhz-bus");
    auto* disc = r.add_extensions();
    disc->set_id("disc0");
    disc->set_label("Hard Disc (SCSI ID 0)");
    disc->set_has_ui(true);
    attach(disc, "scsi-bus", "adapter");
    auto* serial = r.add_extensions();
    serial->set_id("host-serial");
    serial->set_label("Host Serial");
    serial->set_has_ui(true);
    attach(serial, "serial-port");
    attach(serial, "analogue-port");
    return r;
}

beebium::View panel_view(const char* id) {
    beebium::View view;
    view.set_extension_id(id);
    view.set_view_revision(1);
    view.mutable_root()->set_id("status");
    view.mutable_root()->mutable_label()->set_text(std::string("panel of ") + id);
    return view;
}

struct Fixture {
    FakeServer server;
    SessionExtensions extensions;
    SessionPeripherals peripherals;
    explicit Fixture(beebium::ListExtensionsResponse r = listing()) {
        {
            std::lock_guard lock(server.peripherals.mutex);
            server.peripherals.listing = std::move(r);
        }
        server.extension_ui.push(panel_view("disc0"));
        server.extension_ui.push(panel_view("host-serial"));
        auto channel = grpc::CreateChannel(server.endpoint().toStdString(),
                                           grpc::InsecureChannelCredentials());
        extensions.start(channel);
        peripherals.start(channel);
    }
};

QStringList texts(QWidget& widget) {
    QStringList all;
    for (QLabel* label : widget.findChildren<QLabel*>()) {
        if (label->isVisibleTo(&widget) && !label->text().isEmpty()) {
            all << label->text();
        }
    }
    return all;
}

QLabel* label_with(QWidget& widget, const QString& text) {
    for (QLabel* label : widget.findChildren<QLabel*>()) {
        if (label->text() == text) {
            return label;
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("The mode lists the peripherals once, grouped under their points in the server's order",
          "[peripherals][mode]") {
    Fixture f;
    PeripheralsSidebar mode(f.peripherals, f.extensions, [] { return true; });
    mode.resize(Sidebar::WIDTH, 700);
    mode.show();
    REQUIRE(wait_until([&] { return f.peripherals.loaded(); }));
    REQUIRE(wait_until([&] { return label_with(mode, QStringLiteral("Serial Port")) != nullptr; }));
    CHECK(f.server.peripherals.list_requests == 1);
    const auto y = [&](const char* text) {
        QLabel* label = label_with(mode, QString::fromLatin1(text));
        REQUIRE(label);
        return label->mapTo(&mode, QPoint()).y();
    };
    const auto x = [&](const char* text) {
        QLabel* label = label_with(mode, QString::fromLatin1(text));
        REQUIRE(label);
        return label->mapTo(&mode, QPoint()).x();
    };
    // Points in the server's order; the SCSI bus has no root of its own.
    CHECK(y("1 MHz Bus") < y("Acorn SCSI Host Adapter"));
    CHECK(y("Acorn SCSI Host Adapter") < y("Hard Disc (SCSI ID 0)"));
    CHECK(y("Hard Disc (SCSI ID 0)") < y("Serial Port"));
    CHECK(label_with(mode, QStringLiteral("SCSI Bus")) == nullptr);
    // The disc nests under its adapter.
    CHECK(x("Hard Disc (SCSI ID 0)") > x("Acorn SCSI Host Adapter"));
    CHECK(x("Acorn SCSI Host Adapter") > x("1 MHz Bus"));
    CHECK(label_with(mode, QStringLiteral("Acorn SCSI Host Adapter"))->toolTip() ==
          QStringLiteral("Connects SCSI hard discs"));
    // A second point as a badge.
    CHECK(texts(mode).contains(QStringLiteral("also: Analogue Port")));
}

TEST_CASE("Nodes with a UI embed their panels, subscribed only while the mode shows",
          "[peripherals][mode][subscription]") {
    Fixture f;
    PeripheralsSidebar mode(f.peripherals, f.extensions, [] { return true; });
    mode.resize(Sidebar::WIDTH, 700);
    mode.show();
    REQUIRE(wait_until([&] { return mode.findChildren<ExtensionPanel*>().size() == 2; }));
    REQUIRE(wait_until([&] {
        return f.server.extension_ui.subscribers("disc0") == 1 &&
               f.server.extension_ui.subscribers("host-serial") == 1;
    }));
    REQUIRE(wait_until([&] { return label_with(mode, QStringLiteral("panel of disc0")) != nullptr; }));
    // Under its node, indented further.
    CHECK(label_with(mode, QStringLiteral("panel of disc0"))->mapTo(&mode, QPoint()).x() >
          label_with(mode, QStringLiteral("Hard Disc (SCSI ID 0)"))->mapTo(&mode, QPoint()).x());
    // Leaving the mode releases every subscription.
    mode.hide();
    REQUIRE(wait_until([&] {
        return f.server.extension_ui.subscribers("disc0") == 0 &&
               f.server.extension_ui.subscribers("host-serial") == 0;
    }));
    CHECK(f.server.peripherals.list_requests == 1);  // listed once, not per show
}

TEST_CASE("The serial line shows in the serial port's section while the mode shows",
          "[peripherals][mode][serial]") {
    Fixture f;
    PeripheralsSidebar mode(f.peripherals, f.extensions, [] { return true; });
    mode.resize(Sidebar::WIDTH, 700);
    mode.show();
    REQUIRE(wait_until([&] { return f.server.serial.watchers == 1; }));
    beebium::SerialStatus status;
    status.set_has_serial_socket(true);
    status.set_connector("RS423");
    status.set_rs423_selected(true);
    status.set_tx_baud(9600);
    status.set_rx_baud(9600);
    f.server.serial.push(status);
    REQUIRE(wait_until([&] {
        return label_with(mode, QStringLiteral("RS423 Tx 9600 / Rx 9600 baud")) != nullptr;
    }));
    QLabel* line = label_with(mode, QStringLiteral("RS423 Tx 9600 / Rx 9600 baud"));
    CHECK(line->mapTo(&mode, QPoint()).y() >
          label_with(mode, QStringLiteral("Serial Port"))->mapTo(&mode, QPoint()).y());
    CHECK(line->mapTo(&mode, QPoint()).y() <
          label_with(mode, QStringLiteral("Host Serial"))->mapTo(&mode, QPoint()).y());
    mode.hide();
    REQUIRE(wait_until([&] { return f.server.serial.watchers == 0; }));
}

TEST_CASE("No peripherals, a failed listing and the wait each say so", "[peripherals][mode]") {
    {
        Fixture f{beebium::ListExtensionsResponse{}};
        PeripheralsSidebar mode(f.peripherals, f.extensions, [] { return true; });
        mode.show();
        REQUIRE(wait_until([&] { return f.peripherals.loaded(); }));
        REQUIRE(wait_until([&] { return texts(mode).contains(QStringLiteral("No Peripherals")); }));
        CHECK(texts(mode).contains(
            QStringLiteral("This machine is configured without any external peripherals.")));
    }
    {
        FakeServer server;
        server.peripherals.fail = true;
        SessionExtensions extensions;
        SessionPeripherals peripherals;
        PeripheralsSidebar mode(peripherals, extensions, [] { return true; });
        mode.show();
        CHECK(texts(mode).contains(QStringLiteral("Loading...")));
        peripherals.start(grpc::CreateChannel(server.endpoint().toStdString(),
                                              grpc::InsecureChannelCredentials()));
        REQUIRE(wait_until([&] {
            return texts(mode).contains(QStringLiteral("Couldn't load peripherals"));
        }));
        CHECK(texts(mode).contains(QStringLiteral("the listing failed")));
    }
}

TEST_CASE("A machine window offers the Peripherals mode on Meta+3", "[peripherals][window]") {
    MainWindow window(QRhiWidget::Api::OpenGL);
    auto* sidebar = window.findChild<Sidebar*>();
    REQUIRE(sidebar);
    CHECK(sidebar->has_mode(SidebarMode::peripherals));
    CHECK(window.findChild<PeripheralsSidebar*>() != nullptr);
}
