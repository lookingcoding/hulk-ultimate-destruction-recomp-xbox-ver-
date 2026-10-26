# Kernel I/O support

`patches/z-kernel-io.patch` supplies the six kernel exports imported by this title that were absent from the pinned SDK: NtCreateIoCompletion, NtSetIoCompletion, NtRemoveIoCompletion, ObOpenObjectByName, NtUserIoApcDispatcher, and IoQueryVolumeInformation. Boot now reports zero missing imports.

Completion ports hold FIFO packets containing key, context, NTSTATUS and transferred byte count. Removal supports blocking and relative/absolute timeouts. FileCompletionInformation associates a file with a port, and completed transfers with an APC context post packets. Named object reopening preserves object identity and checks requested types. User I/O dispatch converts NTSTATUS to Win32 error before calling the guest completion routine. Volume queries forward to the SDK's existing file/volume implementation through a temporary handle.

The export signatures and object layouts follow [nxdk's public kernel interface](https://github.com/XboxDev/nxdk/blob/master/lib/xboxkrnl/xboxkrnl.h), licensed CC0. The isolated native test exercises the actual export wrappers, FIFO packet/status preservation, an empty-port timeout, a blocking reader wake, invalid handles/types, named identity, completion callback arguments, forwarded volume arguments, guest stack preservation and temporary-handle cleanup.

Limits: completion ports do not reproduce the Xbox kernel's per-thread concurrency scheduling controlled by Count. ObOpenObjectByName handles the SDK's registered object namespace; it does not implement arbitrary device parse procedures. All imported exports being present is not proof of complete kernel compatibility. File/port association needs game or integration coverage in addition to the isolated tests.
