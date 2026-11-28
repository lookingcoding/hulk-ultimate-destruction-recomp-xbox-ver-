# Issue #23 — static analysis of the charged missile throw

**Date:** 2026-11-28 · **Method:** static only (no game launch, no file edits in the repo)

## Environment findings first

- `generated/game/` in the working tree holds only `x86_recomp_stats.txt` — the recompiled
  code is gitignored and was not built here.
- The EU XBE is not on disk; `assets/usa/default.xbe` is the only binary present. I parsed
  it directly (XBE header → sections; base 0x10000, `.text` VA 0x11000..0x364834) and
  disassembled with capstone (installed to `.local/pydeps`, on D:).
- The EU↔USA address mapping is **Δ = +0x240** in the relevant region, confirmed on three
  independent functions: AddCharge (EU 0x16FE80 = USA 0x16FC40), the projectile pool getter
  (EU 0x1B6B20 = USA 0x1B68E0), and the projectile update (EU 0x19EB50 = USA 0x19E910).
- The EU recompiled output from a previous session survives at
  `D:\Projects\Build Cache\hulk-issue23\generated\game\` (763 files, 46k functions) and
  matches the USA disassembly instruction-for-instruction on every check I compared.

## The launch chain (all guest-side)

1. **Charge track** (`ChargeTrack@motion@hulkfight`, EU ~0x171860): actor → grabbable via
   0x592A0/0x59A10. On entry it compares the node's state-id param `[params+0x14]` against
   the prop's tag `[grabbable+0x1E5C]`; on mismatch it zeroes the charge `[+0x1E58]`.
   It reads the axis (`vtable+0x1C`) and writes `(duration − charge) / axis` to `[track+0x24]`.
2. **`AddCharge` = EU 0x16FE80 / USA 0x16FC40**: `[ecx+0x1E58] += [esp+4]; ret 4`.
   **It is dead code — zero call sites in the whole image.** The charge is accumulated by
   the compiler-inlined `fadd [reg+0x1E58]` sites at EU 0x1718CC, 0x17192E, 0x172263.
3. **Charge getter** (USA 0x3EC00, called with tag=2): validates the tag `[+0x1E5C]`,
   clamps to [0,1], **consumes** (zeroes) the charge, returns the fraction.
4. **`MissileThrowAction::execute`** (EU ~0x1985F0–0x1988EB): resolves actor/grabbable,
   reads the wanted projectile id `[params+0x10]`, scans the app's projectile pool
   (`[kApp+0x14]`, 0x20 slots, id getter = vtable slot 5) via the getter
   (EU 0x1B6B20 / USA 0x1B68E0), and **returns false** (fail path 0x1988EB) when the scan
   finds nothing. On success: velocity = lerp(min,max,charge) written to
   `[projectile+0x6C..0x74]`, speed to `[+0x78]`, target attached (0x196640), flags set
   (`[+0x8C] = −1.0` or the computed flight time; `[+0x9C] = 1` or 0).
5. **`ProjectileMissile::Update(float dt)` = EU 0x19EB50 / USA 0x19E910**
   (vtable EU 0x41B448 slot 4): the body I mapped end-to-end:
   - `[this+0x10]` (inactive byte) != 0 → **returns immediately, touching nothing**;
   - `|dt| <= 1e-6` (constant at EU 0x41BCF8 / USA 0x41B230) → **returns immediately**;
   - `[this+0xBC] += dt` (flight timer), life `[+0xC] -= dt`, homing toward the target's
     `[target+0x114]` position, state machine flags `[+0x28]`, `[+0xA4]`, phase `[+0x9C]`,
     then the physics move 0x98190 → 0x97A30 (whose 0x96B50 gate rejects object types 1/5/6).

## Can dt/accumulator stick the missile? No.

`src/game/simulation.cpp` (EU 0x88CD0 / USA 0x88C30 wrapper):
- Accumulator path: each step runs with `dt = kStep = 1/60` (line 167) — **never 0, always
  > 1e-6**. Steps that accumulated no time are skipped entirely (line 152: `if (!steps)`
  returns before the call) — the missile gets no update, but neither does its animation,
  SFX, or the rest of the world: no desync, no stick.
- Paused(6/7), nonpositive(≤0), dilated, and unpaced paths pass the guest's own elapsed —
  could be ≤ 1e-6, but the console ran the identical update with the identical value.
  (Nonpositive explicitly re-runs the original with its own elapsed, line 109-110.)
- The catch-up cap is 4 steps (1/15 s), so no unbounded backlog exists.

So **no recomp-specific dt mechanism can freeze the missile**: the accumulator never passes
0, and every bypass path is console-identical.

## The looping SFX/anim signature

A loop of throw SFX + animation with a stationary projectile is exactly what the guest
produces when the throw node **re-runs every frame**, which happens when
`MissileThrowAction::execute` returns false. Its fail paths: null params `[ebx+0x20]`,
actor/grabbable resolve failure, param resolve returning 0 (0x35EC0), or **the pool scan
not finding the wanted projectile id**. That last one is data-driven (fight-graph params
in the RCFs), guest-side, and identical on console.

## Recommendation

**No recomp-side fix is warranted from static analysis** — every gate that can freeze the
missile is guest code the recomp translates faithfully, and the host dt plumbing cannot
produce a recomp-specific stick.

Get a probe log first: `src/game/prop_probe.cpp` already logs the deciding fields
(dt, life, inactive byte, x, charge tag, state id). **But note one probe flaw I found:
the AddCharge hook (`sub_0016FE80`) can never fire** — the function is dead code (zero
call sites; the charge is accumulated at the inlined `fadd [reg+0x1E58]` sites EU
0x1718CC/0x17192E/0x172263). Charge lines in the log will only ever come from other
hooks. The `ret=%08x` return-address field on the projectile-update hook remains the
most valuable line: it names the state machine the missile is stuck in.
