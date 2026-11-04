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

#include "app/ExtensionPanel.hpp"

#include "app/SessionExtensions.hpp"

#include <QVBoxLayout>

namespace beebium::linux_client {

ExtensionPanel::ExtensionPanel(SessionExtensions& extensions, const std::string& extension_id,
                               ExtensionViewWidget::Shares shares, QWidget* parent)
    : QWidget(parent), feed_(new ExtensionViewFeed(extensions, extension_id, this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    renderer_ = new ExtensionViewWidget(
        [&extensions](const beebium::DispatchRequest& request) { extensions.dispatch(request); },
        std::move(shares), this);
    layout->addWidget(renderer_);
    connect(feed_, &ExtensionViewFeed::changed, this, &ExtensionPanel::present);
    present();
}

ExtensionPanel::~ExtensionPanel() = default;

void ExtensionPanel::showEvent(QShowEvent* event) {
    feed_->set_watching(true);
    QWidget::showEvent(event);
}

void ExtensionPanel::hideEvent(QHideEvent* event) {
    feed_->set_watching(false);
    QWidget::hideEvent(event);
}

void ExtensionPanel::present() {
    if (const auto& view = feed_->view()) {
        renderer_->set_view(*view);
        renderer_->set_stale(feed_->stale());
        renderer_->setVisible(true);
    } else {
        // Nothing yet, or the extension has gone: no panel, and no error.
        renderer_->setVisible(false);
    }
}

}  // namespace beebium::linux_client
