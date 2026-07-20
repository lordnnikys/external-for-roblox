#pragma once

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <tlhelp32.h>

namespace thread_hijack {

// Shellcode (position-independent)
// Layout:
// +0x00: push all regs + sub rsp,0x28                          (36)
// +0x24: GetLuaState() -> rdi                                   (15)
// +0x33: luaVM_load(rdi, src, name, 0)                         (32)
// +0x53: luau_execute(rdi)                                     (15)
// +0x62: add rsp,0x28 + pop all regs                           (36)
// +0x86: ret                                                    (1)
// +0x87: code: "print('Hello World')\0"                        (22)
// +0x9D: name: "=ext\0"                                         (5)
// Total: 162 bytes

enum { kShellcodeSize = 162 };
enum { kCodeOffset = 0x87 };  // where code string starts

inline size_t build_shellcode(uint8_t* buf,
                               uintptr_t get_lua_state,
                               uintptr_t lua_vm_load,
                               uintptr_t luau_exec)
{
    uint8_t* p = buf;

    *p++ = 0x50;  // push rax
    *p++ = 0x53;  // push rbx
    *p++ = 0x51;  // push rcx
    *p++ = 0x52;  // push rdx
    *p++ = 0x56;  // push rsi
    *p++ = 0x57;  // push rdi
    *p++ = 0x41; *p++ = 0x50;  // push r8
    *p++ = 0x41; *p++ = 0x51;  // push r9
    *p++ = 0x41; *p++ = 0x52;  // push r10
    *p++ = 0x41; *p++ = 0x53;  // push r11
    *p++ = 0x41; *p++ = 0x54;  // push r12
    *p++ = 0x41; *p++ = 0x55;  // push r13
    *p++ = 0x41; *p++ = 0x56;  // push r14
    *p++ = 0x41; *p++ = 0x57;  // push r15
    *p++ = 0x9C;               // pushfq
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xEC; *p++ = 0x28;  // sub rsp,0x28
    // offset: 36

    // ---- GetLuaState() -> rdi ----
    *p++ = 0x48; *p++ = 0xB8;
    memcpy(p, &get_lua_state, 8); p += 8;
    *p++ = 0xFF; *p++ = 0xD0;   // call rax
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xC7;  // mov rdi, rax
    // offset: 51

    // ---- luaVM_load(L, src, name, 0) ----
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xF9;  // mov rcx, rdi
    // lea rdx, [rip+disp_code] - rip base = 0x3A, code at 0x87, disp = 0x87 - 0x3A = 0x4D
    *p++ = 0x48; *p++ = 0x8D; *p++ = 0x15;
    uint32_t d1 = (uint32_t)(kCodeOffset - (size_t)(p - buf) - 4);
    memcpy(p, &d1, 4); p += 4;
    // lea r8, [rip+disp_name] - rip base = 0x41, name at 0x9D, disp = 0x9D - 0x41 = 0x5C
    *p++ = 0x4C; *p++ = 0x8D; *p++ = 0x05;
    uint32_t d2 = (uint32_t)(kCodeOffset + 22 - (size_t)(p - buf) - 4);
    memcpy(p, &d2, 4); p += 4;
    *p++ = 0x45; *p++ = 0x31; *p++ = 0xC9;   // xor r9d, r9d
    *p++ = 0x48; *p++ = 0xB8;
    memcpy(p, &lua_vm_load, 8); p += 8;
    *p++ = 0xFF; *p++ = 0xD0;   // call rax
    // offset: 83

    // ---- luau_execute(L) ----
    *p++ = 0x48; *p++ = 0x89; *p++ = 0xF9;   // mov rcx, rdi
    *p++ = 0x48; *p++ = 0xB8;
    memcpy(p, &luau_exec, 8); p += 8;
    *p++ = 0xFF; *p++ = 0xD0;   // call rax
    // offset: 98

    // ---- Restore ALL registers ----
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x28;  // add rsp,0x28
    *p++ = 0x9D;               // popfq
    *p++ = 0x41; *p++ = 0x5F;  // pop r15
    *p++ = 0x41; *p++ = 0x5E;  // pop r14
    *p++ = 0x41; *p++ = 0x5D;  // pop r13
    *p++ = 0x41; *p++ = 0x5C;  // pop r12
    *p++ = 0x41; *p++ = 0x5B;  // pop r11
    *p++ = 0x41; *p++ = 0x5A;  // pop r10
    *p++ = 0x41; *p++ = 0x59;  // pop r9
    *p++ = 0x41; *p++ = 0x58;  // pop r8
    *p++ = 0x5F;               // pop rdi
    *p++ = 0x5E;               // pop rsi
    *p++ = 0x5A;               // pop rdx
    *p++ = 0x59;               // pop rcx
    *p++ = 0x5B;               // pop rbx
    *p++ = 0x58;               // pop rax
    // offset: 134

    // ---- Return to original code ----
    *p++ = 0xC3;  // ret
    // offset: 135

    // ---- Data strings ----
    const char* code = "print('Hello World')";
    size_t codelen = strlen(code) + 1;
    memcpy(p, code, codelen); p += codelen;

    const char* name = "=ext";
    memcpy(p, name, strlen(name) + 1); p += strlen(name) + 1;

    return (size_t)(p - buf);
}

// ---- Find a thread in the target process ------------------------------------------
inline DWORD find_thread(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    THREADENTRY32 te = { sizeof(te) };
    DWORD tid = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) {
                tid = te.th32ThreadID;
                break;
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return tid;
}

// ---- Execute shellcode via thread hijacking --------------------------------
// Returns: true on success
inline bool execute(HANDLE process, uintptr_t get_lua_state, uintptr_t lua_vm_load, uintptr_t luau_exec) {
    DWORD pid = GetProcessId(process);
    DWORD tid = find_thread(pid);
    if (!tid) return false;

    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                                FALSE, tid);
    if (!thread) return false;

    // 1. Suspend the thread
    if (SuspendThread(thread) == (DWORD)-1) { CloseHandle(thread); return false; }

    // 2. Get current context
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_ALL;
    if (!GetThreadContext(thread, &ctx)) {
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    // 3. Allocate shellcode in target
    LPVOID remote_sc = VirtualAllocEx(process, nullptr, kShellcodeSize,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remote_sc) {
        ResumeThread(thread);
        CloseHandle(thread);
        return false;
    }

    // 4. Build shellcode
    uint8_t* local_sc = new uint8_t[kShellcodeSize];
    build_shellcode(local_sc, get_lua_state, lua_vm_load, luau_exec);

    // 5. Write shellcode
    SIZE_T written = 0;
    WriteProcessMemory(process, remote_sc, local_sc, kShellcodeSize, &written);
    delete[] local_sc;

    // 6. Push original RIP onto the thread's stack (so `ret` goes back)
    uintptr_t new_rsp = ctx.Rsp - 8;
    WriteProcessMemory(process, (LPVOID)new_rsp, &ctx.Rip, 8, nullptr);

    // 7. Redirect RIP to shellcode, update RSP
    ctx.Rsp = new_rsp;
    ctx.Rip = (uintptr_t)remote_sc;
    SetThreadContext(thread, &ctx);

    // 8. Resume thread - it runs shellcode, then `ret` back to original RIP
    ResumeThread(thread);

    // 9. Wait briefly for it to execute, then clean up
    WaitForSingleObject(thread, 3000);
    VirtualFreeEx(process, remote_sc, 0, MEM_RELEASE);
    CloseHandle(thread);
    return true;
}

} // namespace thread_hijack
