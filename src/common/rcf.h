#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Radical's cement libraries (.rcf), the archives on the game's disc. The header reads "ATG CORE CEMENT LIBRARY", then
// at 0x24 the directory's offset and size, the names' offset and size, and at 0x38 the file count. Directory entries
// are name hash, offset, size, sorted by hash; the names follow the files' order in the archive. A file moves by
// pointing its entry somewhere else: past the archive's end is fine, the game only reads what the directory says.
namespace hulk {

inline constexpr uint32_t kRcfAlign = 0x800;  // files start on a sector

// reads n bytes from at into dst, the count read
using ReadFn = std::function<size_t(uint64_t at, void* dst, size_t n)>;

struct Rcf {
    uint32_t dir_at = 0;
    std::vector<uint8_t> dir;  // the directory, 12 bytes an entry
    struct File {
        std::string name;  // as the archive spells it: art\copyright-en.p3d
        uint32_t entry;    // its directory entry
    };
    std::vector<File> files;  // in the archive's order

    uint32_t Offset(uint32_t entry) const;
    uint32_t Size(uint32_t entry) const;
    void Move(uint32_t entry, uint32_t offset, uint32_t size);  // in dir
    std::optional<uint32_t> Find(std::string_view name) const;  // any case, either slash
};

// the archive's directory and names, null when it is no cement library
std::optional<Rcf> ReadRcf(uint64_t size, const ReadFn& read);

// any case, '/' as '\'
bool SamePath(std::string_view a, std::string_view b);
uint64_t AlignRcf(uint64_t at);

}  // namespace hulk
