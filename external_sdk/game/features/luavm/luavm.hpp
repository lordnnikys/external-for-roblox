#pragma once
#include "../../../main.hpp"
#include "../../../handlers/vars.hpp"

namespace luavm {
bool install_hook();
int  patch_all_instances();
void unpatch_all();
void tick();
} // namespace luavm
