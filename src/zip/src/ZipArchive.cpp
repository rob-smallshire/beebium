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

// The DOS time and date every member is stamped with: 1980-01-01 12:00, the
// first day a zip can say, at noon so that a tool shifting it by a time zone
// still shows that day. miniz, built without time, writes zeros (a date tools
// read as before 1980), and with time would go through localtime.
constexpr std::uint16_t kDosTime = 12 << 11;  // 12:00:00
constexpr std::uint16_t kDosDate = (0 << 9) | (1 << 5) | 1;  // 1980, January, 1st

std::uint16_t get16(const std::vector<std::uint8_t>& data, std::size_t at) {
    return static_cast<std::uint16_t>(data.at(at) | (data.at(at + 1) << 8));
}
std::uint32_t get32(const std::vector<std::uint8_t>& data, std::size_t at) {
    return get16(data, at) | (static_cast<std::uint32_t>(get16(data, at + 2)) << 16);
}
void put16(std::vector<std::uint8_t>& data, std::size_t at, std::uint16_t value) {
    data.at(at) = static_cast<std::uint8_t>(value & 0xff);
    data.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}

// Stamps every member of the archive `data` (as write_zip writes it: no
// archive comment, no zip64) with kDosTime and kDosDate, in its central
// directory entry and its local header. The fields are outside the CRC.
void stamp_dos_times(std::vector<std::uint8_t>& data) {
    constexpr std::size_t kEndSize = 22;
    if (data.size() < kEndSize || get32(data, data.size() - kEndSize) != 0x06054b50) {
        throw ZipError("the archive written has no end record where expected");
    }
    const std::size_t end = data.size() - kEndSize;
    const std::size_t count = get16(data, end + 10);
    std::size_t entry = get32(data, end + 16);
    for (std::size_t i = 0; i < count; ++i) {
        if (get32(data, entry) != 0x02014b50) {
            throw ZipError("the archive written has no directory entry where expected");
        }
        put16(data, entry + 12, kDosTime);
        put16(data, entry + 14, kDosDate);
        const std::size_t local = get32(data, entry + 42);
        if (get32(data, local) != 0x04034b50) {
            throw ZipError("the archive written has no local header where expected");
        }
        put16(data, local + 10, kDosTime);
        put16(data, local + 12, kDosDate);
        entry += 46 + get16(data, entry + 28) + get16(data, entry + 30) + get16(data, entry + 32);
    }
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

    // Built in memory, so every member's time can be stamped before the
    // archive is written.
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        fail("cannot start the archive (" + miniz_error(zip) + ")");
    }
    for (const auto& member : members) {
        if (!mz_zip_writer_add_mem_ex(&zip, member.name.c_str(), member.data.data(),
                                      member.data.size(), nullptr, 0,
                                      MZ_DEFAULT_COMPRESSION, 0, 0)) {
            const auto error = miniz_error(zip);
            mz_zip_writer_end(&zip);
            fail("cannot add " + member.name + " (" + error + ")");
        }
    }
    void* buffer = nullptr;
    size_t size = 0;
    const bool finished = mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);
    const auto error = finished ? std::string() : miniz_error(zip);
    std::vector<std::uint8_t> archive;
    if (finished) {
        const auto* begin = static_cast<const std::uint8_t*>(buffer);
        archive.assign(begin, begin + size);
        zip.m_pFree(zip.m_pAlloc_opaque, buffer);  // finalizing gave it to us
    }
    mz_zip_writer_end(&zip);
    if (!finished) fail("cannot finish the archive (" + error + ")");
    stamp_dos_times(archive);

    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(archive.data()),
              static_cast<std::streamsize>(archive.size()));
    out.close();
    if (!out) fail("cannot write");
}

}  // namespace beebium::zip
