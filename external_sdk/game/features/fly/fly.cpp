#include "fly.hpp"
#include "../../../handlers/vars.hpp"
#include "../../../game/offsets/offsets.hpp"
#include "../../../game/core.hpp"
#include "../../../addons/kernel/memory.hpp"

static bool fly_toggled = false;
static float yaw = 0.0f;      // Cached yaw for stable camera-relative movement
static float pitch = 0.0f;    // Cached pitch
static bool prev_space = false;
static bool prev_ctrl = false;

void c_fly::run()
{
    if (!g_main::localplayer) return;
    if (!vars::fly::toggled) return;

    bool should_fly = false;

    if (vars::fly::fly_mode == 0) // Hold mode
    {
        should_fly = GetAsyncKeyState(vars::fly::fly_toggle_key) & 0x8000;
    }
    else if (vars::fly::fly_mode == 1) // Toggle mode
    {
        if (GetAsyncKeyState(vars::fly::fly_toggle_key) & 0x1)
        {
            fly_toggled = !fly_toggled;
        }
        should_fly = fly_toggled;
    }

    if (!should_fly) return;

    uintptr_t character = core.get_model_instance(g_main::localplayer);
    if (!character) return;

    uintptr_t humanoidRootPart = core.find_first_child(character, "HumanoidRootPart");
    if (!humanoidRootPart) return;

    uintptr_t humanoid = core.find_first_child_class(character, "Humanoid");
    if (!humanoid) return;

    uintptr_t primitive = memory->read<uintptr_t>(humanoidRootPart + offsets::Primitive);
    if (!primitive) return;

    // Disable collision while flying
    memory->write<bool>(primitive + offsets::CanCollide, false);

    // Read current CFrame to get camera orientation
    Matrix3 cf = memory->read<Matrix3>(primitive + offsets::CFrame);

    // Extract forward (Z column) and right (X column) from the CFrame
    vector forward = { -cf.R02, -cf.R12, -cf.R22 };
    vector right   = {  cf.R00,  cf.R10,  cf.R20 };
    vector up      = {  0.0f,   1.0f,    0.0f  };

    // Normalize to prevent drift
    float fwdMag = sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
    if (fwdMag > 0.001f) { forward.x /= fwdMag; forward.y /= fwdMag; forward.z /= fwdMag; }

    float rtMag = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
    if (rtMag > 0.001f) { right.x /= rtMag; right.y /= rtMag; right.z /= rtMag; }

    // WASD input (camera-relative, NOT MoveDirection which depends on Roblox's input)
    float moveX = 0.0f, moveZ = 0.0f;
    if (GetAsyncKeyState('D') & 0x8000) moveX += 1.0f;
    if (GetAsyncKeyState('A') & 0x8000) moveX -= 1.0f;
    if (GetAsyncKeyState('W') & 0x8000) moveZ += 1.0f;
    if (GetAsyncKeyState('S') & 0x8000) moveZ -= 1.0f;

    // Build velocity from camera-relative axes
    float speed = vars::fly::speed * 50.0f;

    vector velocity;
    velocity.x = (right.x * moveX + forward.x * moveZ) * speed;
    velocity.z = (right.z * moveX + forward.z * moveZ) * speed;

    // Up/Down — apply directly to Y axis for stable vertical movement
    bool space_now = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    bool ctrl_now  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    if (space_now) velocity.y += speed;
    if (ctrl_now)  velocity.y -= speed;

    // When no input, dampen velocity to prevent sliding
    if (moveX == 0.0f && moveZ == 0.0f && !space_now && !ctrl_now)
    {
        // Read current velocity and dampen it
        vector vel = memory->read<vector>(primitive + offsets::Velocity);
        vel.x *= 0.85f;
        vel.y *= 0.85f;
        vel.z *= 0.85f;
        memory->write<vector>(primitive + offsets::Velocity, vel);
        return;
    }

    memory->write<vector>(primitive + offsets::Velocity, velocity);
}
