#include <Zydis/Zydis.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include "xbe/xbe.h"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: hulk_disasm game.xbe address byte-count\n");
        return 1;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) return 1;
    xbe::Image image;
    std::string error;
    if (!image.Parse(std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {}), error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    uint32_t address = uint32_t(std::strtoul(argv[2], nullptr, 0));
    uint64_t end = uint64_t(address) + std::strtoul(argv[3], nullptr, 0);
    while (uint64_t(address) < end) {
        int64_t offset = image.Offset(address);
        if (offset < 0 || uint64_t(offset) >= image.file.size()) return 1;
        ZydisDisassembledInstruction instruction;
        auto available = image.file.size() - size_t(offset);
        if (!ZYAN_SUCCESS(ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LEGACY_32, address,
                image.file.data() + offset, available, &instruction))) {
            std::printf("%08X  db %02X\n", address, image.file[size_t(offset)]);
            address++;
            continue;
        }
        std::printf("%08X  %s\n", address, instruction.text);
        address += instruction.info.length;
    }
}
