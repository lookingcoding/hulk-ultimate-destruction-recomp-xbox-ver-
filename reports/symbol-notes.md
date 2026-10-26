# Symbol investigation

The automatic scan's zero entries are unresolved or absent; they are not proof that those APIs are called. In particular, this XBE reports no XNet/XOnline libraries. Do not invent addresses for absent networking symbols.

The automatic BinkDoFrame match is `0x00368680`. Its game caller is the movie update routine at `0x0021D6B0`.

Two Bink symbols were identified by examining this caller and the candidate function bodies:

- BinkWait: `0x00367400`. The movie update passes its movie handle to this function at `0x0021D6DB`, tests the result, and only decodes a frame when zero. The body consults the playback clock, frame rate, and audio timing, and returns zero/one with `ret 4`.
- BinkNextFrame: `0x00368C90`. Called at `0x0021D795`, after decode/copy and an end-of-movie check. The body advances the movie's frame field at `handle+0x0C` and returns with `ret 4`.

The BinkGetSummary match at `0x003676C0` was also checked: its `0x0036779B`-`0x003677C6` sequence reads byte count at `handle+0x134`, time at `handle+0x13C`, and divides `(bytes*1000)/(time+1)`. These offsets agree with the SDK's overflow fix.

Disassembly was generated locally by `hulk_disasm`, linked against Zydis and using the SDK's XBE parser. Derived assembly files are ignored by Git.

Movie playback has since been captured during native startup.

## Dynamic vertex-buffer synchronization

XDK 5849's vertex buffer Lock2 routine at `0x003838C0` calls `0x0038B130` to wait before reusing a buffer. The SDK has a `D3D_BlockOnNonSurfaceResource` replacement, but the 5849 signature set omitted it. This address is now mapped in `config/game.toml` to the existing resource wait/dirty-tracking implementation.

Before this mapping, stable pause/menu captures contained overlapping text. After it, `release/captures/131227/final.bmp` shows clean difficulty-selection text and `release/captures/131446/final.bmp` shows clean pause-menu text. The renderer timing log also records resource waits. This verifies the observed menu corruption; it does not establish that every graphics effect is correct.
