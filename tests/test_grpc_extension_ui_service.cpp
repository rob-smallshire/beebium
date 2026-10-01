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

// End-to-end gRPC tests for ExtensionUiService. Drives a fake extension
// (FakeUiTransport) loaded into an EconetTransportRegistry through the
// real service implementation, with no real machine state involved.
// Exercises the full validation gauntlet (NOT_FOUND, stale revision,
// unknown control id, payload-type mismatch) plus the streaming
// SubscribeView path with mark_dirty triggering a second push.

#include <catch2/catch_test_macros.hpp>

#include "beebium/Machines.hpp"
#include "beebium/econet/NetworkBackend.hpp"
#include "beebium/extension/EconetTransportExtension.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/ExtensionUi.hpp"
#include "beebium/service/ExtensionUiService.hpp"
#include "beebium/service/Server.hpp"

#include "extension_ui.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace {

// FakeUi exposes a single Toggle (id="toggle1") whose value is mutable
// from tests. handle_event records the last event so tests can assert
// that the framework reached it.
class FakeUi : public beebium::ExtensionUi {
public:
    void build_view(beebium::View* out) const override {
        auto* root = out->mutable_root();
        root->set_id("root");
        auto* group = root->mutable_group();
        auto* tc = group->add_controls();
        tc->set_id("toggle1");
        auto* t = tc->mutable_toggle();
        t->set_label("Test");
        t->set_value(value_.load(std::memory_order_acquire));
    }

    void handle_event(const beebium::DispatchRequest& req) override {
        last_control_id_ = req.control_id();
        if (req.payload_case() == beebium::DispatchRequest::kBoolValue) {
            value_.store(req.bool_value(), std::memory_order_release);
        }
        ++event_count_;
    }

    bool value() const { return value_.load(std::memory_order_acquire); }
    const std::string& last_control_id() const { return last_control_id_; }
    int event_count() const { return event_count_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> value_{false};
    std::string last_control_id_;
    std::atomic<int> event_count_{0};
};

// Minimal EconetTransportExtension subclass that exposes a FakeUi.
// create_backend() is never called because the tests don't fit Econet
// hardware on the machine -- the ExtensionUiService only cares about
// name() and ui().
class FakeUiTransport : public beebium::EconetTransportExtension {
public:
    explicit FakeUiTransport(std::string name = "fake") {
        beebium::ExtensionManifest m;
        m.name = std::move(name);
        m.description = "Fake UI extension for tests";
        m.cli_name = m.name;
        m.extension_kind = "econet-transport";
        set_manifest(std::move(m));
    }

    std::unique_ptr<beebium::NetworkBackend> create_backend(uint8_t) override {
        return nullptr;
    }

    beebium::ExtensionUi* ui() override { return &ui_; }
    FakeUi& fake_ui() { return ui_; }

private:
    FakeUi ui_;
};

// FakeModalUi exposes a single ModalEditor whose editor body is a Group
// containing a Choice (id="choice1") and a TextInput (id="text1"). Used
// by the EditorCommit-targeted dispatcher validation tests.
class FakeModalUi : public beebium::ExtensionUi {
public:
    void build_view(beebium::View* out) const override {
        auto* root = out->mutable_root();
        root->set_id("root");
        auto* group = root->mutable_group();
        auto* control = group->add_controls();
        control->set_id("editor1");
        auto* modal = control->mutable_modal_editor();
        modal->set_editable(editable_.load(std::memory_order_acquire));
        modal->set_commit_role(beebium::ModalEditor_CommitRole_SAVE);
        modal->set_show_cancel(true);

        auto* anchor = modal->mutable_anchor();
        anchor->set_id("editor1");
        anchor->mutable_label()->set_text("Current: (unset)");

        auto* editor = modal->mutable_editor();
        editor->set_id("editor_body");
        auto* body = editor->mutable_group();

        auto* choice_ctrl = body->add_controls();
        choice_ctrl->set_id("choice1");
        auto* choice = choice_ctrl->mutable_choice();
        choice->set_label("Choice");
        *choice->add_options() = "first";
        *choice->add_options() = "second";
        choice->set_selected_index(0);

        auto* text_ctrl = body->add_controls();
        text_ctrl->set_id("text1");
        auto* text = text_ctrl->mutable_text_input();
        text->set_label("Text");
    }

    void handle_event(const beebium::DispatchRequest& req) override {
        last_commit_ = req.editor_commit();
        ++event_count_;
    }

    void set_editable(bool e) {
        editable_.store(e, std::memory_order_release);
        mark_dirty();
    }

    const beebium::EditorCommit& last_commit() const { return last_commit_; }
    int event_count() const { return event_count_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> editable_{true};
    beebium::EditorCommit last_commit_;
    std::atomic<int> event_count_{0};
};

// FakeListUi exposes an EditableList ("list1") and a FileReference
// ("file1"), used by the EditableListEvent / file_action_id dispatch
// validation tests. The list has one add_editor field ("add.host"), an
// item "a" that is editable + removable with an editor field
// ("edit.a.host") and a "save" action, and an item "b" that is neither
// editable nor removable and carries a "save" action. The file reference
// offers a single "reload" action. handle_event records the last event.
class FakeListUi : public beebium::ExtensionUi {
public:
    void build_view(beebium::View* out) const override {
        auto* root = out->mutable_root();
        root->set_id("root");
        auto* group = root->mutable_group();

        auto* list_ctrl = group->add_controls();
        list_ctrl->set_id("list1");
        auto* list = list_ctrl->mutable_editable_list();
        list->set_title("Peers");
        list->set_can_add(can_add_.load(std::memory_order_acquire));
        list->set_empty_text("No peers");
        {
            auto* add_editor = list->mutable_add_editor();
            add_editor->set_id("list1.add_editor");
            auto* body = add_editor->mutable_group();
            auto* field = body->add_controls();
            field->set_id("add.host");
            field->mutable_text_input()->set_label("host");
        }
        {
            auto* item = list->add_items();
            item->set_id("a");
            item->set_primary("0.254  10.0.0.1:32768");
            item->set_secondary("map file");
            item->set_editable(true);
            item->set_removable(true);
            auto* editor = item->mutable_editor();
            editor->set_id("a.editor");
            auto* body = editor->mutable_group();
            auto* field = body->add_controls();
            field->set_id("edit.a.host");
            field->mutable_text_input()->set_label("host");
            auto* action = item->add_actions();
            action->set_id("save");
            action->set_title("Save to map file");
        }
        {
            auto* item = list->add_items();
            item->set_id("b");
            item->set_primary("0.100  10.0.0.2:40001");
            item->set_secondary("mDNS");
            item->set_editable(false);
            item->set_removable(false);
            auto* action = item->add_actions();
            action->set_id("save");
            action->set_title("Save to map file");
        }

        auto* file_ctrl = group->add_controls();
        file_ctrl->set_id("file1");
        auto* file = file_ctrl->mutable_file_reference();
        file->set_path("/tmp/aun-map.json");
        file->set_display_name("aun-map.json");
        file->set_state(beebium::Indicator_State_OK);
        file->set_state_text("2 peers");
        auto* action = file->add_actions();
        action->set_id("reload");
        action->set_title("Reload");
    }

    void handle_event(const beebium::DispatchRequest& req) override {
        last_control_id_ = req.control_id();
        if (req.payload_case() ==
            beebium::DispatchRequest::kEditableListEvent) {
            last_list_event_ = req.editable_list_event();
        } else if (req.payload_case() ==
                   beebium::DispatchRequest::kFileActionId) {
            last_file_action_ = req.file_action_id();
        }
        ++event_count_;
    }

    void set_can_add(bool v) {
        can_add_.store(v, std::memory_order_release);
        mark_dirty();
    }

    const beebium::EditableListEvent& last_list_event() const {
        return last_list_event_;
    }
    const std::string& last_file_action() const { return last_file_action_; }
    const std::string& last_control_id() const { return last_control_id_; }
    int event_count() const {
        return event_count_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> can_add_{true};
    beebium::EditableListEvent last_list_event_;
    std::string last_file_action_;
    std::string last_control_id_;
    std::atomic<int> event_count_{0};
};

class FakeListTransport : public beebium::EconetTransportExtension {
public:
    explicit FakeListTransport(std::string name = "list") {
        beebium::ExtensionManifest m;
        m.name = std::move(name);
        m.description = "Fake editable-list UI extension for tests";
        m.cli_name = m.name;
        m.extension_kind = "econet-transport";
        set_manifest(std::move(m));
    }

    std::unique_ptr<beebium::NetworkBackend> create_backend(uint8_t) override {
        return nullptr;
    }

    beebium::ExtensionUi* ui() override { return &ui_; }
    FakeListUi& fake_ui() { return ui_; }

private:
    FakeListUi ui_;
};

class FakeModalTransport : public beebium::EconetTransportExtension {
public:
    explicit FakeModalTransport(std::string name = "modal") {
        beebium::ExtensionManifest m;
        m.name = std::move(name);
        m.description = "Fake modal-editor UI extension for tests";
        m.cli_name = m.name;
        m.extension_kind = "econet-transport";
        set_manifest(std::move(m));
    }

    std::unique_ptr<beebium::NetworkBackend> create_backend(uint8_t) override {
        return nullptr;
    }

    beebium::ExtensionUi* ui() override { return &ui_; }
    FakeModalUi& fake_ui() { return ui_; }

private:
    FakeModalUi ui_;
};

// Spin up a Server with only ExtensionUiService bound to a registry
// containing the caller's transport extensions. The peripheral registry
// is left empty.
class ExtensionUiFixture {
public:
    explicit ExtensionUiFixture(beebium::EconetTransportRegistry& transports)
        : service_(transports, peripheral_registry_) {
        services_.push_back(&service_);

        server_ = std::make_unique<beebium::service::Server<beebium::ModelB>>(
            machine_, "127.0.0.1", 0);
        server_->start(beebium::service::Provenance{},
                       beebium::service::MachineIdentity{},
                       /*enable_advertisement=*/false,
                       /*policy_config=*/{},
                       /*shutdown_callback=*/nullptr,
                       std::span<grpc::Service*>(services_));

        std::string address = "127.0.0.1:" + std::to_string(server_->port());
        channel_ = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        stub_ = beebium::ExtensionUiService::NewStub(channel_);
    }

    ~ExtensionUiFixture() {
        server_->stop();
    }

    beebium::ExtensionUiService::Stub& stub() { return *stub_; }

private:
    beebium::ModelB machine_;
    beebium::ExtensionRegistry peripheral_registry_;
    beebium::service::ExtensionUiServiceImpl service_;
    std::vector<grpc::Service*> services_;
    std::unique_ptr<beebium::service::Server<beebium::ModelB>> server_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<beebium::ExtensionUiService::Stub> stub_;
};

}  // namespace

TEST_CASE("SubscribeView for unknown extension fails NOT_FOUND immediately",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::SubscribeViewRequest req;
    req.set_extension_id("nonexistent");
    auto reader = fixture.stub().SubscribeView(&ctx, req);

    beebium::View view;
    REQUIRE_FALSE(reader->Read(&view));
    auto status = reader->Finish();
    REQUIRE(status.error_code() == grpc::StatusCode::NOT_FOUND);
}

TEST_CASE("SubscribeView pushes initial View, then a second after mark_dirty",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::SubscribeViewRequest req;
    req.set_extension_id("fake");
    auto reader = fixture.stub().SubscribeView(&ctx, req);

    beebium::View view;
    REQUIRE(reader->Read(&view));
    REQUIRE(view.extension_id() == "fake");
    REQUIRE(view.view_revision() == 1u);
    REQUIRE(view.root().id() == "root");
    REQUIRE(view.root().control_case() == beebium::Control::kGroup);
    REQUIRE(view.root().group().controls_size() == 1);
    REQUIRE(view.root().group().controls(0).id() == "toggle1");
    REQUIRE(view.root().group().controls(0).toggle().value() == false);

    fake_ptr->fake_ui().mark_dirty();

    REQUIRE(reader->Read(&view));
    REQUIRE(view.view_revision() == 2u);

    ctx.TryCancel();
    // Drain any final write the server may have queued before noticing
    // the cancel; ignore the result.
    while (reader->Read(&view)) {}
    (void)reader->Finish();
}

TEST_CASE("SubscribeView distinguishes two instances of the same extension type by id",
          "[grpc][extension-ui]") {
    // ExtensionUiService keys subscriptions by Extension::id() rather
    // than by manifest name, so multi-instance extensions (most
    // obviously, multiple SCSI hard discs on the same adapter) are
    // each addressable. Two FakeUiTransports with the same manifest
    // name "fake" go in; the registry's Step-1 auto-id machinery
    // hands them "fake" and "fake-1"; the service uses those.
    beebium::EconetTransportRegistry registry;
    auto a = std::make_unique<FakeUiTransport>("fake");
    auto b = std::make_unique<FakeUiTransport>("fake");
    auto* a_ptr = a.get();
    auto* b_ptr = b.get();
    registry.add(std::move(a));
    registry.add(std::move(b));

    REQUIRE(a_ptr->id() == "fake");
    REQUIRE(b_ptr->id() == "fake-1");

    ExtensionUiFixture fixture(registry);

    // Initial state distinct so we can tell the views apart later.
    b_ptr->fake_ui().handle_event([]{
        beebium::DispatchRequest r;
        r.set_extension_id("fake-1");
        r.set_control_id("toggle1");
        r.set_view_revision(1);
        r.set_bool_value(true);
        return r;
    }());

    auto subscribe = [&](const std::string& id,
                         beebium::View* out_view) {
        grpc::ClientContext ctx;
        beebium::SubscribeViewRequest req;
        req.set_extension_id(id);
        auto reader = fixture.stub().SubscribeView(&ctx, req);
        bool ok = reader->Read(out_view);
        ctx.TryCancel();
        beebium::View drain;
        while (reader->Read(&drain)) {}
        (void)reader->Finish();
        return ok;
    };

    beebium::View view_a;
    REQUIRE(subscribe("fake", &view_a));
    REQUIRE(view_a.extension_id() == "fake");
    REQUIRE(view_a.root().group().controls(0).toggle().value() == false);

    beebium::View view_b;
    REQUIRE(subscribe("fake-1", &view_b));
    REQUIRE(view_b.extension_id() == "fake-1");
    REQUIRE(view_b.root().group().controls(0).toggle().value() == true);
}

TEST_CASE("Dispatch on current revision with matching payload runs handle_event",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("fake");
    req.set_control_id("toggle1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    req.set_bool_value(true);

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE(resp.accepted());
    REQUIRE(resp.error().empty());
    REQUIRE(fake_ptr->fake_ui().value() == true);
    REQUIRE(fake_ptr->fake_ui().last_control_id() == "toggle1");
    REQUIRE(fake_ptr->fake_ui().event_count() == 1);
}

TEST_CASE("Dispatch with stale view_revision is rejected",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    // Bump the revision so the request below is stale by construction.
    fake_ptr->fake_ui().mark_dirty();
    REQUIRE(fake_ptr->fake_ui().current_revision() == 2u);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("fake");
    req.set_control_id("toggle1");
    req.set_view_revision(1);  // stale
    req.set_bool_value(true);

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("stale") != std::string::npos);
    // handle_event must not have been called.
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
    REQUIRE(fake_ptr->fake_ui().value() == false);
}

TEST_CASE("Dispatch with unknown control id is rejected",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("fake");
    req.set_control_id("does-not-exist");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    req.set_bool_value(true);

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("unknown control") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch with wrong payload type for the addressed control is rejected",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeUiTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("fake");
    req.set_control_id("toggle1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    // Wrong payload variant for a Toggle (which expects bool_value).
    req.set_string_value("not a bool");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("payload type") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch EditorCommit to a ModalEditor runs handle_event",
          "[grpc][extension-ui][modal_editor]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeModalTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("modal");
    req.set_control_id("editor1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* commit = req.mutable_editor_commit();
    {
        auto* f = commit->add_fields();
        f->set_field_id("choice1");
        f->set_index_value(1);
    }
    {
        auto* f = commit->add_fields();
        f->set_field_id("text1");
        f->set_string_value("hello");
    }

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE(resp.accepted());
    REQUIRE(resp.error().empty());
    REQUIRE(fake_ptr->fake_ui().event_count() == 1);
    REQUIRE(fake_ptr->fake_ui().last_commit().fields_size() == 2);
}

TEST_CASE("Dispatch EditorCommit rejected when the ModalEditor is not editable",
          "[grpc][extension-ui][modal_editor]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeModalTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);
    fake_ptr->fake_ui().set_editable(false);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("modal");
    req.set_control_id("editor1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* commit = req.mutable_editor_commit();
    auto* f = commit->add_fields();
    f->set_field_id("text1");
    f->set_string_value("should be rejected");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("not editable") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch EditorCommit with unknown field_id is rejected",
          "[grpc][extension-ui][modal_editor]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeModalTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("modal");
    req.set_control_id("editor1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* commit = req.mutable_editor_commit();
    auto* f = commit->add_fields();
    f->set_field_id("no_such_field");
    f->set_string_value("bogus");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("unknown editor field") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch EditorCommit with wrong value variant for a sub-control is rejected",
          "[grpc][extension-ui][modal_editor]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeModalTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("modal");
    req.set_control_id("editor1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* commit = req.mutable_editor_commit();
    auto* f = commit->add_fields();
    f->set_field_id("text1");  // TextInput expects string_value
    f->set_index_value(7);     // Wrong variant

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("variant") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch with string_value targets an EditableChoice",
          "[grpc][extension-ui][editable_choice]") {
    // FakeUi's view tree only has a Toggle. Build a fresh transport
    // with an inline FakeEditableUi so this test stays self-contained.
    class FakeEditableUi : public beebium::ExtensionUi {
    public:
        void build_view(beebium::View* out) const override {
            auto* root = out->mutable_root();
            root->set_id("root");
            auto* group = root->mutable_group();
            auto* control = group->add_controls();
            control->set_id("ec1");
            auto* ec = control->mutable_editable_choice();
            ec->set_label("Pick");
            *ec->add_options() = "alpha";
            *ec->add_options() = "beta";
        }
        void handle_event(const beebium::DispatchRequest& req) override {
            last_value_ = req.string_value();
            ++event_count_;
        }
        const std::string& last_value() const { return last_value_; }
        int event_count() const { return event_count_.load(std::memory_order_acquire); }
    private:
        std::string last_value_;
        std::atomic<int> event_count_{0};
    };
    class FakeEditableTransport : public beebium::EconetTransportExtension {
    public:
        FakeEditableTransport() {
            beebium::ExtensionManifest m;
            m.name = "ec";
            m.cli_name = "ec";
            m.extension_kind = "econet-transport";
            set_manifest(std::move(m));
        }
        std::unique_ptr<beebium::NetworkBackend> create_backend(uint8_t) override {
            return nullptr;
        }
        beebium::ExtensionUi* ui() override { return &ui_; }
        FakeEditableUi& fake_ui() { return ui_; }
    private:
        FakeEditableUi ui_;
    };

    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeEditableTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("ec");
    req.set_control_id("ec1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    req.set_string_value("custom-typed");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE(resp.accepted());
    REQUIRE(fake_ptr->fake_ui().last_value() == "custom-typed");
    REQUIRE(fake_ptr->fake_ui().event_count() == 1);
}

TEST_CASE("Dispatch with index_value to an EditableChoice is rejected",
          "[grpc][extension-ui][editable_choice]") {
    // Same FakeEditableTransport pattern as the accept-test above; the
    // server should reject index_value because EditableChoice expects
    // string_value (the typed/picked text, not an option index).
    class FakeEditableUi : public beebium::ExtensionUi {
    public:
        void build_view(beebium::View* out) const override {
            auto* root = out->mutable_root();
            root->set_id("root");
            auto* group = root->mutable_group();
            auto* control = group->add_controls();
            control->set_id("ec1");
            auto* ec = control->mutable_editable_choice();
            *ec->add_options() = "a";
        }
        void handle_event(const beebium::DispatchRequest&) override {
            ++event_count_;
        }
        int event_count() const { return event_count_.load(std::memory_order_acquire); }
    private:
        std::atomic<int> event_count_{0};
    };
    class FakeEditableTransport : public beebium::EconetTransportExtension {
    public:
        FakeEditableTransport() {
            beebium::ExtensionManifest m;
            m.name = "ec";
            m.cli_name = "ec";
            m.extension_kind = "econet-transport";
            set_manifest(std::move(m));
        }
        std::unique_ptr<beebium::NetworkBackend> create_backend(uint8_t) override {
            return nullptr;
        }
        beebium::ExtensionUi* ui() override { return &ui_; }
        FakeEditableUi& fake_ui() { return ui_; }
    private:
        FakeEditableUi ui_;
    };

    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeEditableTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("ec");
    req.set_control_id("ec1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    req.set_index_value(0);  // wrong variant for EditableChoice

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("payload type") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Cancelling the SubscribeView context exits the server poll loop cleanly",
          "[grpc][extension-ui]") {
    beebium::EconetTransportRegistry registry;
    registry.add(std::make_unique<FakeUiTransport>());
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::SubscribeViewRequest req;
    req.set_extension_id("fake");
    auto reader = fixture.stub().SubscribeView(&ctx, req);

    // Receive at least the initial push.
    beebium::View view;
    REQUIRE(reader->Read(&view));

    auto t0 = std::chrono::steady_clock::now();
    ctx.TryCancel();
    while (reader->Read(&view)) {}
    auto status = reader->Finish();
    auto elapsed = std::chrono::steady_clock::now() - t0;

    // Loop should exit within a few poll intervals (~50ms each); allow
    // generous slack for slow CI machines.
    REQUIRE(elapsed < std::chrono::seconds(2));
    // Cancelled streams report CANCELLED to the client; OK is also
    // acceptable if the server happened to write+exit cleanly between
    // cancel signal and next poll.
    REQUIRE((status.error_code() == grpc::StatusCode::CANCELLED ||
             status.error_code() == grpc::StatusCode::OK));
}

// --- EditableList dispatch (EditableListEvent payload) ---

TEST_CASE("Dispatch ADD to an EditableList runs handle_event",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::ADD);
    auto* f = event->mutable_commit()->add_fields();
    f->set_field_id("add.host");
    f->set_string_value("192.168.1.9");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE(resp.accepted());
    REQUIRE(resp.error().empty());
    REQUIRE(fake_ptr->fake_ui().event_count() == 1);
    REQUIRE(fake_ptr->fake_ui().last_list_event().kind() ==
            beebium::EditableListEvent::ADD);
}

TEST_CASE("Dispatch ADD is rejected when the list does not allow adding",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    fake_ptr->fake_ui().set_can_add(false);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::ADD);

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("does not allow adding") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch EDIT to an editable item runs handle_event",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::EDIT);
    event->set_item_id("a");
    auto* f = event->mutable_commit()->add_fields();
    f->set_field_id("edit.a.host");
    f->set_string_value("10.0.0.9");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE(resp.accepted());
    REQUIRE(fake_ptr->fake_ui().event_count() == 1);
    REQUIRE(fake_ptr->fake_ui().last_list_event().item_id() == "a");
}

TEST_CASE("Dispatch EDIT with an unknown item id is rejected",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::EDIT);
    event->set_item_id("nope");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("unknown item id") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch EDIT to a non-editable item is rejected",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::EDIT);
    event->set_item_id("b");  // not editable

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("not editable") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch EDIT commit with an unknown field id is rejected",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::EDIT);
    event->set_item_id("a");
    auto* f = event->mutable_commit()->add_fields();
    f->set_field_id("edit.a.nope");  // not a field of item a's editor
    f->set_string_value("x");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("unknown editor field") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch REMOVE of a removable item runs handle_event",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::REMOVE);
    event->set_item_id("a");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE(resp.accepted());
    REQUIRE(fake_ptr->fake_ui().event_count() == 1);
}

TEST_CASE("Dispatch REMOVE of a non-removable item is rejected",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::REMOVE);
    event->set_item_id("b");  // not removable

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("not removable") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch ACTION with a known action id runs handle_event",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::ACTION);
    event->set_item_id("b");
    event->set_action_id("save");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE(resp.accepted());
    REQUIRE(fake_ptr->fake_ui().event_count() == 1);
    REQUIRE(fake_ptr->fake_ui().last_list_event().action_id() == "save");
}

TEST_CASE("Dispatch ACTION with an unknown action id is rejected",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    auto* event = req.mutable_editable_list_event();
    event->set_kind(beebium::EditableListEvent::ACTION);
    event->set_item_id("a");
    event->set_action_id("frobnicate");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("unknown action id") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

TEST_CASE("Dispatch with wrong payload type for an EditableList is rejected",
          "[grpc][extension-ui][editable_list]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("list1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    req.set_bool_value(true);  // wrong variant for an EditableList

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("payload type") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}

// --- FileReference dispatch (file_action_id payload) ---

TEST_CASE("Dispatch a known file action runs handle_event",
          "[grpc][extension-ui][file_reference]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("file1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    req.set_file_action_id("reload");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE(resp.accepted());
    REQUIRE(fake_ptr->fake_ui().event_count() == 1);
    REQUIRE(fake_ptr->fake_ui().last_file_action() == "reload");
}

TEST_CASE("Dispatch an unknown file action is rejected",
          "[grpc][extension-ui][file_reference]") {
    beebium::EconetTransportRegistry registry;
    auto fake = std::make_unique<FakeListTransport>();
    auto* fake_ptr = fake.get();
    registry.add(std::move(fake));
    ExtensionUiFixture fixture(registry);

    grpc::ClientContext ctx;
    beebium::DispatchRequest req;
    req.set_extension_id("list");
    req.set_control_id("file1");
    req.set_view_revision(fake_ptr->fake_ui().current_revision());
    req.set_file_action_id("destroy");

    beebium::DispatchResponse resp;
    REQUIRE(fixture.stub().Dispatch(&ctx, req, &resp).ok());
    REQUIRE_FALSE(resp.accepted());
    REQUIRE(resp.error().find("unknown file action") != std::string::npos);
    REQUIRE(fake_ptr->fake_ui().event_count() == 0);
}
