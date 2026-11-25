// The actor update's owner guard: it skips to the game's cleanup only when the owner the guest is about to read
// through is null, wherever the hook sits, and never because of a stale eax (common/actor_owner_guard.h).
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
#include <toml++/toml.hpp>
#include "common/actor_owner_guard.h"
#include "common/addresses.h"

namespace {
enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };

struct Memory {
    std::map<uint32_t, uint32_t> words;
    uint32_t operator()(uint32_t a) const { auto it = words.find(a); return it == words.end() ? 0xCDCDCDCD : it->second; }
};

hulk::OwnerGuardPlan Plan(std::vector<uint8_t> code) {
    code.resize(16, 0x90);  // the hook reads 16 bytes: pad with nops
    return hulk::PlanOwnerGuard(code.data(), code.size());
}

int failures = 0;
void Check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}
}  // namespace

int main(int argc, char** argv) {
    using hulk::OwnerGuardRule;
    const Memory none;

    // the hook on the read itself: mov ecx, [eax+0xEC]
    {
        const auto plan = Plan({0x8B, 0x88, 0xEC, 0x00, 0x00, 0x00});
        Check(plan.rule == OwnerGuardRule::OwnerRegister && plan.owner_reg == EAX, "read through eax");
        uint32_t live[8] = {0x00401000, 0, 0, 0, 0x0010F000, 0, 0x00402000, 0};
        Check(!hulk::OwnerGuardSkips(plan, live, none), "live owner in eax runs the lookup");
        uint32_t gone[8] = {0, 0, 0, 0, 0x0010F000, 0, 0x00402000, 0};
        Check(hulk::OwnerGuardSkips(plan, gone, none), "null owner in eax skips to the cleanup");
    }
    // the read through another register: mov ecx, [esi+0xEC]. A zero left in eax by the callbacks must not skip.
    {
        const auto plan = Plan({0x8B, 0x8E, 0xEC, 0x00, 0x00, 0x00});
        Check(plan.rule == OwnerGuardRule::OwnerRegister && plan.owner_reg == ESI, "read through esi");
        uint32_t stale_eax[8] = {0, 0, 0, 0, 0x0010F000, 0, 0x00402000, 0};
        Check(!hulk::OwnerGuardSkips(plan, stale_eax, none), "stale eax with a live owner in esi runs the lookup");
        uint32_t gone[8] = {1, 0, 0, 0, 0x0010F000, 0, 0, 0};
        Check(hulk::OwnerGuardSkips(plan, gone, none), "null owner in esi skips");
    }
    // the hook on the reload: mov eax, [esi+0x10]; mov ecx, [eax+0xEC]. eax still holds the callbacks' result.
    {
        const auto plan = Plan({0x8B, 0x46, 0x10, 0x8B, 0x88, 0xEC, 0x00, 0x00, 0x00});
        Check(plan.rule == OwnerGuardRule::OwnerLoad && plan.owner_reg == EAX && plan.load_base == ESI &&
                  plan.load_disp == 0x10, "reload from [esi+0x10]");
        Memory live; live.words[0x00402010] = 0x00403000;
        uint32_t stale_eax[8] = {0, 0, 0, 0, 0x0010F000, 0, 0x00402000, 0};
        Check(!hulk::OwnerGuardSkips(plan, stale_eax, live), "stale eax before a live reload runs the update");
        Check(stale_eax[EAX] == 0, "a guard that does not skip leaves the registers alone");
        Memory gone; gone.words[0x00402010] = 0;
        uint32_t regs[8] = {1, 0, 0, 0, 0x0010F000, 0, 0x00402000, 0};
        Check(hulk::OwnerGuardSkips(plan, regs, gone), "a reload that finds no owner skips");
        Check(regs[EAX] == 0 && regs[ESI] == 0x00402000, "the skipped reload leaves its null owner in eax");
    }
    // a reload from the stack and a compare: mov edx, [esp+0x14]; cmp dword [edx+0xEC], 0
    {
        const auto plan = Plan({0x8B, 0x54, 0x24, 0x14, 0x83, 0xBA, 0xEC, 0x00, 0x00, 0x00, 0x00});
        Check(plan.rule == OwnerGuardRule::OwnerLoad && plan.owner_reg == EDX && plan.load_base == ESP &&
                  plan.load_disp == 0x14, "reload from [esp+0x14]");
        Memory live; live.words[0x0010F014] = 0x00403000;
        uint32_t regs[8] = {0, 0, 0, 0, 0x0010F000, 0, 0, 0};
        Check(!hulk::OwnerGuardSkips(plan, regs, live), "live stack owner runs the update");
    }
    // a reload of a global into eax: mov eax, [abs]; lea ecx, [eax+0xEC]
    {
        const auto plan = Plan({0xA1, 0x00, 0x2B, 0x4E, 0x00, 0x8D, 0x88, 0xEC, 0x00, 0x00, 0x00});
        Check(plan.rule == OwnerGuardRule::OwnerLoad && plan.owner_reg == EAX && plan.load_base == -1 &&
                  uint32_t(plan.load_disp) == 0x004E2B00, "reload from a global");
        Memory gone; gone.words[0x004E2B00] = 0;
        uint32_t regs[8] = {5, 0, 0, 0, 0x0010F000, 0, 0, 0};
        Check(hulk::OwnerGuardSkips(plan, regs, gone) && regs[EAX] == 0, "null global owner skips");
    }
    // the read itself through a reload's base register is still a read, not a reload: mov eax, [esi+0xEC]; test eax, eax
    {
        const auto plan = Plan({0x8B, 0x86, 0xEC, 0x00, 0x00, 0x00, 0x85, 0xC0});
        Check(plan.rule == OwnerGuardRule::OwnerRegister && plan.owner_reg == ESI, "read of [esi+0xEC]");
    }
    // a reload from [esi+0xEC] followed by the read of [eax+0xEC]: the reload wins
    {
        const auto plan = Plan({0x8B, 0x86, 0xEC, 0x00, 0x00, 0x00, 0x8B, 0x88, 0xEC, 0x00, 0x00, 0x00});
        Check(plan.rule == OwnerGuardRule::OwnerLoad && plan.owner_reg == EAX && plan.load_base == ESI,
              "reload then read, both at +0xEC");
    }
    // a one-byte displacement of 0xEC is -0x14, not the owner's field
    {
        const auto plan = Plan({0x8B, 0x48, 0xEC});
        Check(plan.rule == OwnerGuardRule::Unknown, "disp8 0xEC is not +0xEC");
    }
    // a scaled index is not the owner's field
    {
        const auto plan = Plan({0x8B, 0x8C, 0x86, 0xEC, 0x00, 0x00, 0x00});
        Check(plan.rule == OwnerGuardRule::Unknown, "indexed access");
    }
    // anything else keeps v0.5.3's test of eax
    {
        const auto plan = Plan({0x90});
        Check(plan.rule == OwnerGuardRule::Unknown, "nops");
        uint32_t zero[8] = {0, 0, 0, 0, 0x0010F000, 0, 0, 0};
        uint32_t live[8] = {1, 0, 0, 0, 0x0010F000, 0, 0, 0};
        Check(hulk::OwnerGuardSkips(plan, zero, none) && !hulk::OwnerGuardSkips(plan, live, none), "unknown tests eax");
    }
    // bytes running out never read past the end
    {
        const uint8_t cut[] = {0x8B, 0x88, 0xEC, 0x00};
        Check(hulk::PlanOwnerGuard(cut, sizeof cut).rule == OwnerGuardRule::Unknown, "truncated displacement");
        const uint8_t sib[] = {0x8B, 0x54};
        Check(hulk::PlanOwnerGuard(sib, sizeof sib).rule == OwnerGuardRule::Unknown, "truncated SIB");
    }

    // the address the hook reads its code from is where this region's config puts the hook
    if (argc > 1) {
        bool found = false;
        try {
            const toml::table config = toml::parse_file(argv[1]);
            if (const toml::array* hooks = config["hook"].as_array()) {
                for (const toml::node& node : *hooks) {
                    const toml::table* hook = node.as_table();
                    if (!hook || (*hook)["call"].value_or(std::string()) != "HasNoActorUpdateOwner") continue;
                    found = true;
                    const int64_t at = (*hook)["at"].value_or(int64_t(-1));
                    Check(at == int64_t(hulk::addr::kActorOwnerGuard), "config hook address matches kActorOwnerGuard");
                }
            }
        } catch (const toml::parse_error& e) {
            std::printf("FAIL: %s: %s\n", argv[1], e.what());
            ++failures;
        }
        Check(found, "the config has the HasNoActorUpdateOwner hook");
    }

    if (failures) return 1;
    std::printf("actor owner guard: all checks passed\n");
    return 0;
}
