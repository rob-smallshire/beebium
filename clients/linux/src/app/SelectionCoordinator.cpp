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

#include "app/SelectionCoordinator.hpp"

#include "video.pb.h"

#include <algorithm>

namespace beebium::linux_client {

namespace {

// A copy of nothing but line breaks (every row blank) is nothing to copy.
bool blank(const std::string& text) {
    return text.find_first_not_of('\n') == std::string::npos;
}

}  // namespace

SelectionCoordinator::SelectionCoordinator(SelectionServices services)
    : services_(std::move(services)) {}

SelectionCoordinator::~SelectionCoordinator() {
    // The window cancels before it goes; answers still in flight find this.
    *alive_ = false;
}

bool SelectionCoordinator::begin(ViewPoint point, Interpretation interpretation,
                                 const ScreenPresentation& presentation) {
    if (!services_.connected()) {
        return false;
    }
    const std::optional<ScreenContent> content = services_.live_content();
    if (!content) {
        return false;
    }
    const auto hit = PictureMapping(*content, presentation).frame_hit(point);
    if (!hit) {
        return false;
    }
    cancel();
    selecting_ = true;
    interpretation_ = interpretation;
    presentation_ = presentation;
    content_ = *content;
    view_anchor_ = view_focus_ = point;
    anchor_ = focus_ = hit->pixel;
    const unsigned selection = ++selection_generation_;
    services_.release_machine_keys();
    // Still at once, so nothing moves while the hold is asked for.
    services_.freeze();
    refresh();
    services_.hold([this, alive = alive_, release = services_.release,
                    selection](std::optional<HeldScreen> held) {
        if (!*alive) {
            if (held) {
                release(held->hold_id);
            }
            return;
        }
        on_held(selection, std::move(held));
    });
    return true;
}

void SelectionCoordinator::on_held(unsigned selection, std::optional<HeldScreen> held) {
    const bool current = selecting_ && selection == selection_generation_;
    if (!held) {
        // Never read the live screen behind a frozen picture.
        if (current) {
            cancel();
        }
        return;
    }
    if (!current) {
        services_.release(held->hold_id);
        return;
    }
    hold_id_ = held->hold_id;
    grid_ = std::move(held->grid);
    if (held->frame) {
        // The server's still may be a frame or two on from the one showing
        // when the drag began: show it, and put the drag on it.
        services_.show_still(*held->frame);
        content_ = screen_content(*held->frame);
        const PictureMapping still(content_, presentation_);
        if (const auto hit = still.frame_hit(view_anchor_)) {
            anchor_ = hit->pixel;
        }
        if (const auto hit = still.frame_hit(view_focus_)) {
            focus_ = hit->pixel;
        }
    }
    refresh();
    const std::vector<Interpretation> copies = std::move(pending_copies_);
    pending_copies_.clear();
    for (const Interpretation interpretation : copies) {
        copy(interpretation);
    }
}

void SelectionCoordinator::update(ViewPoint point) {
    if (!selecting_) {
        return;
    }
    const auto hit = PictureMapping(content_, presentation_).frame_hit(point);
    if (!hit) {
        return;
    }
    view_focus_ = point;
    focus_ = hit->pixel;
    refresh();
}

void SelectionCoordinator::set_interpretation(Interpretation interpretation) {
    if (!selecting_ || interpretation == interpretation_) {
        return;
    }
    interpretation_ = interpretation;
    refresh();
}

void SelectionCoordinator::set_presentation(const ScreenPresentation& presentation) {
    presentation_ = presentation;
    if (selecting_) {
        notify();
    }
}

void SelectionCoordinator::cancel() {
    if (!selecting_) {
        return;
    }
    if (hold_id_) {
        services_.release(*hold_id_);
    }
    selecting_ = false;
    hold_id_.reset();
    grid_ = {};
    range_.clear();
    text_.clear();
    pending_copies_.clear();
    ++selection_generation_;
    ++highlight_generation_;
    services_.resume();
    notify();
}

void SelectionCoordinator::copy(Interpretation interpretation) {
    if (selecting_) {
        if (!hold_id_) {
            pending_copies_.push_back(interpretation);
            return;
        }
        read_selection(interpretation, selection_generation_, highlight_generation_, true);
        return;
    }
    if (!services_.connected()) {
        services_.nothing_to_copy();
        return;
    }
    // No selection: the whole live screen, needing no hold. Rows and
    // columns of the whole screen are the same text; anywhere also reads
    // what sits off the grid.
    const Interpretation whole = interpretation == Interpretation::anywhere
                                     ? Interpretation::anywhere
                                     : Interpretation::rows;
    services_.read(screen_text_request(whole, services_.mode7_characters(), {}, {}),
                   [this, alive = alive_](ScreenReadResult result) {
                       if (!*alive) {
                           return;
                       }
                       if (const auto* failure = std::get_if<ScreenReadFailure>(&result)) {
                           services_.copy_failed(failure->message);
                           return;
                       }
                       deliver(std::get<ScreenReading>(result).text);
                   });
}

std::optional<PictureMapping> SelectionCoordinator::mapping() const {
    if (!selecting_) {
        return std::nullopt;
    }
    return PictureMapping(content_, presentation_);
}

SelectionOverlay SelectionCoordinator::overlay() const {
    SelectionOverlay overlay;
    const auto laid_out = mapping();
    if (!laid_out) {
        return overlay;
    }
    for (const PixelRect& rect : range_) {
        overlay.range.push_back(laid_out->view_rect(rect));
    }
    for (const PixelRect& rect : text_) {
        overlay.text.push_back(laid_out->view_rect(rect));
    }
    if (interpretation_ == Interpretation::anywhere) {
        overlay.marquee = laid_out->view_rect(bounding_rect(anchor_, focus_));
    }
    return overlay;
}

const ScreenBand* SelectionCoordinator::band() const {
    return selection_band(grid_, std::min(anchor_.y, focus_.y));
}

SelectionCoordinator::Request SelectionCoordinator::request_for(
    Interpretation interpretation) const {
    const ScreenBand* grid_band = band();
    switch (interpretation) {
    case Interpretation::rows:
        if (grid_band != nullptr) {
            const RowsFlow flow = rows_flow(anchor_, focus_, *grid_band, content_.width);
            return {flow.request_region, flow};
        }
        return {bounding_rect(anchor_, focus_), std::nullopt};
    case Interpretation::columns:
        return {snapped_rectangle(anchor_, focus_, grid_band), std::nullopt};
    case Interpretation::anywhere:
        break;
    }
    return {bounding_rect(anchor_, focus_), std::nullopt};
}

void SelectionCoordinator::refresh() {
    range_ = range_rects(interpretation_, anchor_, focus_, band(), content_.width);
    notify();
    if (hold_id_) {
        read_selection(interpretation_, selection_generation_, ++highlight_generation_, false);
    }
}

void SelectionCoordinator::read_selection(Interpretation interpretation, unsigned selection,
                                          unsigned highlight, bool copying) {
    const Request request = request_for(interpretation);
    const ScreenBand* grid_band = band();
    services_.read(
        screen_text_request(interpretation, services_.mode7_characters(), request.region,
                            hold_id_),
        [this, alive = alive_, selection, highlight, copying, flow = request.flow,
         grid_band = grid_band != nullptr ? *grid_band : ScreenBand{}](ScreenReadResult result) {
            if (!*alive || selection != selection_generation_) {
                return;
            }
            if (const auto* failure = std::get_if<ScreenReadFailure>(&result)) {
                if (failure->hold_lost) {
                    // The still has gone server-side: the selection with it.
                    cancel();
                    services_.hold_lost();
                } else if (copying) {
                    services_.copy_failed(failure->message);
                }
                return;
            }
            const ScreenReading& reading = std::get<ScreenReading>(result);
            std::string text = reading.text;
            std::vector<TextRun> runs = reading.runs;
            if (flow) {
                RowsText rows = trim_to_flow(reading.runs, grid_band, *flow);
                text = std::move(rows.text);
                runs = std::move(rows.runs);
            }
            if (copying) {
                deliver(text);
                return;
            }
            if (highlight != highlight_generation_) {
                return;  // the selection has moved on since
            }
            text_ = matched_cell_rects(runs);
            notify();
        });
}

void SelectionCoordinator::deliver(const std::string& text) {
    if (blank(text)) {
        services_.nothing_to_copy();
    } else {
        services_.write_clipboard(text);
    }
}

void SelectionCoordinator::notify() {
    if (services_.changed) {
        services_.changed();
    }
}

}  // namespace beebium::linux_client
