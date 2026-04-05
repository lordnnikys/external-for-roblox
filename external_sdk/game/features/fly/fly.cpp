#include "fly.hpp"
#include "../../../handlers/vars.hpp"
#include "../../../game/offsets/offsets.hpp"
#include "../../../game/core.hpp"
#include "../../../addons/kernel/memory.hpp"
#include <Windows.h>
#include <iostream>
#include <cmath>

static bool fly_toggled = false;
static uintptr_t cached_workspace = 0;
static uintptr_t cached_camera = 0;

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

    // Cache camera lookup instead of searching every frame
    if (!cached_workspace || !cached_camera)
    {
        cached_workspace = core.find_first_child_class(g_main::datamodel, "Workspace");
        if (cached_workspace)
            cached_camera = memory->read<uintptr_t>(cached_workspace + offsets::Workspace::CurrentCamera);
    }
    if (!cached_camera)
        return;

    // OPTIMIZATION: Read Camera CFrame rotation directly instead of relying on character CFrame.
    // Camera rotation stores: X = pitch, Y = yaw, Z = roll
    // This fixes the "must move camera left/right to fly straight" issue — previously fly
    // used the character's CFrame which doesn't update unless the character physically moves.
    vector cam_rot = memory->read<vector>(cached_camera + offsets::CameraRotation);
    float yaw = cam_rot.y;    // yaw (horizontal angle)
    float pitch = cam_rot.x;  // pitch (vertical angle)

    // Build camera-relative forward/right vectors from yaw/pitch
    vector forward, right;
    forward.x = sinf(yaw);
    forward.y = sinf(pitch);
    forward.z = cosf(yaw);

    right.x = cosf(yaw);
    right.y = 0.0f;
    right.z = -sinf(yaw);

    // WASD movement (camera-relative horizontal plane)
    float x_input = 0.0f;
    float z_input = 0.0f;
    if (GetAsyncKeyState('W') & 0x8000) z_input = 1.0f;
    if (GetAsyncKeyState('S') & 0x8000) z_input = -1.0f;
    if (GetAsyncKeyState('A') & 0x8000) x_input = -1.0f;
    if (GetAsyncKeyState('D') & 0x8000) x_input = 1.0f;

    // Up/Down input
    float up_input = 0.0f;
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) up_input = 1.0f;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) up_input = -1.0f;

    // Check if we have any active input
    bool has_input = (x_input != 0.0f || z_input != 0.0f || up_input != 0.0f);

    float fly_speed = vars::fly::speed * 120.0f;

    if (!has_input)
    {
        // OPTIMIZATION: Dampen velocity when no input (smooth deceleration)
        vector dampened_vel = memory->read<vector>(primitive + offsets::AssemblyLinearVelocity);
        dampened_vel.x *= 0.85f;
        dampened_vel.y *= 0.85f;
        dampened_vel.z *= 0.85f;
        memory->write<vector>(primitive + offsets::AssemblyLinearVelocity, dampened_vel);
    }
    else
    {
        // Normalize inputs and apply speed
        float len = sqrtf(x_input * x_input + z_input * z_input + up_input * up_input);
        if (len > 0.001f) len = 1.0f / len;

        x_input *= len;
        z_input *= len;
        up_input *= len;

        // Assemble velocity: XZ from camera-relative WASD, Y from up/down
        vector velocity;
        velocity.x = (forward.x * z_input + right.x * x_input) * fly_speed;
        velocity.y = up_input * fly_speed;
        velocity.z = (forward.z * z_input + right.z * x_input) * fly_speed;

        memory->write<vector>(primitive + offsets::AssemblyLinearVelocity, velocity);
    }

    // Noclip through walls
    uint8_t prim_flags = memory->read<uint8_t>(primitive + offsets::PrimitiveFlags);
    prim_flags &= ~offsets::CanCollideMask;
    memory->write<uint8_t>(primitive + offsets::PrimitiveFlags, prim_flags);
}
