#include "handler.hpp"
#include "example_esp/esp.hpp"
#include "speed_hack/speed.hpp"
#include "freecam/freecam.hpp"
#include "fly/fly.hpp"
#include "infinite_jump/infinite_jump.hpp"
#include "../../tphandler.hpp"

#include "../../handlers/vars.hpp"
#include "../../handlers/misc/misc.hpp"
#include <chrono>

void c_feature_handler::start(uintptr_t datamodel)
{
    if (!memory) return;
    if (!g_main::datamodel) return;
    if (!g_main::localplayer) return;
    if (!tp_handler.is_ready()) return;

    static auto last_time = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> dt = now - last_time;
    last_time = now;

    if (!datamodel && !g_main::v_engine) return;

    static HWND roblox_window = NULL;
    static std::chrono::steady_clock::time_point last_window_check;
    float ms_since_window_check = std::chrono::duration<float, std::milli>(now - last_window_check).count();

    if (ms_since_window_check > 1000.0f || roblox_window == NULL)
    {
        last_window_check = now;
        roblox_window = FindWindowA(NULL, "Roblox");
        if (!roblox_window) roblox_window = FindWindowA("WINDOWSCLIENT", NULL);
    }


    // ===== ONLY IF FOCUSED =====
    bool roblox_focused = (roblox_window && GetForegroundWindow() == roblox_window);
    if (!roblox_focused) return;

    if (!g_main::v_engine) return;

    // Read viewmatrix fresh every frame. Validate it's not degenerate
    // (all-zero matrix collapses every player to the same screen pixel)
    static view_matrix_t cached_viewmatrix;
    static bool has_valid_vm = false;
    {
        // WORKAROUND: Position moved from BasePart (0x12C) to Primitive (0xEC).
        // Primitive Flags moved from 0x1AE to 0x1B6.
        // The offset server doesn't update these yet. Override here.
        offsets::Position = 0xEC;
        offsets::BasePart::PrimitiveFlags = 0x1B6;

        view_matrix_t vm = memory->read<view_matrix_t>(g_main::v_engine + offsets::viewmatrix);
        bool ok = (fabsf(vm.m[0][0]) > 0.0001f || fabsf(vm.m[0][1]) > 0.0001f
                || fabsf(vm.m[0][2]) > 0.0001f || fabsf(vm.m[0][3]) > 0.0001f)
               && (fabsf(vm.m[1][0]) > 0.0001f || fabsf(vm.m[1][1]) > 0.0001f
                || fabsf(vm.m[1][2]) > 0.0001f || fabsf(vm.m[1][3]) > 0.0001f)
               && (fabsf(vm.m[3][0]) > 0.0001f || fabsf(vm.m[3][1]) > 0.0001f
                || fabsf(vm.m[3][2]) > 0.0001f || fabsf(vm.m[3][3]) > 0.0001f);
        if (ok) { cached_viewmatrix = vm; has_valid_vm = true; }
    }
    if (!has_valid_vm) return;

    if (vars::esp::toggled)
        esp.run_players(cached_viewmatrix);

    esp.run_aimbot(cached_viewmatrix);
    esp.draw_hitbox_esp(cached_viewmatrix);

    speed_hack::run();
    freecam.enabled = vars::freecam::toggled;
    freecam.run(dt.count());
    jump_power::run();
    infinite_jump::run();

    if (vars::fly::toggled)
        fly.run();

    if (vars::set_fov::unlock_zoom)
        freecam.unlock_zoom();

    if (vars::set_fov::toggled)
        freecam.set_fov(vars::set_fov::set_fov);
}
