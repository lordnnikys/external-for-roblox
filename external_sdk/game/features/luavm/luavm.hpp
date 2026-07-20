#pragma once
#include "../../../main.hpp"
#include "../../../handlers/vars.hpp"

namespace luavm {
bool install_hook();                          // RemoteEvent (keeps patches)
int  patch_all_instances();
void unpatch_all();
bool execute_heartbeat();                     // Heartbeat one-shot: find + hook + auto-unpatch
void tick();
} // namespace luavm
