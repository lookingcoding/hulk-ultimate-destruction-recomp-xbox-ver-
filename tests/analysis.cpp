#include <cstdio>
#include <cstring>
#include "analysis.h"

int main() {
    xbe::Image image;
    image.base = 0x1000;
    image.entry = 0x1000;
    image.file.resize(0x1200, 0xCC);
    image.sections.push_back({".text", 4, 0x1000, 0x200, 0, 0x200});
    image.sections.push_back({".rdata", 0, 0x2000, 0x100, 0x1000, 0x100});
    image.sections.push_back({".data", 1, 0x3000, 0x100, 0x1100, 0x100});
    image.file[0] = 0xC3;
    // The first virtual method follows unvisited non-padding bytes. Its
    // vtable references, rather than a guessed prologue boundary, identify it.
    for (uint32_t at : {0x1100u, 0x1120u, 0x1140u, 0x1160u, 0x1180u, 0x11A0u}) {
        // jmp rel32 from the preceding function, not yet visited by analysis.
        image.file[at - 0x1000 - 5] = 0xE9;
        const int32_t displacement = 0x10;
        std::memcpy(image.file.data() + at - 0x1000 - 4, &displacement, 4);
        if (at == 0x1120) {
            image.file[at - 0x1000 - 2] = 0xEB;
            image.file[at - 0x1000 - 1] = 0x10;
        }
        const uint8_t method[] = {0x56, 0xB8, 42, 0, 0, 0, 0x5E, 0xC3};
        std::memcpy(image.file.data() + at - 0x1000, method, sizeof(method));
    }
    const uint32_t table[] = {0x1100, 0x1120, 0x1140};
    std::memcpy(image.file.data() + 0x1000, table, sizeof(table));
    const uint32_t declaredData = 0x1160;
    std::memcpy(image.file.data() + 0x1000 + sizeof(table), &declaredData, 4);
    const uint32_t writablePointers[] = {0x1180, 0x11A0};
    std::memcpy(image.file.data() + 0x1100, writablePointers, sizeof(writablePointers));
    Config config;
    config.code_sections = {".text"};
    config.gap_fill = false;
    config.data_ranges = {{0x1160, 0x1170}};
    Analysis analysis(image, config);
    analysis.Run();
    for (uint32_t entry : table) {
        if (!analysis.functions.count(entry)) {
            std::fprintf(stderr, "Missing vtable method %08X without preceding padding\n", entry);
            return 1;
        }
    }
    if (analysis.functions.count(0x1200) || analysis.functions.count(0x2000) ||
        analysis.functions.count(0x1160) || analysis.functions.count(0x1180) || analysis.functions.count(0x11A0)) {
        std::fprintf(stderr, "Discovered an entry outside the code section\n");
        return 1;
    }
    std::puts("Vtable method discovery passed.");
}
