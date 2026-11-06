# Issue #10: Frost's v0.4.4 photo

Local analysis, 2026-11-06. No GitHub reply, commit, push, publication, or executable/physics change was made.

## Source and what it establishes

Fetched the full fourth comment through `gh issue view` and the individual GitHub API comment, then downloaded and inspected the original attachment at its original 1920x1080 resolution.

- Comment: [frost4d367, 2026-11-06T18:24:46Z](https://github.com/lookingcoding/hulk-ultimate-destruction-recomp-xbox-ver-/issues/10#issuecomment-6692249627).
- After quoting our reply, Frost explicitly writes: **The bug is still there in v0.4.4**. Version confirmation is therefore already present; it does not need to be requested again as if unknown. The image and text concern the transport; they do not separately establish whether the display blackouts persisted.
- Attachment: `https://github.com/user-attachments/assets/ff5ae407-d664-4713-b710-9d8e09e7d1e5`. GitHub returns image/jpeg, 458,674 bytes. SHA256 `1012D84525EFF3CA95BAE2630FEA5AF87F47C05A3BB5CA4122833D82093D82B8`. Saved as `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-photo\frost-transport.jpg`; initially downloaded with a .png filename, then renamed to .jpg after checking the content type. Image bytes were not edited.
- **Seen:** the circled vehicle is rendered well above the road beside a glass building, with a red target marker above it. Hulk is down at street level, not visibly carrying that vehicle. Dust/debris and bright vertical/diagonal beams are visible near him. Red target indicators surround the scene. The green bar at top left is the health display; yellow 7426 below it is the points counter, not a numeric health reading.
- The screenshot does not show the running executable/version, FPS overlay, active mission objective text, a grab/drop action, or the preceding event. Version is supported by Frost's accompanying text; 144 fps comes from his earlier comment, not a measurement shown in this image.

This confirms the reported **visual airborne-vehicle symptom**. A single screenshot cannot establish whether the actor root/collision body moved up, only the rendered truck/attached body did, or when the displacement began.

## Mission phase: what can and cannot be inferred

The strongest visible interpretation is combat/disable or retrieve activity, rather than an active carry animation. The red destroy-style target and empty-looking carry pose lean toward the pre-pickup/shield stage, but they do **not** conclusively identify the mission state. A post-pickup drop remains possible. The photo alone does not prove delivery phase or a successful earlier shield break.

The original EU `c03m03_scenario.fig` provides better discriminators:

- State list at Lua chunk `0x19E`: `div_truck_intro`, `retrieve_truck_chase`, `retrieve_truck_inhand`, `retrieve_truck_dropped`, `complete`.
- Chase entry at `0x189E`: assigns `ARROW_DESTROY` to the truck, enables `HUD_STANDARD + HUD_CHASE`, and records its smart-prop position.
- Shield-damaged action at `0x7FC2`: changes the objective arrow to `ARROW_GRAB` and hides the shield's enemy-health display.
- Pickup action at `0x6746`: enters `retrieve_truck_inhand`, removes the truck arrow, adds a return icon at `c3m3_markerjump1_loc`, hides `HUD_CHASE`, and shows truck enemy-health HUD. The in-hand entry at `0x1DCA` disables the thrown-object-only damage restriction and activates `church_putdown_marker`.
- Dropped entry at `0x224E`: changes back to `player_state = in_mission`, changes/deactivates the church marker, and adds a grab icon to the truck.
- The jump-to-Church condition at `0xA282` requires `retrieve_truck_inhand` and Hulk inside `c3m3_markerjump1_vol`.

These HUDs can share similar-looking bars, and objective markers can coexist with ordinary enemy indicators. The photo's bar/marker is evidence to compare, not a substitute for the current mission objective and a short video.

## The vertical white beam

**Not conclusively identified.** The image contains a vertical bright column and crossing diagonal streaks with dust/debris around Hulk. This is compatible with a combat/impact or shield-contact effect, but a persistent jump/mission marker cannot be ruled out from a still image. It is not valid to label it the Church delivery marker merely because it is vertical or because Hulk is nearby.

The mission has both shield-contact events (`hulkzapped` / `shield_damageshulk`, chunk `0x57FE`) and jump/delivery markers. Neither proves which rendered effect appears in this photo. A video can distinguish them: does the light appear briefly on a hit, or remain fixed on the street when Hulk moves away? Does a jump/Church prompt appear? The beam itself does not prove a physics timestep fault or a later mission state.

## The concrete gap in our 144 fps test

Our clean runs left Hulk stationary at the initial rooftop/bridge spawn and queried the named actor's position. They did not chase on foot at street level, make melee contact, throw objects at the shield, break it, grab/carry/drop the truck, or return it to a jump marker. Frost is actively in the street amid effects; this is materially different even if both runs have the same mission-state name and FPS.

The script explicitly has player-proximity-dependent behavior:

- `0x45FA`: within 20 game units, posts `approachingdivtruck` and updates the approach gating flags.
- `0x536E`: a close-distance branch calls `am_SetPatrolParams` with values 1.2, 0, 8; its debug text says the truck should speed up.
- `0x4D86`: within 50 units, gates a disable-stage camera/event transition; `0x4366` also gates escort activity by proximity.
- `0x4AFA`: shield-health thresholds post `truckhit` events.

This gives a concrete lead beyond raw FPS: **approach/contact/damage-triggered state changes and their interaction with 144 fps**. It is not yet an identified fault. Being unable to reproduce the symptom with a distant, stationary player did not test these branches deliberately or validate the truck's full lifecycle.

Our telemetry logged `am_GetActorPosition(DivisionTruck)`. The mission also uses `spm_GetSmartPropPosition('division_truck_c3m3_convoytruck_1')` and switches actor/smart-prop handling around pickup. Therefore 127 actor-root samples at road height are not a complete test of the visual/body transform, held-object attachment, or collision representation. Future instrumentation should capture both positions, rendered/body transforms where possible, current mission/smart-prop/shield state, distance to Hulk, and the last damage/grab/drop event.

One superficially suspicious static-data lead was checked and rejected: locators named `c3m3_divisiontruck_convoy3/4` have elevated heights, but chunk `0x972E` uses them to spawn **Comanche helicopters**, not the transport truck. Their names alone do not establish an airborne-truck spawn bug.

## What to ask Frost next (not posted)

1. **When does it lift off?** Before breaking the shield or ever picking it up, immediately after a punch/thrown object/vehicle collision, when the shield breaks, or after grabbing/dropping? What does the current mission objective say? What object/move was used?
2. **A short video and a pre-failure save.** Ideally include 20-30 seconds before it rises, the HUD/objective, and the action that triggers it. Does the white column persist at a fixed spot or flash when Hulk is hit? If feasible, repeat the same action from the same save at 60 versus 144 fps.
3. **Settings for this exact screenshot:** was the new v0.4.4 run still capped at 144 with vsync off/on? Which renderer (auto/DX12 or DX11), GPU, and disc region? Attach `settings.toml` and the latest runtime log, and list any mods. The version itself is already explicitly confirmed in this comment.
4. **Separately:** did v0.4.4 stop the display blackouts, or does that symptom also remain? Do not infer the answer from the transport photo.

If a video/save is supplied, the next local reproduction should match the action and mission phase first, then compare 60 and 144 with synchronized state/transform logging. This evidence does not justify a broad fixed-timestep change yet.

## Saved evidence

Under `D:\Projects\Build Cache\Hulk Recompiled\scratch\issue10-photo\`: `comment-6692249627.json`, `image-headers.txt`, `frost-transport.jpg`, `extract-evidence.py`, `script-evidence.txt`, and `extraction-log.txt`. Original extracted disc data and disassembly from the earlier investigation were reused, read-only. Documentation is local and uncommitted.
