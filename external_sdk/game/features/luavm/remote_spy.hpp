#pragma once
// RemoteEvent vtable hook -> custom Print(msg, selected levels)
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace remote_spy {

enum { VTABLE_COUNT = 256, VTABLE_SIZE = VTABLE_COUNT * 8 };
enum { CAVE_OFFSET = 0x2000, COL_OFFSET = 0x00, VT_OFFSET = 0x08, SC_OFFSET = 0x808, HOOK_SLOT = 3 };

// mask bits: 0=Output 1=Info 2=Warning 3=Error
inline size_t build_shellcode(uint8_t* buf, uintptr_t print_addr, uintptr_t original_func,
                               const char* msg, uint8_t mask) {
    uint8_t* p = buf;
    size_t msglen = strlen(msg);
    int count = 0; for (int i = 0; i < 4; i++) if (mask & (1 << i)) count++;

    *p++=0x53;*p++=0x50;*p++=0x51;*p++=0x52;
    *p++=0x41;*p++=0x50;*p++=0x41;*p++=0x51;
    *p++=0x48;*p++=0x83;*p++=0xEC;*p++=0x28;
    *p++=0x48;*p++=0xBB;memcpy(p,&print_addr,8);p+=8;

    size_t msg_offset = (p - buf) + count * 14 + 26; // restore(12) + jmp+func(14)
    for (int lvl = 0; lvl < 4; lvl++) {
        if (!(mask & (1 << lvl))) continue;
        *p++ = 0xB9; *p++ = (uint8_t)lvl; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
        *p++ = 0x48; *p++ = 0x8D; *p++ = 0x15;
        uint32_t disp = (uint32_t)(msg_offset - ((p - buf) + 4));
        memcpy(p, &disp, 4); p += 4;
        *p++ = 0xFF; *p++ = 0xD3;
    }

    *p++=0x48;*p++=0x83;*p++=0xC4;*p++=0x28;
    *p++=0x41;*p++=0x59;*p++=0x41;*p++=0x58;*p++=0x5A;*p++=0x59;*p++=0x58;*p++=0x5B;
    *p++=0xFF;*p++=0x25;*p++=0x00;*p++=0x00;*p++=0x00;*p++=0x00;
    memcpy(p, &original_func, 8); p += 8;
    memcpy(p, msg, msglen + 1); p += msglen + 1;
    return (size_t)(p - buf);
}

inline int install_hook(HANDLE ph, uintptr_t base, uintptr_t winsta,
    uintptr_t re_vt_rva, uintptr_t print_rva,
    const char* msg, uint8_t mask,
    uintptr_t& cave, uintptr_t& shadow)
{
    auto rd=[&](uintptr_t a,void*b,size_t s){SIZE_T n;return ReadProcessMemory(ph,(LPCVOID)a,b,s,&n)&&n==s;};
    auto wr=[&](uintptr_t a,const void*b,size_t s){SIZE_T n;return WriteProcessMemory(ph,(LPVOID)a,b,s,&n)&&n==s;};
    cave=winsta+CAVE_OFFSET;shadow=cave+VT_OFFSET;
    uintptr_t orig_vt=base+re_vt_rva;
    DWORD old;
    if(!VirtualProtectEx(ph,(LPVOID)cave,0x1000,PAGE_EXECUTE_READWRITE,&old))return 1;
    uintptr_t col=0;if(!rd(orig_vt-8,&col,8))return 2;if(!wr(cave,&col,8))return 3;
    std::vector<uint8_t>vt(VTABLE_SIZE);
    if(!rd(orig_vt,vt.data(),VTABLE_SIZE))return 4;
    if(!wr(cave+VT_OFFSET,vt.data(),VTABLE_SIZE))return 5;
    uintptr_t orig_func=0;
    if(!rd(orig_vt+HOOK_SLOT*8,&orig_func,8))return 6;
    uint8_t sc[256]={};
    size_t len=build_shellcode(sc,base+print_rva,orig_func,msg,mask);
    uintptr_t sc_addr=cave+SC_OFFSET;
    if(!wr(sc_addr,sc,len))return 7;
    FlushInstructionCache(ph,(LPCVOID)sc_addr,len);
    if(!wr(cave+VT_OFFSET+HOOK_SLOT*8,&sc_addr,8))return 8;
    return 0;
}

inline int patch_instance(HANDLE ph, uintptr_t inst, uintptr_t shadow, uintptr_t orig) {
    uintptr_t cur=0;SIZE_T n;
    if(!ReadProcessMemory(ph,(LPCVOID)inst,&cur,8,&n)||cur!=orig)return 1;
    if(!WriteProcessMemory(ph,(LPVOID)inst,&shadow,8,&n))return 2;
    uintptr_t v=0;
    if(!ReadProcessMemory(ph,(LPCVOID)inst,&v,8,&n)||v!=shadow){WriteProcessMemory(ph,(LPVOID)inst,&orig,8,&n);return 3;}
    return 0;
}

inline void unpatch_instance(HANDLE ph, uintptr_t inst, uintptr_t orig) {
    SIZE_T n;WriteProcessMemory(ph,(LPVOID)inst,&orig,8,&n);
}

// ==================== Heartbeat one-shot ====================
// Build: save -> Print -> write original back to shadow vtable -> tail-call
// Self-disables after first execution. mask bits: 0=Out 1=Info 2=Warn 3=Err
inline size_t build_shellcode_oneshot(uint8_t* buf, uintptr_t print_addr, uintptr_t orig_func,
                                       uintptr_t shadow_vt, const char* msg, uint8_t mask) {
    uint8_t* p = buf;
    size_t msglen = strlen(msg);
    int count = 0; for(int i=0;i<4;i++) if(mask&(1<<i)) count++;

    *p++=0x53;*p++=0x50;*p++=0x51;*p++=0x52;
    *p++=0x41;*p++=0x50;*p++=0x41;*p++=0x51;
    *p++=0x48;*p++=0x83;*p++=0xEC;*p++=0x28;
    *p++=0x48;*p++=0xBB;memcpy(p,&print_addr,8);p+=8;

    size_t msg_off = (p-buf) + count*14 + 12 + 27 + 14; // restore + unpatch + jmp
    for(int lvl=0;lvl<4;lvl++){
        if(!(mask&(1<<lvl)))continue;
        *p++=0xB9;*p++=(uint8_t)lvl;*p++=0x00;*p++=0x00;*p++=0x00;
        *p++=0x48;*p++=0x8D;*p++=0x15;
        uint32_t d=(uint32_t)(msg_off-((p-buf)+4));memcpy(p,&d,4);p+=4;
        *p++=0xFF;*p++=0xD3;
    }

    // Restore regs (before unpatch to free rbx)
    *p++=0x48;*p++=0x83;*p++=0xC4;*p++=0x28;
    *p++=0x41;*p++=0x59;*p++=0x41;*p++=0x58;*p++=0x5A;*p++=0x59;*p++=0x58;*p++=0x5B; // 10

    // Self-unpatch: shadow_vt[HOOK_SLOT] = orig_func
    *p++=0x48;*p++=0xB8;memcpy(p,&shadow_vt,8);p+=8;      // mov rax, shadow_vt
    *p++=0x48;*p++=0xB9;memcpy(p,&orig_func,8);p+=8;       // mov rcx, orig_func
    *p++=0x48;*p++=0x89;*p++=0x88;                         // mov [rax+HOOK_SLOT*8], rcx
    uint32_t slot_off=HOOK_SLOT*8;memcpy(p,&slot_off,4);p+=4;

    // Tail-call
    *p++=0xFF;*p++=0x25;*p++=0x00;*p++=0x00;*p++=0x00;*p++=0x00;
    memcpy(p,&orig_func,8);p+=8;
    memcpy(p,msg,msglen+1);p+=msglen+1;
    return (size_t)(p-buf);
}

inline int install_heartbeat_hook(HANDLE ph, uintptr_t base, uintptr_t winsta,
    uintptr_t print_rva, uintptr_t hb_job, const char* msg, uint8_t mask,
    uintptr_t& cave, uintptr_t& shadow)
{
    auto rd=[&](uintptr_t a,void*b,size_t s){SIZE_T n;return ReadProcessMemory(ph,(LPCVOID)a,b,s,&n)&&n==s;};
    auto wr=[&](uintptr_t a,const void*b,size_t s){SIZE_T n;return WriteProcessMemory(ph,(LPVOID)a,b,s,&n)&&n==s;};
    cave=winsta+CAVE_OFFSET;shadow=cave+VT_OFFSET;
    DWORD old;
    if(!VirtualProtectEx(ph,(LPVOID)cave,0x1000,PAGE_EXECUTE_READWRITE,&old))return 1;
    // Copy COL from heartbeat vtable
    uintptr_t hb_vt=0;if(!rd(hb_job,&hb_vt,8))return 2;
    uintptr_t col=0;if(!rd(hb_vt-8,&col,8))return 3;if(!wr(cave,&col,8))return 4;
    // Copy heartbeat vtable
    std::vector<uint8_t>vt(VTABLE_SIZE);
    if(!rd(hb_vt,vt.data(),VTABLE_SIZE))return 5;
    if(!wr(cave+VT_OFFSET,vt.data(),VTABLE_SIZE))return 6;
    uintptr_t orig_func=0;
    if(!rd(hb_vt+HOOK_SLOT*8,&orig_func,8))return 7;
    uint8_t sc[256]={};
    size_t len=build_shellcode_oneshot(sc,base+print_rva,orig_func,shadow,msg,mask);
    uintptr_t sc_addr=cave+SC_OFFSET;
    if(!wr(sc_addr,sc,len))return 8;
    FlushInstructionCache(ph,(LPCVOID)sc_addr,len);
    if(!wr(cave+VT_OFFSET+HOOK_SLOT*8,&sc_addr,8))return 9;
    // Replace heartbeat job vtable -> shadow
    if(!wr(hb_job,&shadow,8))return 10;
    return 0;
}

} // namespace remote_spy
