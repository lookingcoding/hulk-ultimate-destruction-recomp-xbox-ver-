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

// the hash a file's name has in the directory, the cement library's own: any case, either slash, a leading \ not
// counted, and every byte under 'a' raised to it with the letters
uint32_t HashName(std::string_view name);

// reads n bytes from at into dst, the count read
using ReadFn = std::function<size_t(uint64_t at, void* dst, size_t n)>;

struct Rcf {
    uint32_t dir_at = 0;
    std::vector<uint8_t> dir;  // the directory, 12 bytes an entry
    struct File {
        std::string name;  // as the archive spells it: art\copyright-en.p3d
        uint32_t entry;    // its directory entry
        uint32_t time = 0;  // the names block's stamp for it, kept when the names are written again
    };
    std::vector<File> files;  // in the archive's order, the ones added at the end
    std::vector<uint8_t> head;  // the archive's first 0x40 bytes, kept to write them again

    uint32_t Offset(uint32_t entry) const;
    uint32_t Size(uint32_t entry) const;
    void Move(uint32_t entry, uint32_t offset, uint32_t size);  // in dir
    std::optional<uint32_t> Find(std::string_view name) const;  // any case, either slash

    // a file the archive does not have yet: its entry, to read like any other's. WriteRcf places it
    // past the archive's end with the rest of what the mods bring
    uint32_t Add(std::string_view name, uint32_t size);

    std::vector<uint32_t> added;  // the entries Add made, in the order given; WriteRcf places them
};

// an archive with files added past its end: the bytes to append and the header to lay over the original's
struct RcfWritten {
    std::vector<uint8_t> dir;     // the whole directory, entries sorted by hash
    std::vector<uint8_t> names;   // every file's name, in the files' order by offset
    std::vector<uint8_t> header;  // the original's header, patched at 0x24 and 0x38
    uint64_t at = 0;              // where the appended bytes go: the archive's aligned end
    uint64_t data = 0;            // where the files' bytes start
    uint64_t size = 0;            // the archive's size with the files' last byte in
};

// writes an archive with the files given past its end, in the order given, each on its own sector: the
// entries (directory entry, the size its file takes there) moved into place, the directory sorted by hash,
// the names in the files' order, the header pointing at both. The files' bytes go at what `data` onwards
// says, in the order given (mods.cpp)
RcfWritten WriteRcf(Rcf& rcf, const std::vector<std::pair<uint32_t, uint32_t>>& entries, uint64_t size);

// the archive's directory and names, null when it is no cement library
std::optional<Rcf> ReadRcf(uint64_t size, const ReadFn& read);

// any case, '/' as '\'
bool SamePath(std::string_view a, std::string_view b);
uint64_t AlignRcf(uint64_t at);

}  // namespace hulk
