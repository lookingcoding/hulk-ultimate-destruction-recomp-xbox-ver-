# Virtual function discovery

The movement/camera/jump smoke test reached gameplay, then the first scripted punch at 155 seconds trapped on an indirect call to `0x0001D570` from `0x00013975`.

The function is a normal method with a stack frame and `ret 4`. The read-only vtable at `0x003F4174` contains its pointer at `0x003F4190`. The previous function ends with an unconditional tail jump at `0x0001D56B`, immediately before the method.

The SDK's pointer-seed heuristic recognizes a preceding relative jump only once that jump has been decoded. This can reject an unvisited method boundary, allowing speculative gap decoding to obscure it later.

`patches/vtable-discovery.patch` establishes methods referenced by read-only runs of code pointers when a decoded short or near tail jump ends exactly at the method. This happens before speculative pointer candidates. Ordinary pointer candidates retain their original checks.

The regression test in `tests/analysis.cpp` exercises both short and near unvisited tail jumps, verifies the read-only method entries, and checks that writable pointer arrays and addresses outside the code section do not acquire the same priority. A baseline test failed on the original SDK. The revised test passes with the patch.

Generation with the patch includes `sub_0001D570`; no combat function was replaced with a success stub. The native rebuild succeeded, and the repeated 195-second gameplay smoke test passed the previously failing punch input without a logged runtime error.
