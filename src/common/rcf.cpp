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

// the 8 bytes of a names block record past the file's name: the terminator and the padding
constexpr size_t kNameTail = 4;

}  // namespace

uint32_t HashName(std::string_view name) {
    uint32_t res = 0;
    for (unsigned char ch : name) {
        if (res == 0 && ch == '\\') continue;
        uint32_t c = ch < 'a' ? ch + ('a' - 'A') : ch;
        res = (res << 5) - res + c;
    }
    return res;
}

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
}

std::optional<uint32_t> Rcf::Find(std::string_view name) const {
    for (const File& f : files)
        if (SamePath(f.name, name)) return f.entry;
    return std::nullopt;
}

uint32_t Rcf::Add(std::string_view name, uint32_t size) {
    uint32_t entry = uint32_t(dir.size() / 12);
    uint32_t hash = HashName(name), at = 0;
    uint8_t made[12];
    std::memcpy(made, &hash, 4);
    std::memcpy(made + 4, &at, 4);
    std::memcpy(made + 8, &size, 4);
    dir.insert(dir.end(), made, made + 12);
    files.push_back({std::string(name), entry});
    added.push_back(entry);
    return entry;
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
    rcf.head = head;
    rcf.dir_at = dir_at;
    rcf.dir = bytes(dir_at, count * 12);
    std::vector<uint8_t> names = bytes(names_at, names_size);
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
        uint32_t len = U32(names, p + 12), time = U32(names, p);
        p += 16;
        if (!len || p + len > names.size()) return std::nullopt;
        rcf.files.push_back({std::string(reinterpret_cast<const char*>(names.data() + p), len - 1), order[i], time});
        p += len + 3;
    }
    return rcf;
}

RcfWritten WriteRcf(Rcf& rcf, const std::vector<std::pair<uint32_t, uint32_t>>& entries, uint64_t size) {
    RcfWritten w;
    w.at = AlignRcf(size);
    const uint32_t count = uint32_t(rcf.dir.size() / 12);
    // the files given go past the new directory and names, each on its own sector
    uint64_t names_size = 8;
    for (const Rcf::File& f : rcf.files) names_size += 16 + f.name.size() + 1 + 3;
    w.data = AlignRcf(w.at + uint64_t(count) * 12 + names_size);
    uint64_t end = w.data;
    for (const auto& [entry, file_size] : entries) {
        rcf.Move(entry, uint32_t(end), file_size);
        w.size = end + file_size;
        end = AlignRcf(end + file_size);
    }
    // the directory: every entry, sorted by hash as the game looks files up
    std::vector<uint32_t> by_hash(count), by_offset(count);
    for (uint32_t i = 0; i < count; i++) by_hash[i] = by_offset[i] = i;
    std::stable_sort(by_hash.begin(), by_hash.end(),
                     [&](uint32_t a, uint32_t b) { return U32(rcf.dir, a * 12) < U32(rcf.dir, b * 12); });
    w.dir.resize(size_t(count) * 12);
    for (uint32_t i = 0; i < count; i++)
        std::memcpy(w.dir.data() + size_t(i) * 12, rcf.dir.data() + size_t(by_hash[i]) * 12, 12);
    // the names: in the files' new order, the same one ReadRcf pairs them by
    std::stable_sort(by_offset.begin(), by_offset.end(), [&](uint32_t a, uint32_t b) {
        uint32_t oa = rcf.Offset(a), ob = rcf.Offset(b);
        return oa != ob ? oa < ob : rcf.Size(a) < rcf.Size(b);
    });
    std::vector<const Rcf::File*> file_of(count, nullptr);
    for (const Rcf::File& f : rcf.files)
        if (f.entry < count) file_of[f.entry] = &f;
    w.names.assign(8, 0);
    uint32_t alignment = 0x0800;
    std::memcpy(w.names.data(), &alignment, 4);
    for (uint32_t i = 0; i < count; i++) {
        const Rcf::File* f = file_of[by_offset[i]];
        static const Rcf::File none{};
        if (!f) f = &none;
        uint32_t len = uint32_t(f->name.size()) + 1;
        uint8_t record[16] = {};
        std::memcpy(record, &f->time, 4);
        std::memcpy(record + 4, &alignment, 4);
        std::memcpy(record + 12, &len, 4);
        w.names.insert(w.names.end(), record, record + 16);
        w.names.insert(w.names.end(), f->name.begin(), f->name.end());
        w.names.insert(w.names.end(), kNameTail, 0);  // the terminator and the padding
    }
    // the header: the original's, pointing at the directory and the names past the end
    w.header = rcf.head;
    if (w.header.size() >= 0x40) {
        uint32_t at32 = uint32_t(w.at), dir_size = count * 12, names_at = uint32_t(w.at + uint64_t(count) * 12),
                 names32 = uint32_t(w.names.size());
        std::memcpy(w.header.data() + 0x24, &at32, 4);
        std::memcpy(w.header.data() + 0x28, &dir_size, 4);
        std::memcpy(w.header.data() + 0x2C, &names_at, 4);
        std::memcpy(w.header.data() + 0x30, &names32, 4);
        std::memcpy(w.header.data() + 0x38, &count, 4);
    }
    return w;
}

}  // namespace hulk
