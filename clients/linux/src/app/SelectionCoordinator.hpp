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

#ifndef BEEBIUM_LINUX_APP_SELECTION_COORDINATOR_HPP
#define BEEBIUM_LINUX_APP_SELECTION_COORDINATOR_HPP

#include "connection/ScreenReading.hpp"
#include "connection/ScreenSelection.hpp"
#include "display/PictureMapping.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace beebium {
class GetScreenTextRequest;
}

namespace beebium::linux_client {

// Selecting text on the screen and copying it (client specification 6.5),
// as the macOS client's SelectionCoordinator: a selection holds the screen
// and shows the held still while the emulator runs on, the highlight
// follows the drag and the interpretation, and a copy reads the held still
// in the interpretation asked for. With no selection, a copy reads the
// whole live screen, aligned. Free of any toolkit: the window answers
// through these seams.
struct SelectionServices {
    using HoldDone = std::function<void(std::optional<HeldScreen> held)>;
    using ReadDone = std::function<void(ScreenReadResult result)>;

    // Whether the calls below can be made; none of them answers otherwise.
    std::function<bool()> connected;
    std::function<void(HoldDone done)> hold;
    std::function<void(const beebium::GetScreenTextRequest& request, ReadDone done)> read;
    std::function<void(std::uint64_t hold_id)> release;

    // The shape of the frame now showing, when there is one.
    std::function<std::optional<ScreenContent>()> live_content;
    // Stop showing the live stream, show a still in its place, show the
    // live stream again.
    std::function<void()> freeze;
    std::function<void(const DecodedFrame& still)> show_still;
    std::function<void()> resume;

    // Let go of every machine key held, as the selection now owns the
    // qualifier modifiers and their releases will not reach the machine.
    std::function<void()> release_machine_keys;

    // How Mode 7 copies, read afresh at each copy.
    std::function<Mode7Characters()> mode7_characters;
    std::function<void(const std::string& text)> write_clipboard;
    // A copy found nothing to copy, or failed in these words (empty when
    // nothing was found).
    std::function<void(const std::string& reason)> nothing_copied;

    // The overlay has something new to show.
    std::function<void()> changed;
};

// What the overlay draws, in view units, back to front: the selected range,
// the cells that will copy, and the dragged marquee (anywhere only).
struct SelectionOverlay {
    std::vector<ViewRect> range;
    std::vector<ViewRect> text;
    std::optional<ViewRect> marquee;
};

class SelectionCoordinator {
public:
    explicit SelectionCoordinator(SelectionServices services);
    ~SelectionCoordinator();

    SelectionCoordinator(const SelectionCoordinator&) = delete;
    SelectionCoordinator& operator=(const SelectionCoordinator&) = delete;

    // Start a selection where the drag began, releasing any earlier one's
    // hold. Refused, with nothing frozen, outside the picture or with no
    // connection or no frame.
    bool begin(ViewPoint point, Interpretation interpretation,
               const ScreenPresentation& presentation);
    // The drag moved; a point outside the picture leaves the focus where it
    // was.
    void update(ViewPoint point);
    void set_interpretation(Interpretation interpretation);
    // The window resized or its video settings changed: the same still is
    // laid out afresh.
    void set_presentation(const ScreenPresentation& presentation);
    // End the selection, release its hold and show the live stream again.
    // Harmless when there is none.
    void cancel();

    // Copy in an interpretation: the selection's, read from the held
    // still, or with no selection the whole live screen, aligned.
    void copy(Interpretation interpretation);

    bool selecting() const { return selecting_; }
    Interpretation interpretation() const { return interpretation_; }
    std::optional<std::uint64_t> hold_id() const { return hold_id_; }
    FramePoint anchor() const { return anchor_; }
    FramePoint focus() const { return focus_; }
    // The geometry the overlay is laid out by: the held content at the
    // live presentation.
    std::optional<PictureMapping> mapping() const;
    SelectionOverlay overlay() const;

private:
    struct Request {
        std::optional<PixelRect> region;
        std::optional<RowsFlow> flow;  // rows on a grid, trimmed here
    };

    void on_held(unsigned selection, std::optional<HeldScreen> held);
    const ScreenBand* band() const;
    Request request_for(Interpretation interpretation) const;
    void refresh();
    void read_selection(Interpretation interpretation, unsigned selection, unsigned highlight,
                        bool copying);
    void notify();

    SelectionServices services_;
    bool selecting_ = false;
    Interpretation interpretation_ = Interpretation::rows;
    ScreenPresentation presentation_;
    ScreenContent content_;  // the held still's once it is shown
    ScreenGrid grid_;
    std::optional<std::uint64_t> hold_id_;
    ViewPoint view_anchor_;
    ViewPoint view_focus_;
    FramePoint anchor_;
    FramePoint focus_;
    std::vector<PixelRect> range_;
    std::vector<PixelRect> text_;
    std::vector<Interpretation> pending_copies_;  // asked before the hold came
    unsigned selection_generation_ = 0;
    unsigned highlight_generation_ = 0;
    // Answers outlive neither the coordinator nor a cancelled selection.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_SELECTION_COORDINATOR_HPP
