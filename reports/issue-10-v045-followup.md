# Issue #10: truck still airborne after v0.4.5

Local investigation, 2026-11-06. No commit, push, release or GitHub comment. No production physics change in this follow-up.

## Reporter evidence and conclusion

Fetched the individual [Frost comment at 21:09:43Z](https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-/issues/10#issuecomment-6695647002), not just its notification. He quotes the v0.4.5 reply and says the truck still goes up to the sky. Therefore v0.4.5 is **not established as a fix for his truck**, despite the earlier local fixture results. This comment supplies no new blackout result.

The exact remaining cause has not been proved. Packaging checks pass. The audit found real timing gaps worth instrumenting, but neither is yet a demonstrated cause of the truck lift. Do not label the issue fixed based on another actor-position trace.

## Actual published package

Downloaded the public v0.4.5 asset again with `gh release download` into the new D: follow-up folder. Release target is `03e5829e4531c8ab42c5863970a45d470b8e6244`; local HEAD/main/origin-main match. ZIP is 77,387,004 bytes.

- ZIP SHA256: `bd05bb5d965d37a2764071f474783def061bc948237dba6de94a9110d33c7c19`, matching the release digest and original upload manifest.
- Shipped EU EXE: `7b4dfafeb5fc3bceee1c6a956205ac9756a35678d0b04a2b1a80f6e566f418da`.
- Shipped USA EXE: `690cd751511e737c4ece4ae52b9a2143d192338f29252e56356d81916ec81b4f`.
- EU executable exports `sub_00088CD0`; shipped `HulkRecompiled.lib` contains its import. USA executable exports `sub_00088C30`; shipped USA import library contains it. These are the scheduler wrappers in the released source.

The package generates the game DLL on the player's machine; a development DLL copied into a fixture does not check that path. To address this, regenerated and compiled all 129 European translation units from the local retail EU XBE using **only the downloaded release's** recompiler/config/headers/clang/lld/import library, with the flags in `sdk/runtime/app/game_code.cpp`. Build/link succeeded in about 318 seconds. This is a CLI reproduction of the preparation toolchain, not a test of the preparation GUI.

Parsed the resulting PE imports and exported `X86FuncMappings` table: guest `0x00088CD0` maps to host thunk `0x180DDEF60`, which jumps through IAT RVA `0xFF3DC8` importing `HulkRecompiled.exe!sub_00088CD0`. Thus fresh EU generation actually routes the world entry to the scheduler; the check is stronger than finding a symbol string. New native runs use this freshly generated DLL with the downloaded EXE.

This rules out an omitted scheduler in the public archive/fresh EU link. It cannot establish which files Frost launched locally. An overlay installation retaining an older launcher/executable is a possibility to check, not an accusation or an established explanation. Fresh USA generation/gameplay was not run; the USA export and import-library checks passed.

## Movement and timing audit

European original-code addresses below; USA uses the matching world wrapper at `0x00088C30`. Static direct-call and vtable evidence does not prove every indirect or event path has been exercised.

| Path | Evidence | Scope |
|---|---|---|
| Active actor movement | World `88CD0 -> 2AB40 -> 40950` | Under fixed ticks for positive world elapsed time |
| Distant/sleepwalk movement | Same actor manager `2AB40 -> 3E270 -> 35240` | Under the same boundary; not a separate per-render patrol loop found here |
| Actor post-collision / ground | World `88CD0 -> 2AE80 -> 42940 -> 428C0 -> 42660`, ground helper `418D0` | Under the same boundary for positive elapsed time |
| Smart-prop and physics managers | World invokes `1E7930`, `1B5FE0`, then smart-prop post-update `1DC870` | Main manager phases are covered; held/body/visual transforms and all callbacks are not dynamically verified |
| Scripted position changes | `3F3D0` calls `35240`; called by script bindings at `26B10/26B50` and motion actions | Can happen through events/scripts; not evidence of an independently ticking patrol loop by itself |
| Snap to ground motion action | `172B90` is vtable slot `+0xC` of RTTI `SnapToGroundAction@motion@hulkfight`; calls `35240` | Motion action exists; no proof this caused Frost's displacement |

Two concrete limitations in the current scheduling boundary:

1. **Zero elapsed time bypass.** `src/game/simulation.cpp` calls the full original world function when `elapsed <= 0`, resetting the accumulator. The original world function branches around normal work only for states 6/7; zero elapsed by itself does not take that special branch. An active state with zero elapsed can therefore still invoke actor/motion/ground phases once per rendered frame. Existing final-build logs (`issue10-fixed-clean-144/logs/hulk-3_001.log`, about 33-39 seconds) show roughly 143-145 movement/ground calls per second at `dt=0`, rather than 60. Those same samples report zero net vertical displacement. This is a real bypass, **not a reproduced ascent or proof that removing zero-time callbacks is safe**; UI/pause/script behavior must be preserved.

2. **Combat animation reads a separate clock.** Original `0x0013AD90` is RTTI `FrameReachedCondition@motion@hulkfight` vtable slot `+0x18`. At `0x0013AE43..55`, it obtains the original clock through global `[0x004DF840] + 0x58` and calls `0x000860A0`. That getter reads clock fields `+0x30/+0x34`; the main clock `0x00086430` updates them per main/render iteration. The condition multiplies that elapsed value by animation speed/frame values to test a frame threshold. The scheduler changes the world call's argument; it does **not** replace these global clock fields. This is a remaining mixed-time source in combat/motion, potentially relevant to action timing. It has not been shown to run for the failing truck action or to produce lift. Simply replacing all clock reads would be a speculative change.

The main loop also runs work outside `88CD0`, including rendering and other managers. Investigation of `86A30 -> 1EBCC0 -> 1F2D40` and `1D1B80` did not establish another truck movement loop. In particular, `1D1B80` only calls the Lua stack helper `32B6B0` with zero, rather than independently moving the truck. Do not treat every per-render callback as physics.

The correct next instrumentation should establish whether a truck/prop/body/attachment transform changes outside a scheduled tick, which clock the failing action used, and the last shield/contact/grab/drop event. Actor-root Y alone can miss a displaced visual or held smart prop.

## Screenshot and missing playthrough state

Reused and visually inspected the original 1920x1080 Frost attachment saved under `scratch/issue10-photo/frost-transport.jpg`; see `issue-10-photo.md` for its fetched comment, hash, script chunks and full interpretation.

The truck is visibly above street level, Hulk is on the street, and the red destroy-style marker leans toward **disable/shielded combat**, not a currently carried truck. Mission chase entry assigns `ARROW_DESTROY`; shield damage changes it to `ARROW_GRAB`; pickup removes the truck arrow and changes HUD/mission state. A still cannot conclusively exclude an earlier drop. The bright white column/crossing streaks may be a combat/shield-contact effect; they are not positively identified and do not prove a Church delivery marker.

The old fixture moved Hulk beside the truck once, periodically refilled half health and watched its actor position. It did not reproduce Frost's particular hit, collision, shield interaction, carry/drop or campaign history. The unsuccessful health-stepping fixture did not prove shield-break/pickup/delivery; changing health is not equivalent to successfully exercising the original damage/contact events.

## Native follow-up and limitations

All test files are isolated on D:, with copied saves and an assets junction. Original user save and installed game are unchanged. Original `save.dat` SHA1 remains `89c3af3a6b854ba8386c8e54ab31d5d467fd8b83`, matching the pre-investigation value.

**Normal save/menu route:** two boots (100 and 180 seconds) with the downloaded EU EXE and fresh DLL, no `-level`, no mission mod. Copied the original `release/hdd` into the isolated run. Its save metadata says `Need-To-Know`, not Frantic Recovery. The sharper second run visibly reached Story Mode/Continue and the Church menu, with Return to Gameplay selected. Selecting it subsequently produced a black screen with a yellow Press Enter prompt, which remained through the final capture; focus had also been lost during inspection. No Frantic Recovery gameplay was reached. Do not interpret this incomplete menu/replay attempt as a campaign mission pass, a new blackout reproduction or proof the save can reach mission 3-2. No untouched pre-Frantic save was located. The original save was not unlocked or advanced artificially.

The first menu runner missed 1 ms capture events and briefly loaded unsorted replay events, logging `test input: invalid event; replay disabled`; the replay was sorted/reloaded. The second run used longer capture events and logged no errors. These are harness limitations. Captures `162741` shows Story Mode, `162751/162756` show Church, and `162806` onward show Press Enter. A true campaign-entry reproduction remains untested.

**New combat-input variation:** 150-second EU/DX11 run of the shipped EXE/fresh DLL, 144 cap, vsync off, 640x480/25% scale. The mod again uses `-level c03m03` and half-health refill, but the single approach is at 3 units rather than 8 and it sends punch/special/grab/run inputs. It does not edit truck health or its transform. This deliberately tests a different interaction attempt; it is still synthetic, not a campaign playthrough. Eighty-five complete actor samples stayed near road height (Y -0.023..0.254); zero logged errors. Captures show Hulk on the street with enemies/effects, **not proof of a successful shield hit, shield break, pickup or delivery**; the truck is not clearly visible in those inspected frames. Consequently no new stage pass or comprehensive visual/body-transform pass is claimed.

This was not stable 144: per-second FPS observations in seconds 25..80 span 31.9..144.4, median 123.15. Renderer captures stall, and the first inefficient PE audit helper also consumed CPU during part of this run. This is not an isolated hardware benchmark. Later observations return near the cap, but do not repair the missing stable-144 interaction coverage. No assertion that Frost's stable-144 case is safe is justified. No zero-dt phase samples occurred in this new variation; the bypass evidence comes from the explicitly cited earlier log.

All test processes were stopped. `analyze_native.py` and `native-results.json` retain counts/ranges and exact log/capture paths. No further clean repeat of the old proximity fixture was used to declare success.

Five existing CTests passed again. These verify their respective unit/regression cases, not this mission or all combat timing.

## Questions for Frost, for owner review only

1. Does it first lift **before hitting/breaking the shield or ever picking it up**, on a shield hit/contact, or after a grab/drop? What action/object/move, and what objective text is on screen? Does simply approaching without attacking trigger it?
2. A 20-30 second video leading into the first lift, with the full HUD and FPS overlay, plus the save immediately before starting/failing the mission. A new still helps identify the stage, but the lead-in is more decisive than another airborne still.
3. For the v0.4.5 failure: exact cap and vsync setting, DX11/DX12/auto, disc region, GPU, mods, and `settings.toml` plus newest runtime log. Is this a fresh extraction or an overlay update, and which launcher/executable path was started? Version is already reported as v0.4.5; ask for execution details, not a redundant generic version question.
4. Separately, has the blackout recurred in v0.4.5? His latest truck-only comment gives no answer.

No questions have been posted to GitHub. No new physics candidate has been applied or published.

## Evidence paths

Follow-up base: `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-v045-followup`.

- Downloaded original ZIP; extracted `published/` (local retail data not included in the ZIP).
- `comment-6695647002.json`, `package-verification.json`, `eu-exports.txt`, `usa-exports.txt`, `fresh-dll-imports.txt`.
- `rebuild_published.py`, `fresh-eu-code/` generated code/objects/compile logs, `fresh-translate.log`, `fresh-link.log`, `fresh-build-result.json`, `verify_binding.py`.
- `audit_refs.py`, `audit-refs.txt`, `clock-xrefs.txt`, `extra-disasm.txt`, `more-disasm.txt`, `callback-disasm.txt`, `post-disasm.txt`; original disassembly also under sibling `issue10-vehicle-re/`.
- `campaign.ps1`, `campaign-continue.ps1`, isolated `campaign/`; `contact.ps1`, isolated `contact-144/`.
- `analyze_native.py`, `native-results.json` retain the new runs' results and original-save hash.

The first PE verifier was inefficient (repeated whole-file string splitting), consumed CPU during part of the contact run and was stopped, corrected to bounded string reads, and rerun successfully. Therefore that run is additionally unsuitable as proof of stable-144 behavior. A first guessed LLVM inspection-tool path was absent; the installed VS `dumpbin` and a small standard-library PE parser supplied the checks instead.
