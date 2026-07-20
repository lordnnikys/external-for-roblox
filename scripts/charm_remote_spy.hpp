#pragma once
// ============================================================================
// Charm Remote Spy — Complete Standalone Shellcode + Hook Implementation
// Extracted 1:1 from charm-main/src/features/remote_spy/remote_spy.cpp
//
// Drop this .hpp into your project, call remote_spy::full_setup() once,
// then poll_loop() every frame. No dependencies beyond Windows.h.
// ============================================================================

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>

namespace remote_spy {

// ==================== Constants ====================
constexpr size_t VTABLE_ENTRIES  = 256;
constexpr size_t VTABLE_SIZE     = VTABLE_ENTRIES * 8;
constexpr size_t WINSTA_CAVE_OFF = 0x2000;
constexpr size_t COL_OFF         = 0x00;
constexpr size_t VTABLE_OFF      = 0x08;
constexpr size_t SHELLCODE_OFF   = 0x808;
constexpr size_t SHELLCODE_STRIDE= 0x80;
constexpr size_t DATA_AREA_OFF   = 0x988;
constexpr size_t DATA_AREA_SZ    = 0x530;

constexpr size_t RING_CAP        = 32;
constexpr size_t RING_ENTRY_SZ   = 40;
constexpr size_t RING_SEQ_OFF    = 0x28;
constexpr size_t RING_BASE_OFF   = 0x30;
constexpr int    HOOK_SLOT       = 3;

// ==================== Ring Entry ====================
#pragma pack(push, 1)
struct RingEntry {
    uint64_t rcx;
    uint64_t rdx;
    uint64_t r8;
    uint64_t r9;
    uint32_t slot_id;
    uint32_t pad;
};
#pragma pack(pop)

// ==================== Shellcode Builder ====================
inline size_t build_shellcode(uint8_t* buf, uintptr_t data_area, uintptr_t original_func, int slot_id) {
    uint8_t* p = buf;

    // Prologue: 10 pushes (80) + sub rsp,0x28 (40) = 120 = aligned
    *p++ = 0x41; *p++ = 0x57;   // push r15
    *p++ = 0x41; *p++ = 0x56;   // push r14
    *p++ = 0x41; *p++ = 0x55;   // push r13
    *p++ = 0x41; *p++ = 0x54;   // push r12
    *p++ = 0x53;                // push rbx
    *p++ = 0x57;                // push rdi
    *p++ = 0x56;                // push rsi
    *p++ = 0x52;                // push rdx (save arg2)
    *p++ = 0x52;                // push rdx (alignment dup)
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xEC; *p++ = 0x28;  // sub rsp, 0x28

    // Load data_area into r10
    *p++ = 0x48; *p++ = 0xB8; memcpy(p, &data_area, 8); p += 8;  // mov rax, data_area
    *p++ = 0x49; *p++ = 0x89; *p++ = 0xC2;                       // mov r10, rax

    // lock xadd [r10+RING_SEQ_OFF], 1  (atomic get-and-inc)
    *p++ = 0x41; *p++ = 0xBB;                                    // mov r11d, 1
    *p++ = 0x01; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    *p++ = 0xF0; *p++ = 0x45; *p++ = 0x0F; *p++ = 0xC1;        // lock xadd
    *p++ = 0x5A; *p++ = (uint8_t)RING_SEQ_OFF;                  // [r10+RING_SEQ_OFF]

    // Compute index = (old_seq) & (RING_CAP-1)
    *p++ = 0x44; *p++ = 0x89; *p++ = 0xD8;                     // mov eax, r11d
    *p++ = 0x83; *p++ = 0xE0; *p++ = (uint8_t)(RING_CAP - 1);  // and eax, 31

    // Compute entry addr = data_area + RING_BASE_OFF + index * RING_ENTRY_SZ
    *p++ = 0x48; *p++ = 0x6B; *p++ = 0xC0; *p++ = (uint8_t)RING_ENTRY_SZ;  // imul rax, rax, 40
    *p++ = 0x4C; *p++ = 0x01; *p++ = 0xD0;                                   // add rax, r10
    *p++ = 0x48; *p++ = 0x05;                                                 // add rax, RING_BASE_OFF
    uint32_t rbo = (uint32_t)RING_BASE_OFF; memcpy(p, &rbo, 4); p += 4;

    // Write entry fields
    *p++ = 0x48; *p++ = 0x89; *p++ = 0x08;                       // mov [rax],    rcx
    *p++ = 0x48; *p++ = 0x8B; *p++ = 0x54; *p++ = 0x24; *p++ = 0x58; // mov rdx, [rsp+0x58]
    *p++ = 0x48; *p++ = 0x89; *p++ = 0x50; *p++ = 0x08;         // mov [rax+8],  rdx
    *p++ = 0x4C; *p++ = 0x89; *p++ = 0x40; *p++ = 0x10;         // mov [rax+16], r8
    *p++ = 0x4C; *p++ = 0x89; *p++ = 0x48; *p++ = 0x18;         // mov [rax+24], r9
    *p++ = 0xC7; *p++ = 0x40; *p++ = 0x20;                       // mov [rax+32], slot_id
    memcpy(p, &slot_id, 4); p += 4;

    // lock inc total counter
    *p++ = 0xF0; *p++ = 0x49; *p++ = 0xFF; *p++ = 0x42; *p++ = 0x20;

    // Epilogue: restore regs
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x28;  // add rsp, 0x28
    *p++ = 0x5A; *p++ = 0x5A;   // pop rdx, pop rdx (alignment + restore arg2)
    *p++ = 0x5E;                // pop rsi
    *p++ = 0x5F;                // pop rdi
    *p++ = 0x5B;                // pop rbx
    *p++ = 0x41; *p++ = 0x5C;   // pop r12
    *p++ = 0x41; *p++ = 0x5D;   // pop r13
    *p++ = 0x41; *p++ = 0x5E;   // pop r14
    *p++ = 0x41; *p++ = 0x5F;   // pop r15

    // Tail-call original: jmp [rip+0]; .dq original_func
    *p++ = 0xFF; *p++ = 0x25; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    memcpy(p, &original_func, 8); p += 8;

    return (size_t)(p - buf);
}

// ==================== Low-Level Helpers ====================
inline bool rpm(HANDLE h, uintptr_t a, void* b, size_t s) {
    SIZE_T n = 0; return ReadProcessMemory(h, (LPCVOID)a, b, s, &n) && n == s;
}
inline bool wpm(HANDLE h, uintptr_t a, const void* b, size_t s) {
    SIZE_T n = 0; return WriteProcessMemory(h, (LPVOID)a, b, s, &n) && n == s;
}

// ==================== Hook Install ====================
// Returns 0 on success. Outputs: codecave, shadow vtable addr, data area addr.
inline int install_hook(
    HANDLE h,
    uintptr_t robux_base,
    uintptr_t winsta_base,
    uintptr_t original_vtable_rva,
    uintptr_t& out_codecave,
    uintptr_t& out_remote_vtable,
    uintptr_t& out_data_area
) {
    uintptr_t codecave       = winsta_base + WINSTA_CAVE_OFF;
    uintptr_t remote_vtable  = codecave + VTABLE_OFF;
    uintptr_t data_area      = codecave + DATA_AREA_OFF;
    uintptr_t orig_vt        = robux_base + original_vtable_rva;

    out_codecave = codecave;
    out_remote_vtable = remote_vtable;
    out_data_area = data_area;

    // 1. RWX protect code cave
    DWORD oldP;
    if (!VirtualProtectEx(h, (LPVOID)codecave, 0x1000, PAGE_EXECUTE_READWRITE, &oldP)) return 1;

    // 2. Copy COL
    uintptr_t col = 0;
    if (!rpm(h, orig_vt - 8, &col, 8)) return 2;
    if (!wpm(h, codecave + COL_OFF, &col, 8)) return 3;

    // 3. Copy entire vtable (256 * 8 = 2048 bytes)
    std::vector<uint8_t> vt_buf(VTABLE_SIZE);
    if (!rpm(h, orig_vt, vt_buf.data(), VTABLE_SIZE)) return 4;
    if (!wpm(h, codecave + VTABLE_OFF, vt_buf.data(), VTABLE_SIZE)) return 5;

    // 4. Zero data area
    std::vector<uint8_t> zeros(DATA_AREA_SZ, 0);
    if (!wpm(h, codecave + DATA_AREA_OFF, zeros.data(), DATA_AREA_SZ)) return 6;

    // 5. Read original vfunc at slot 3
    uintptr_t original_vfunc = 0;
    if (!rpm(h, orig_vt + HOOK_SLOT * 8, &original_vfunc, 8)) return 7;

    // 6. Build + write shellcode
    uintptr_t sc_addr = codecave + SHELLCODE_OFF;
    uint8_t sc[192] = {};
    size_t sc_len = build_shellcode(sc, data_area, original_vfunc, HOOK_SLOT);
    if (!wpm(h, sc_addr, sc, sc_len)) return 8;
    FlushInstructionCache(h, (LPCVOID)sc_addr, sc_len);

    // 7. Patch shadow vtable: slot 3 -> shellcode_addr
    if (!wpm(h, remote_vtable + HOOK_SLOT * 8, &sc_addr, 8)) return 9;

    return 0;
}

// ==================== Instance Patching ====================
inline int patch_instance(HANDLE h, uintptr_t inst, uintptr_t remote_vtable, uintptr_t original_vtable) {
    uintptr_t cur = 0;
    if (!rpm(h, inst, &cur, 8)) return 1;
    if (cur != original_vtable) return 2; // already patched or wrong class

    if (!wpm(h, inst, &remote_vtable, 8)) return 3;

    // Verify
    uintptr_t verify = 0;
    if (!rpm(h, inst, &verify, 8) || verify != remote_vtable) {
        wpm(h, inst, &original_vtable, 8); // rollback
        return 4;
    }
    return 0;
}

inline void unpatch_instance(HANDLE h, uintptr_t inst, uintptr_t original_vtable) {
    wpm(h, inst, &original_vtable, 8);
}

// ==================== Ring Buffer Polling ====================
inline uint32_t read_seq(HANDLE h, uintptr_t data_area) {
    uint32_t s = 0; rpm(h, data_area + RING_SEQ_OFF, &s, 4); return s;
}

inline uint64_t read_total(HANDLE h, uintptr_t data_area) {
    uint64_t c = 0; rpm(h, data_area + 0x20, &c, 8); return c;
}

inline bool read_entry(HANDLE h, uintptr_t data_area, uint32_t seq, RingEntry& out) {
    uint32_t idx = seq & (RING_CAP - 1);
    uintptr_t addr = data_area + RING_BASE_OFF + (uintptr_t)idx * RING_ENTRY_SZ;
    return rpm(h, addr, &out, sizeof(out));
}

// ==================== High-Level API ====================

// Call once after attaching to Roblox.
// re_vtable_rva = RemoteEvent vtable RVA (e.g. 0x611EE00), discover via RTTI scan.
inline bool full_setup(
    HANDLE h,
    uintptr_t robux_base,
    uintptr_t winsta_base,
    uintptr_t re_vtable_rva,
    uintptr_t& out_codecave,
    uintptr_t& out_remote_vtable,
    uintptr_t& out_data_area,
    uintptr_t& out_original_vtable
) {
    out_original_vtable = robux_base + re_vtable_rva;
    int err = install_hook(h, robux_base, winsta_base, re_vtable_rva,
                           out_codecave, out_remote_vtable, out_data_area);
    if (err) { printf("[rs] install_hook err=%d\n", err); return false; }
    printf("[rs] OK cave=0x%llX data=0x%llX\n",
           (unsigned long long)out_codecave, (unsigned long long)out_data_area);
    return true;
}

// Call every frame. Pass your last_seq (static var, init to read_seq() on first call).
inline void poll_loop(HANDLE h, uintptr_t data_area, uint32_t& last_seq,
                       void (*on_fire)(const RingEntry&)) {
    uint32_t seq = read_seq(h, data_area);
    while (last_seq != seq) {
        ++last_seq;
        RingEntry e = {};
        if (read_entry(h, data_area, last_seq - 1, e) && on_fire)
            on_fire(e);
    }
}

} // namespace remote_spy
