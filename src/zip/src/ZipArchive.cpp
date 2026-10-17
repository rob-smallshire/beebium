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

#include "beebium/zip/ZipArchive.hpp"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <utility>

namespace beebium::zip {

namespace {

std::string miniz_error(mz_zip_archive& zip) {
    return mz_zip_get_error_string(mz_zip_get_last_error(&zip));
}

// miniz reads an archive through this, so the file is opened by its path
// object (wide on Windows), not by a narrow name.
size_t read_from_stream(void* opaque, mz_uint64 offset, void* buffer, size_t size) {
    auto& in = *static_cast<std::ifstream*>(opaque);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset));
    in.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return static_cast<size_t>(in.gcount());
}

size_t write_to_stream(void* opaque, mz_uint64 offset, const void* buffer, size_t size) {
    auto& out = *static_cast<std::ofstream*>(opaque);
    out.seekp(static_cast<std::streamoff>(offset));
    out.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(size));
    return out ? size : 0;
}

}  // namespace

bool is_valid_member_name(std::string_view name) {
    if (name.empty() || name.front() == '/') return false;
    if (name.find('\\') != std::string_view::npos) return false;
    if (name.size() >= 2 && name[1] == ':' &&
        std::isalpha(static_cast<unsigned char>(name[0]))) {
        return false;
    }
    // A directory entry's trailing '/' ends it; no other component is empty.
    if (name.back() == '/') name.remove_suffix(1);
    std::size_t start = 0;
    while (true) {
        const auto end = name.find('/', start);
        const auto component = name.substr(start, end - start);
        if (component.empty() || component == "." || component == "..") return false;
        if (end == std::string_view::npos) return true;
        start = end + 1;
    }
}

struct ZipReader::Impl {
    std::filesystem::path filepath;
    std::ifstream in;
    mz_zip_archive zip{};
    std::vector<std::string> member_names;
    std::vector<mz_uint> member_indexes;

    ~Impl() { mz_zip_reader_end(&zip); }

    [[noreturn]] void fail(const std::string& what) const {
        throw ZipError(filepath.string() + ": " + what);
    }

    mz_uint index_of(std::string_view name) const {
        for (std::size_t i = 0; i < member_names.size(); ++i) {
            if (member_names[i] == name) return member_indexes[i];
        }
        fail("no member " + std::string(name));
    }
};

ZipReader::ZipReader(const std::filesystem::path& filepath) : impl_(std::make_unique<Impl>()) {
    impl_->filepath = filepath;
    impl_->in.open(filepath, std::ios::binary);
    if (!impl_->in) impl_->fail("cannot open");
    impl_->in.seekg(0, std::ios::end);
    const auto size = static_cast<mz_uint64>(impl_->in.tellg());
    impl_->zip.m_pRead = read_from_stream;
    impl_->zip.m_pIO_opaque = &impl_->in;
    if (!mz_zip_reader_init(&impl_->zip, size, 0)) {
        impl_->fail("not a zip archive (" + miniz_error(impl_->zip) + ")");
    }
    const mz_uint count = mz_zip_reader_get_num_files(&impl_->zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&impl_->zip, i, &stat)) {
            impl_->fail("cannot read the archive's directory (" + miniz_error(impl_->zip) + ")");
        }
        const std::string name = stat.m_filename;
        if (!is_valid_member_name(name)) {
            impl_->fail("member name '" + name + "' is not a relative path within the archive");
        }
        if (stat.m_is_directory) continue;
        if (std::find(impl_->member_names.begin(), impl_->member_names.end(), name) !=
            impl_->member_names.end()) {
            impl_->fail("two members are called " + name);
        }
        if (!stat.m_is_supported) {
            impl_->fail("member " + name + " is stored in a way that cannot be read");
        }
        impl_->member_names.push_back(name);
        impl_->member_indexes.push_back(i);
    }
}

ZipReader::~ZipReader() = default;
ZipReader::ZipReader(ZipReader&&) noexcept = default;
ZipReader& ZipReader::operator=(ZipReader&&) noexcept = default;

const std::vector<std::string>& ZipReader::member_names() const { return impl_->member_names; }

bool ZipReader::contains(std::string_view name) const {
    const auto& names = impl_->member_names;
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::vector<std::uint8_t> ZipReader::read(std::string_view name) const {
    const auto index = impl_->index_of(name);
    mz_zip_archive_file_stat stat{};
    if (!mz_zip_reader_file_stat(&impl_->zip, index, &stat)) {
        impl_->fail("cannot read member " + std::string(name));
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(stat.m_uncomp_size));
    if (!mz_zip_reader_extract_to_mem(&impl_->zip, index, data.data(), data.size(), 0)) {
        impl_->fail("cannot read member " + std::string(name) + " (" +
                    miniz_error(impl_->zip) + ")");
    }
    return data;
}

void ZipReader::extract(std::string_view name, const std::filesystem::path& dest_filepath) const {
    const auto index = impl_->index_of(name);
    std::ofstream out(dest_filepath, std::ios::binary | std::ios::trunc);
    if (!out) impl_->fail("cannot write " + dest_filepath.string());
    if (!mz_zip_reader_extract_to_callback(&impl_->zip, index, write_to_stream, &out, 0)) {
        impl_->fail("cannot extract member " + std::string(name) + " (" +
                    miniz_error(impl_->zip) + ")");
    }
    out.close();
    if (!out) impl_->fail("cannot write " + dest_filepath.string());
}

void write_zip(const std::filesystem::path& filepath, std::vector<ZipMember> members) {
    auto fail = [&](const std::string& what) -> void {
        throw ZipError(filepath.string() + ": " + what);
    };
    std::sort(members.begin(), members.end(),
              [](const ZipMember& a, const ZipMember& b) { return a.name < b.name; });
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (!is_valid_member_name(members[i].name) || members[i].name.back() == '/') {
            fail("'" + members[i].name + "' is not a valid member name");
        }
        if (i > 0 && members[i].name == members[i - 1].name) {
            fail("two members are called " + members[i].name);
        }
    }

    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    if (!out) fail("cannot write");
    mz_zip_archive zip{};
    zip.m_pWrite = write_to_stream;
    zip.m_pIO_opaque = &out;
    if (!mz_zip_writer_init(&zip, 0)) fail("cannot start the archive (" + miniz_error(zip) + ")");
    for (const auto& member : members) {
        if (!mz_zip_writer_add_mem_ex(&zip, member.name.c_str(), member.data.data(),
                                      member.data.size(), nullptr, 0,
                                      MZ_DEFAULT_COMPRESSION, 0, 0)) {
            const auto error = miniz_error(zip);
            mz_zip_writer_end(&zip);
            fail("cannot add " + member.name + " (" + error + ")");
        }
    }
    const bool finished = mz_zip_writer_finalize_archive(&zip);
    const auto error = finished ? std::string() : miniz_error(zip);
    mz_zip_writer_end(&zip);
    if (!finished) fail("cannot finish the archive (" + error + ")");
    out.close();
    if (!out) fail("cannot write");
}

}  // namespace beebium::zip
