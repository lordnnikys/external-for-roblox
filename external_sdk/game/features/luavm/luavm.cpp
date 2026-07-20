#include "luavm.hpp"
#include "remote_spy.hpp"
#include "../../offsets/offsets.hpp"
#include "../../core.hpp"
#include <vector>

namespace luavm {

static std::vector<uintptr_t> s_patched;

static void find_by_class(uintptr_t parent, const std::string& cls, std::vector<uintptr_t>& out) {
    if(!parent) return;
    for(auto c : core.children(parent)) {
        if(!c) continue;
        if(core.get_instance_classname(c)==cls) out.push_back(c);
        find_by_class(c,cls,out);
    }
}

bool install_hook() {
    using namespace vars::luavm;
    if(!memory||!memory->is_valid()){strcpy_s(status_msg,"Error: Roblox not found.");return false;}
    uintptr_t base=memory->find_image();
    if(!base){strcpy_s(status_msg,"Error: No base.");return false;}
    uintptr_t ws=memory->find_module("winsta.dll");
    if(!ws){strcpy_s(status_msg,"Error: winsta.dll not found.");return false;}
    level_mask = (lvl_output?1:0) | (lvl_info?2:0) | (lvl_warn?4:0) | (lvl_error?8:0);
    int err=remote_spy::install_hook(memory->get_handle(),base,ws,
        offsets::RemoteEvent::VtableRVA, offsets::LuaU::Print,
        msg, level_mask,
        codecave,remote_vtable);
    if(err){snprintf(status_msg,sizeof(status_msg),"install err: %d",err);return false;}
    original_vtable=base+offsets::RemoteEvent::VtableRVA;
    installed=true;
    snprintf(status_msg,sizeof(status_msg),"Installed. msg=\"%s\" mask=%02X",msg,level_mask);
    return true;
}

int patch_all_instances() {
    using namespace vars::luavm;
    s_patched.clear();
    if(!installed||!g_main::datamodel) return 0;
    std::vector<uintptr_t> res;
    find_by_class(g_main::datamodel,"RemoteEvent",res);
    auto ws=core.find_first_child_class(g_main::datamodel,"Workspace");
    if(ws) find_by_class(ws,"RemoteEvent",res);
    int n=0;
    for(auto re:res) if(!remote_spy::patch_instance(memory->get_handle(),re,remote_vtable,original_vtable)){s_patched.push_back(re);n++;}
    patched_count=n;patches_applied=n>0;
    snprintf(status_msg,sizeof(status_msg),"Patched %d.",n);
    return n;
}

void unpatch_all(){
    for(auto i:s_patched) remote_spy::unpatch_instance(memory->get_handle(),i,vars::luavm::original_vtable);
    s_patched.clear();
    vars::luavm::patches_applied=false;vars::luavm::patched_count=0;
    strcpy_s(vars::luavm::status_msg,"Unpatched.");
}

void tick(){}

// === Heartbeat one-shot ===
static uintptr_t find_heartbeat() {
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t start = (uintptr_t)si.lpMinimumApplicationAddress;
    uintptr_t end   = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi;
    std::vector<BYTE> buf;
    while (start < end) {
        if (VirtualQueryEx(memory->get_handle(), (LPCVOID)start, &mbi, sizeof(mbi))) {
            if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE|PAGE_EXECUTE_READWRITE)) && !(mbi.Protect & PAGE_GUARD)) {
                buf.resize(mbi.RegionSize);
                SIZE_T rd=0;
                if (ReadProcessMemory(memory->get_handle(), (LPCVOID)start, buf.data(), mbi.RegionSize, &rd) && rd == mbi.RegionSize) {
                    for (size_t i = 0; i < mbi.RegionSize - 0x80; i += 8) {
                        uintptr_t ptr = *(uintptr_t*)&buf[i];
                        if (ptr < 0x10000 || ptr >= 0x7FFFFFFFFFFF) continue;
                        char name[32] = {};
                        SIZE_T n = 0;
                        if (ReadProcessMemory(memory->get_handle(), (LPCVOID)(ptr + 0x18), name, sizeof(name), &n) && n > 0) {
                            if (strcmp(name, "Heartbeat") == 0) return ptr;
                        }
                    }
                }
            }
            start += mbi.RegionSize;
        } else start += 0x1000;
    }
    return 0;
}

bool execute_heartbeat() {
    using namespace vars::luavm;
    if (!memory || !memory->is_valid()) { strcpy_s(status_msg, "No Roblox."); return false; }
    uintptr_t base = memory->find_image();
    uintptr_t ws = memory->find_module("winsta.dll");
    if (!base || !ws) { strcpy_s(status_msg, "No base/winsta."); return false; }
    level_mask = (lvl_output?1:0)|(lvl_info?2:0)|(lvl_warn?4:0)|(lvl_error?8:0);

    strcpy_s(status_msg, "Scanning for Heartbeat...");
    uintptr_t hb = find_heartbeat();
    if (!hb) { strcpy_s(status_msg, "Heartbeat not found."); return false; }

    int err = remote_spy::install_heartbeat_hook(memory->get_handle(), base, ws,
        offsets::LuaU::Print, hb, msg, level_mask, codecave, remote_vtable);
    if (err) { snprintf(status_msg, sizeof(status_msg), "err: %d", err); return false; }
    installed = true;
    snprintf(status_msg, sizeof(status_msg), "Heartbeat hooked.");
    return true;
}

} // namespace luavm
