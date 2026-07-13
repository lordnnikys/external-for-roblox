#include "fly.hpp"
#include "../../../handlers/vars.hpp"
#include "../../../game/offsets/offsets.hpp"
#include "../../../game/core.hpp"
#include "../../../addons/kernel/memory.hpp"
#include <thread>
#include <atomic>
#include <cmath>

static std::atomic<bool> s_running{ false };
static std::atomic<bool> s_stop{ false };

static void fly_thread()
{
    while (!s_stop)
    {
        if (!g_main::localplayer || !vars::fly::toggled)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        bool should_fly = false;
        if (vars::fly::fly_mode == 0)
            should_fly = GetAsyncKeyState(vars::fly::fly_toggle_key) & 0x8000;
        else if (vars::fly::fly_mode == 1)
        {
            static bool toggle = false;
            if (GetAsyncKeyState(vars::fly::fly_toggle_key) & 0x1) toggle = !toggle;
            should_fly = toggle;
        }

        if (!should_fly) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }

        uintptr_t character = core.get_model_instance(g_main::localplayer);
        if (!character) continue;

        uintptr_t hrp = core.find_first_child(character, "HumanoidRootPart");
        if (!hrp) continue;

        uintptr_t primitive = memory->read<uintptr_t>(hrp + offsets::Primitive);
        if (!primitive) continue;

        CFrame cf = memory->read<CFrame>(primitive + offsets::Rotation);

        vector forward = { -cf.R02, -cf.R12, -cf.R22 };
        vector right = { cf.R00,  cf.R10,  cf.R20 };

        float fwdMag = sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
        if (fwdMag > 0.001f) { forward.x /= fwdMag; forward.y /= fwdMag; forward.z /= fwdMag; }

        float rtMag = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
        if (rtMag > 0.001f) { right.x /= rtMag; right.y /= rtMag; right.z /= rtMag; }

        float moveX = 0.0f, moveZ = 0.0f;
        if (GetAsyncKeyState('D') & 0x8000) moveX += 1.0f;
        if (GetAsyncKeyState('A') & 0x8000) moveX -= 1.0f;
        if (GetAsyncKeyState('W') & 0x8000) moveZ += 1.0f;
        if (GetAsyncKeyState('S') & 0x8000) moveZ -= 1.0f;

        bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;

        float speed = vars::fly::speed * 50.0f;

        vector vel = {};
        vel.x = (right.x * moveX + forward.x * moveZ) * speed;
        vel.z = (right.z * moveX + forward.z * moveZ) * speed;
        vel.y = space ? speed : ctrl ? -speed : 0.0f;

        for (int n = 0; n < 100; n++)
        {
            memory->write<vector>(primitive + offsets::Velocity, vel);
            Sleep(0);
        }
    }
    s_running = false;
}

void c_fly::run()
{
    if (vars::fly::toggled && !s_running)
    {
        s_stop = false;
        s_running = true;
        std::thread(fly_thread).detach();
    }
    else if (!vars::fly::toggled && s_running)
    {
        s_stop = true;
    }
}