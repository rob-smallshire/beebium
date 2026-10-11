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

// Naming extensions that a test registers directly.
//
// The registries require every extension to have an id: the server names each
// instance once its configuration is resolved (assign_extension_ids), giving
// the only instance of an extension its name. A test that builds extensions
// itself names them the same way with with_default_id, which leaves an id the
// test gave explicitly alone.

#ifndef BEEBIUM_TESTS_TEST_EXTENSION_IDS_HPP
#define BEEBIUM_TESTS_TEST_EXTENSION_IDS_HPP

#include <beebium/extension/Extension.hpp>

#include <memory>
#include <string>

template <typename E>
std::unique_ptr<E> with_default_id(std::unique_ptr<E> extension) {
    if (extension->id().empty()) {
        extension->set_config_value("id", std::string(extension->name()));
    }
    return extension;
}

#endif  // BEEBIUM_TESTS_TEST_EXTENSION_IDS_HPP
