#include "common/rcf.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace hulk {
namespace {

uint32_t U32(const std::vector<uint8_t>& b, size_t at) {
    uint32_t v = 0;
    if (at + 4 <= b.size()) std::memcpy(&v, b.data() + at, 4);
    return v;
}

char Fold(char c) { return c == '/' ? '\\' : char(std::tolower(uint8_t(c))); }

}  // namespace

bool SamePath(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (Fold(a[i]) != Fold(b[i])) return false;
    return true;
}

uint64_t AlignRcf(uint64_t at) { return (at + kRcfAlign - 1) & ~uint64_t(kRcfAlign - 1); }

uint32_t Rcf::Offset(uint32_t entry) const { return U32(dir, entry * 12 + 4); }
uint32_t Rcf::Size(uint32_t entry) const { return U32(dir, entry * 12 + 8); }

void Rcf::Move(uint32_t entry, uint32_t offset, uint32_t size) {
    if (entry * 12 + 12 > dir.size()) return;
    std::memcpy(dir.data() + entry * 12 + 4, &offset, 4);
    std::memcpy(dir.data() + entry * 12 + 8, &size, 4);
    std::vector<const File*> order;
    for (const File& file : files) order.push_back(&file);
    std::sort(order.begin(), order.end(), [&](const File* a, const File* b) {
        if (Offset(a->entry) != Offset(b->entry)) return Offset(a->entry) < Offset(b->entry);
        if (Size(a->entry) != Size(b->entry)) return Size(a->entry) < Size(b->entry);
        return a->entry < b->entry;
    });
    // Preserve the name-to-hash-entry identity when a later view reparses this archive.
    // Records have unchanged total length; the header, name metadata and trailing padding stay intact.
    size_t at = 8;
    for (const File* file : order) {
        std::memcpy(names.data() + at, file->record.data(), file->record.size());
        at += file->record.size();
    }
}

std::optional<uint32_t> Rcf::Find(std::string_view name) const {
    for (const File& f : files)
        if (SamePath(f.name, name)) return f.entry;
    return std::nullopt;
}

std::optional<Rcf> ReadRcf(uint64_t size, const ReadFn& read) {
    auto bytes = [&](uint64_t at, size_t n) {
        std::vector<uint8_t> b(n);
        b.resize(read(at, b.data(), n));
        return b;
    };
    std::vector<uint8_t> head = bytes(0, 0x40);
    if (head.size() < 0x40 || std::memcmp(head.data(), "ATG CORE CEMENT LIBRARY", 23) != 0) return std::nullopt;
    uint32_t dir_at = U32(head, 0x24), names_at = U32(head, 0x2C), names_size = U32(head, 0x30), count = U32(head, 0x38);
    if (!count || count > 0x10000 || dir_at + uint64_t(count) * 12 > size || names_at + uint64_t(names_size) > size)
        return std::nullopt;
    Rcf rcf;
    rcf.dir_at = dir_at;
    rcf.dir = bytes(dir_at, count * 12);
    rcf.names_at = names_at;
    rcf.names = bytes(names_at, names_size);
    const auto& names = rcf.names;
    if (rcf.dir.size() != count * 12 || names.size() != names_size) return std::nullopt;

    // the names are in the files' order: the entries sorted by offset
    std::vector<uint32_t> order(count);
    for (uint32_t i = 0; i < count; i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
        uint32_t ox = rcf.Offset(x), oy = rcf.Offset(y);
        return ox != oy ? ox < oy : rcf.Size(x) < rcf.Size(y);
    });
    // a name: time, alignment, 0, length with its terminator, the name, 3 bytes of padding
    size_t p = 8;
    for (uint32_t i = 0; i < count; i++) {
        const size_t start = p;
        uint32_t len = U32(names, p + 12);
        p += 16;
        if (!len || p + uint64_t(len) + 3 > names.size()) return std::nullopt;
        rcf.files.push_back({std::string(reinterpret_cast<const char*>(names.data() + p), len - 1), order[i],
                            {names.begin() + start, names.begin() + p + len + 3}});
        p += len + 3;
    }
    return rcf;
}

}  // namespace hulk
