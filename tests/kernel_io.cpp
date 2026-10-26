#include <algorithm>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include "kernel/kernel.h"

// Exercise the real exported wrappers against an isolated guest-memory/handle fixture.
namespace kernel {
uint8_t* g_base;
auto& Exports() { static std::unordered_map<uint32_t, KernelFn> value; return value; }
void RegisterExport(uint32_t ordinal, const char*, KernelFn fn) { Exports()[ordinal] = fn; }
static std::unordered_map<uint32_t, ObjectRef> handles, pointers;
static std::unordered_map<std::string, ObjectRef> names;
static std::mutex handle_lock, dispatch_lock;
static uint32_t next_handle = 4;
uint32_t NewHandle(ObjectRef object) { std::lock_guard lock(handle_lock); uint32_t h = next_handle; next_handle += 4; handles[h] = object; return h; }
ObjectRef FromHandle(uint32_t handle) { std::lock_guard lock(handle_lock); auto it = handles.find(handle); return it == handles.end() ? nullptr : it->second; }
bool CloseHandle(uint32_t handle) { std::lock_guard lock(handle_lock); return handles.erase(handle) != 0; }
ObjectRef FromPointer(uint32_t pointer) { return pointers[pointer]; }
ObjectRef FindNamed(const std::string& name) { return names[name]; }
void AddNamed(const std::string& name, ObjectRef object) { object->name = name; names[name] = object; }
std::string ReadAnsiString(uint32_t str) { return str ? std::string(reinterpret_cast<char*>(g_base + R32(str + 4)), R16(str)) : ""; }
uint32_t DataAddress(uint32_t ordinal) { return 0x80000 + ordinal * 16; }
std::mutex& DispatchLock() { return dispatch_lock; }
void SetSignal(Object& object, int32_t value) { object.signal = value; }
void SignalChanged() {}
int64_t ToDeadline(int64_t due) {
    auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return now + (due < 0 ? -due * 100 : 0);
}
static std::vector<uint32_t> callback;
uint32_t CallGuest(X86Context&, uint32_t fn, const uint32_t* args, size_t count) {
    if (fn == 0xFE000000u + 301 * 4) { if (count != 1 || args[0] != kAccessDenied) throw std::runtime_error("APC status conversion arguments"); return 5; }
    if (fn == 0xFE000000u + 218 * 4) {
        if (count != 5 || !FromHandle(args[0]) || args[2] != 0x5000 || args[3] != 24 || args[4] != 3) throw std::runtime_error("Volume query forwarding arguments");
        W32(args[1], kSuccess); W32(args[1] + 4, 24); W32(args[2] + 20, 2048); return kSuccess;
    }
    if (fn != 0x123456) throw std::runtime_error("Unexpected guest callback");
    callback.assign(args, args + count); return 0;
}
}
using namespace kernel;
static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static uint32_t Invoke(uint32_t ordinal, std::initializer_list<uint32_t> args, uint32_t stack = 0x10000) {
    X86Context ctx{}; ctx.esp = stack; W32(stack, 0xDEADBEEF);
    size_t i = 0; for (uint32_t value : args) W32(stack + 4 + uint32_t(i++) * 4, value);
    Exports().at(ordinal)(ctx, g_base);
    Check(ctx.esp == stack + 4 + args.size() * 4, "Export corrupted guest stack");
    return ctx.eax;
}
int main() {
    try {
        std::vector<uint8_t> memory(1 << 20); g_base = memory.data();
        Check(Invoke(191, {0x100, 0, 0, 0}) == kSuccess, "Create completion port");
        uint32_t port = R32(0x100);
        Check(IsIoCompletion(FromHandle(port)), "Port type");
        Check(Invoke(227, {port, 42, 100, kAccessDenied, 13}) == kSuccess, "Post failed I/O packet");
        Check(Invoke(227, {port, 43, 101, kSuccess, 19}) == kSuccess, "Post second packet");
        Check(Invoke(223, {port, 0x110, 0x114, 0x118, 0}) == kSuccess && R32(0x110) == 42 && R32(0x114) == 100 && R32(0x118) == kAccessDenied && R32(0x11C) == 13, "FIFO or failure status lost");
        Check(Invoke(223, {port, 0x110, 0x114, 0x118, 0}) == kSuccess && R32(0x110) == 43 && R32(0x11C) == 19, "Second FIFO packet");
        W64(0x130, 0);
        Check(Invoke(223, {port, 0x110, 0x114, 0x118, 0x130}) == kTimeout, "Empty port must time out");
        auto waiter = std::async(std::launch::async, [=] { return Invoke(223, {port, 0x210, 0x214, 0x218, 0}, 0x20000); });
        Check(waiter.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout, "Empty port did not block");
        Invoke(227, {port, 77, 88, kSuccess, 99});
        Check(waiter.wait_for(std::chrono::seconds(2)) == std::future_status::ready && waiter.get() == kSuccess && R32(0x210) == 77 && R32(0x21C) == 99, "Blocking remove did not wake with packet");
        uint32_t event = NewHandle(std::make_shared<Object>(ObjType::Event));
        Check(Invoke(227, {event, 0, 0, 0, 0}) == kObjectTypeMismatch && Invoke(223, {0xFFFFFFFF, 0x110, 0x114, 0x118, 0}) == kInvalidHandle, "Invalid handles/types accepted");
        const char name[] = "\\TestPort";
        std::memcpy(g_base + 0x1000, name, sizeof(name)); W16(0x1100, sizeof(name) - 1); W32(0x1104, 0x1000); W32(0x1204, 0x1100);
        Check(Invoke(191, {0x140, 0, 0x1200, 0}) == kSuccess, "Create named port");
        Check(Invoke(243, {0x1200, DataAddress(64), 0, 0x144}) == kSuccess && FromHandle(R32(0x140)) == FromHandle(R32(0x144)), "Open named object did not preserve identity");
        Check(Invoke(243, {0x1200, DataAddress(16), 0, 0x144}) == kObjectTypeMismatch, "Open named object ignored type");
        W32(0x2000, kAccessDenied); W32(0x2004, 87);
        Check(Invoke(232, {0x123456, 0x2000, 0}) == 0 && callback == std::vector<uint32_t>{5, 87, 0x2000}, "User I/O completion callback ABI");
        pointers[0x3000] = std::make_shared<Object>(ObjType::File);
        size_t old_handles = handles.size();
        Check(Invoke(76, {0x3000, 3, 24, 0x5000, 0x150}) == kSuccess && R32(0x150) == 24 && R32(0x5014) == 2048 && handles.size() == old_handles, "Volume query output/temporary handle cleanup");
        std::puts("Kernel I/O FIFO, blocking wake, statuses, object identity, callback ABI and volume forwarding passed.");
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
