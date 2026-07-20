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

    int err=remote_spy::install_hook(memory->get_handle(),base,ws,
        offsets::RemoteEvent::VtableRVA,
        offsets::LuaU::Print,
        codecave,remote_vtable);
    if(err){snprintf(status_msg,sizeof(status_msg),"install_hook err: %d",err);return false;}

    original_vtable=base+offsets::RemoteEvent::VtableRVA;
    installed=true;
    snprintf(status_msg,sizeof(status_msg),"Hook installed. Cave: 0x%llX",(unsigned long long)codecave);
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
    patched_count=n;
    patches_applied=n>0;
    snprintf(status_msg,sizeof(status_msg),"Patched %d RemoteEvents. Fire() runs print('Hello World').",n);
    return n;
}

void unpatch_all(){
    for(auto i:s_patched) remote_spy::unpatch_instance(memory->get_handle(),i,vars::luavm::original_vtable);
    s_patched.clear();
    vars::luavm::patches_applied=false;
    vars::luavm::patched_count=0;
    strcpy_s(vars::luavm::status_msg,"All unpatched.");
}

void tick(){}

} // namespace luavm
