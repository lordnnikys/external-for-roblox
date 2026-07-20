#pragma once
// RemoteEvent vtable hook -> Print("Hello World") x4
// Shellcode: save args -> Print(0/1/2/3, msg) -> restore -> tail-call original

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace remote_spy {

enum { VTABLE_COUNT = 256 };
enum { VTABLE_SIZE = VTABLE_COUNT * 8 };
enum { CAVE_OFFSET  = 0x2000 };
enum { COL_OFFSET   = 0x00 };
enum { VT_OFFSET    = 0x08 };
enum { SC_OFFSET    = 0x808 };
enum { HOOK_SLOT    = 3 };

// Shellcode (70 bytes):
// +0x00 (6):  push rcx,rdx,r8,r9
// +0x06 (4):  sub rsp,0x28
// +0x0A (5):  mov ecx,1
// +0x0F (7):  lea rdx,[rip+0x24] -> "Hello World"
// +0x16 (10): mov rax,print_addr; call
// +0x22 (4):  add rsp,0x28
// +0x26 (6):  pop r9,r8,rdx,rcx
// +0x2C (6):  jmp [rip+0]
// +0x32 (8):  original_func
// +0x3A (12): "Hello World\0"

inline size_t build_shellcode(uint8_t* buf, uintptr_t print_addr, uintptr_t original_func) {
    uint8_t* p = buf;

    // Save: rbx(print ptr), rax(align), rcx/rdx/r8/r9(args)
    *p++ = 0x53;                   // push rbx
    *p++ = 0x50;                   // push rax (alignment)
    *p++ = 0x51;                   // push rcx
    *p++ = 0x52;                   // push rdx
    *p++ = 0x41; *p++ = 0x50;      // push r8
    *p++ = 0x41; *p++ = 0x51;      // push r9

    *p++ = 0x48; *p++ = 0x83; *p++ = 0xEC; *p++ = 0x28;

    // Store print addr in preserved rbx
    *p++ = 0x48; *p++ = 0xBB;
    memcpy(p, &print_addr, 8); p += 8;

    // --- Print(0) Output ---
    *p++ = 0xB9; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    *p++ = 0x48; *p++ = 0x8D; *p++ = 0x15;
    uint32_t d = 0x46; memcpy(p, &d, 4); p += 4;
    *p++ = 0xFF; *p++ = 0xD3;

    // --- Print(1) Information ---
    *p++ = 0xB9; *p++ = 0x01; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    *p++ = 0x48; *p++ = 0x8D; *p++ = 0x15;
    d = 0x38; memcpy(p, &d, 4); p += 4;
    *p++ = 0xFF; *p++ = 0xD3;

    // --- Print(2) Warning ---
    *p++ = 0xB9; *p++ = 0x02; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    *p++ = 0x48; *p++ = 0x8D; *p++ = 0x15;
    d = 0x2A; memcpy(p, &d, 4); p += 4;
    *p++ = 0xFF; *p++ = 0xD3;

    // --- Print(3) Error ---
    *p++ = 0xB9; *p++ = 0x03; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    *p++ = 0x48; *p++ = 0x8D; *p++ = 0x15;
    d = 0x1C; memcpy(p, &d, 4); p += 4;
    *p++ = 0xFF; *p++ = 0xD3;

    // Restore
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x28;
    *p++ = 0x41; *p++ = 0x59;  // pop r9
    *p++ = 0x41; *p++ = 0x58;  // pop r8
    *p++ = 0x5A;               // pop rdx
    *p++ = 0x59;               // pop rcx
    *p++ = 0x58;               // pop rax
    *p++ = 0x5B;               // pop rbx

    // Tail-call original
    *p++ = 0xFF; *p++ = 0x25; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    memcpy(p, &original_func, 8); p += 8;

    // String
    const char* msg = "Hello World";
    memcpy(p, msg, strlen(msg)+1);
    p += strlen(msg)+1;

    return (size_t)(p - buf);
}

inline int install_hook(HANDLE ph, uintptr_t base, uintptr_t winsta,
                         uintptr_t re_vt_rva, uintptr_t print_rva,
                         uintptr_t& cave, uintptr_t& shadow)
{
    auto rd = [&](uintptr_t a, void* b, size_t s){ SIZE_T n; return ReadProcessMemory(ph,(LPCVOID)a,b,s,&n)&&n==s; };
    auto wr = [&](uintptr_t a, const void* b, size_t s){ SIZE_T n; return WriteProcessMemory(ph,(LPVOID)a,b,s,&n)&&n==s; };

    cave = winsta + CAVE_OFFSET;
    shadow = cave + VT_OFFSET;
    uintptr_t orig_vt = base + re_vt_rva;

    DWORD old;
    if(!VirtualProtectEx(ph,(LPVOID)cave,0x1000,PAGE_EXECUTE_READWRITE,&old)) return 1;

    uintptr_t col=0;
    if(!rd(orig_vt-8,&col,8)) return 2;
    if(!wr(cave,&col,8)) return 3;

    std::vector<uint8_t> vt(VTABLE_SIZE);
    if(!rd(orig_vt,vt.data(),VTABLE_SIZE)) return 4;
    if(!wr(cave+VT_OFFSET,vt.data(),VTABLE_SIZE)) return 5;

    uintptr_t orig_func=0;
    if(!rd(orig_vt+HOOK_SLOT*8,&orig_func,8)) return 6;

    uint8_t sc[128]={};
    size_t len = build_shellcode(sc, base+print_rva, orig_func);

    uintptr_t sc_addr = cave + SC_OFFSET;
    if(!wr(sc_addr,sc,len)) return 7;
    FlushInstructionCache(ph,(LPCVOID)sc_addr,len);

    if(!wr(cave+VT_OFFSET+HOOK_SLOT*8,&sc_addr,8)) return 8;
    return 0;
}

inline int patch_instance(HANDLE ph, uintptr_t inst, uintptr_t shadow, uintptr_t orig) {
    uintptr_t cur=0; SIZE_T n;
    if(!ReadProcessMemory(ph,(LPCVOID)inst,&cur,8,&n)||cur!=orig) return 1;
    if(!WriteProcessMemory(ph,(LPVOID)inst,&shadow,8,&n)) return 2;
    uintptr_t v=0;
    if(!ReadProcessMemory(ph,(LPCVOID)inst,&v,8,&n)||v!=shadow){WriteProcessMemory(ph,(LPVOID)inst,&orig,8,&n);return 3;}
    return 0;
}

inline void unpatch_instance(HANDLE ph, uintptr_t inst, uintptr_t orig) {
    SIZE_T n; WriteProcessMemory(ph,(LPVOID)inst,&orig,8,&n);
}

} // namespace remote_spy
