#pragma once
#include <cstddef>
#include <cstdint>

// The actor update's owner guard (game/hooks.cpp, HasNoActorUpdateOwner). After its callbacks the update reloads the
// actor's owner and reads [owner+0xEC] without checking it; the guard branches to the game's cleanup when that owner
// is null (the Lightning Strike substation crash). v0.5.3 tested eax there, which is only the owner when the hook sits
// on the read itself. Read the guest's own instructions at the hook site instead and test the pointer the code is
// about to dereference, so a stale eax left by the callbacks can never skip the rest of a live actor's update.
namespace hulk {

inline constexpr int32_t kActorOwnerAttachment = 0xEC;  // the owner field the update reads without a check

enum class OwnerGuardRule : uint8_t {
    Unknown,        // neither shape below: keep the v0.5.3 test of eax
    OwnerRegister,  // the site reads [reg+0xEC]: the owner is in reg
    OwnerLoad,      // the site loads the owner into reg, the next instruction reads [reg+0xEC]
};

struct OwnerGuardPlan {
    OwnerGuardRule rule = OwnerGuardRule::Unknown;
    uint8_t owner_reg = 0;     // x86 register number: eax ecx edx ebx esp ebp esi edi
    int8_t load_base = -1;     // OwnerLoad: the load's base register, -1 for an absolute address
    int32_t load_disp = 0;     // OwnerLoad: the load's displacement
};

struct OwnerGuardOperand {
    bool memory = false;
    bool index = false;     // a scaled index: not a plain field access
    uint8_t reg = 0;        // ModRM reg field
    int8_t base = -1;       // memory base register (-1 absolute), or the register operand
    int32_t disp = 0;
    uint8_t length = 0;     // ModRM, SIB and displacement bytes
};

// ModRM, SIB and displacement at p, 32-bit addressing. False when the bytes run out.
inline bool DecodeOwnerGuardModRm(const uint8_t* p, size_t n, OwnerGuardOperand& o) {
    if (n < 1) return false;
    const uint8_t m = p[0], mod = m >> 6;
    uint8_t rm = m & 7;
    o = {};
    o.reg = (m >> 3) & 7;
    o.length = 1;
    if (mod == 3) {
        o.base = int8_t(rm);
        return true;
    }
    o.memory = true;
    bool absolute = false;
    if (rm == 4) {
        if (n < 2) return false;
        const uint8_t sib = p[1];
        o.length = 2;
        o.index = ((sib >> 3) & 7) != 4;
        rm = sib & 7;
        absolute = rm == 5 && mod == 0;
    } else {
        absolute = rm == 5 && mod == 0;
    }
    o.base = absolute ? int8_t(-1) : int8_t(rm);
    const size_t disp_size = mod == 1 ? 1 : (mod == 2 || absolute) ? 4 : 0;
    if (n < o.length + disp_size) return false;
    const uint8_t* d = p + o.length;
    if (disp_size == 1) o.disp = int8_t(d[0]);
    if (disp_size == 4) o.disp = int32_t(uint32_t(d[0]) | uint32_t(d[1]) << 8 | uint32_t(d[2]) << 16 | uint32_t(d[3]) << 24);
    o.length = uint8_t(o.length + disp_size);
    return true;
}

// Offset of the ModRM byte for the instructions that read or write a 32-bit address through it, else 0.
inline size_t OwnerGuardModRmOffset(const uint8_t* p, size_t n) {
    if (n < 2) return 0;
    const uint8_t op = p[0];
    if (op < 0x40 && (op & 7) < 4) return 1;  // add or adc and sbb sub xor cmp, r/m forms
    switch (op) {
    case 0x69: case 0x6B:                              // imul r, r/m, imm
    case 0x80: case 0x81: case 0x83:                   // group 1 with an immediate
    case 0x84: case 0x85: case 0x86: case 0x87:        // test, xchg
    case 0x88: case 0x89: case 0x8A: case 0x8B:        // mov
    case 0x8D:                                         // lea: the address of the field, read later
    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3:  // shifts
    case 0xC6: case 0xC7:                              // mov r/m, imm
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:  // x87
    case 0xF6: case 0xF7: case 0xFE: case 0xFF:        // test imm, inc dec push call jmp
        return 1;
    case 0x0F: {
        const uint8_t op2 = p[1];
        if ((op2 & 0xF0) == 0x40 || op2 == 0xAF || op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF) return 2;
        return 0;
    }
    default:
        return 0;
    }
}

// Decode the memory operand of the instruction at p, if it has one.
inline bool DecodeOwnerGuardMemory(const uint8_t* p, size_t n, OwnerGuardOperand& o) {
    const size_t at = OwnerGuardModRmOffset(p, n);
    return at && DecodeOwnerGuardModRm(p + at, n - at, o) && o.memory;
}

inline bool ReadsOwnerAttachment(const OwnerGuardOperand& o, uint8_t reg) {
    return o.memory && !o.index && o.base == int8_t(reg) && o.disp == kActorOwnerAttachment;
}

// How to find the owner at the hook site, from the guest's instruction bytes there (at least 16 when available).
inline OwnerGuardPlan PlanOwnerGuard(const uint8_t* code, size_t n) {
    OwnerGuardPlan plan;
    // the reload: mov reg, [mem] (8B /r, or A1 for eax from an absolute address) followed by the read of [reg+0xEC]
    size_t load_length = 0;
    uint8_t dest = 0;
    int8_t load_base = -1;
    int32_t load_disp = 0;
    OwnerGuardOperand load;
    if (n >= 2 && code[0] == 0x8B && DecodeOwnerGuardModRm(code + 1, n - 1, load) && load.memory && !load.index) {
        load_length = 1 + load.length;
        dest = load.reg;
        load_base = load.base;
        load_disp = load.disp;
    } else if (n >= 5 && code[0] == 0xA1) {
        load_length = 5;
        load_disp = int32_t(uint32_t(code[1]) | uint32_t(code[2]) << 8 | uint32_t(code[3]) << 16 | uint32_t(code[4]) << 24);
    }
    OwnerGuardOperand next;
    if (load_length && DecodeOwnerGuardMemory(code + load_length, n - load_length, next) &&
        ReadsOwnerAttachment(next, dest)) {
        plan.rule = OwnerGuardRule::OwnerLoad;
        plan.owner_reg = dest;
        plan.load_base = load_base;
        plan.load_disp = load_disp;
        return plan;
    }
    // the read itself: any access to [reg+0xEC]
    OwnerGuardOperand here;
    if (DecodeOwnerGuardMemory(code, n, here) && !here.index && here.base >= 0 && here.disp == kActorOwnerAttachment) {
        plan.rule = OwnerGuardRule::OwnerRegister;
        plan.owner_reg = uint8_t(here.base);
    }
    return plan;
}

// Whether the owner the guest is about to read through is null. regs: the eight registers in x86 order.
template <class Read32>
inline bool OwnerGuardOwnerIsNull(const OwnerGuardPlan& plan, const uint32_t (&regs)[8], Read32&& read32) {
    switch (plan.rule) {
    case OwnerGuardRule::OwnerRegister:
        return regs[plan.owner_reg] == 0;
    case OwnerGuardRule::OwnerLoad:
        return read32(uint32_t(plan.load_disp) + (plan.load_base >= 0 ? regs[plan.load_base] : 0u)) == 0;
    case OwnerGuardRule::Unknown:
    default:
        return regs[0] == 0;
    }
}

// The hook's answer: true skips to the cleanup. A skipped owner load leaves the null owner in its register, as the
// guest's own load would have.
template <class Read32>
inline bool OwnerGuardSkips(const OwnerGuardPlan& plan, uint32_t (&regs)[8], Read32&& read32) {
    if (!OwnerGuardOwnerIsNull(plan, regs, read32)) return false;
    if (plan.rule == OwnerGuardRule::OwnerLoad) regs[plan.owner_reg] = 0;
    return true;
}

}  // namespace hulk
