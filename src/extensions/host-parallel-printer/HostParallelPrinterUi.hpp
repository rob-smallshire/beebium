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

#ifndef BEEBIUM_EXT_HOST_PARALLEL_PRINTER_UI_HPP
#define BEEBIUM_EXT_HOST_PARALLEL_PRINTER_UI_HPP

#include <beebium/extension/ExtensionUi.hpp>

namespace beebium {

class HostParallelPrinterExtension;

// Peripherals-sidebar panel for the host printer: the device, whether
// it is open, and the byte counts. Read-only: online follows the open
// device, so there is nothing to set.
class HostParallelPrinterUi : public ExtensionUi {
public:
    explicit HostParallelPrinterUi(HostParallelPrinterExtension& ext) : ext_(ext) {}

    void build_view(View* out) const override;
    void handle_event(const DispatchRequest& request) override;

private:
    HostParallelPrinterExtension& ext_;
};

}  // namespace beebium

#endif  // BEEBIUM_EXT_HOST_PARALLEL_PRINTER_UI_HPP
