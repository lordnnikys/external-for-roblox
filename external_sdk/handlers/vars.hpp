#pragma once

#include <windows.h>
#include "../addons/imgui/imgui.h" // Corrected Include ImGui for ImColor
#include <cmath> // For sqrtf
#include <string> // Required for std::string
#include <chrono>
#include <vector>

// Define 
// and CFrame structs first

struct Matrix3 {
    float data[9]; // Represents a 3x3 matrix

    Matrix3() {
        for (int i = 0; i < 9; ++i) data[i] = 0.0f;
    }
};

// Now define the vars namespace that uses the vector struct
namespace vars
{
    namespace esp
    {

        inline bool toggled = true;
        inline bool show_health = true;
        inline bool show_distance = true;
        inline bool show_skeleton = false;
        inline bool show_tracers = true;
        inline bool show_box = true;
        inline bool hide_dead = true;
        inline bool hide_teammates = true;
        inline bool show_fps = true;
        inline bool show_weapon = true;
        inline bool chams_enabled = false;
        inline bool chams_visible_only = false;
        inline bool chams_enemies_only = true;
        inline float chams_transparency = 0.5f;
        inline float chams_opacity = 0.5f;

        inline ImColor esp_box_color = ImColor(255, 255, 255, 255);
        inline ImColor esp_name_color = ImColor(255, 255, 255, 255);
        inline ImColor esp_distance_color = ImColor(255, 255, 255, 255);
        inline ImColor esp_skeleton_color = ImColor(255, 0, 0, 255);
        inline ImColor esp_tracer_color = ImColor(255, 255, 255, 255);
        inline ImColor chams_visible_color = ImColor(0, 255, 0, 255);
        inline ImColor chams_invisible_color = ImColor(255, 0, 0, 255);
    }

    namespace triggerbot
    {
        inline bool toggled = false;
        inline int activation_key = VK_XBUTTON1;
        inline bool ignore_teammates = true;
        inline float fov = 15.0f;
        inline int delay = 50;
        inline int hold_time = 50;
        inline bool use_prediction = true;      // NEW: Use aimbot's prediction
        inline bool only_when_aiming = false;   // NEW: Only trigger when aimbot is aiming
        inline bool hit_chance_enabled = false; // NEW: Random chance to fire
        inline int hit_chance = 100;            // NEW: Percentage (0-100)
    }

    namespace aimbot
    {
        inline bool toggled = true;
        inline float speed = 1.0f;
        inline float fov = 100.0f;
        inline float deadzone = 5.0f;
        inline bool use_set_cursor_pos = false;
        inline float smoothing_factor = 0.2f;
        inline int activation_key = 0x02;
        inline int aimbot_hitbox = 0; // 0 = Head, 1 = Body ()
        inline bool ignore_teammates = true;
        inline bool show_fov_circle = true; // New: Toggle for FOV circle
        inline bool prediction = false; // Toggle for aimbot prediction
        inline bool prediction_auto = true;  // Auto-tune all prediction values (recommended)
        inline float prediction_time = 0.08f;  // How far ahead to predict (seconds). 0.05-0.20 typical range
        inline float prediction_smoothing = 0.4f;  // EMA alpha for velocity smoothing (0.1=very smooth, 1.0=raw). Lower = less jitter
        inline bool prediction_use_acceleration = false;  // Use quadratic prediction (vel + 0.5*accel*t^2)
        inline float prediction_max_velocity = 100.0f;  // Clamp velocity to reject teleports/respawns (studs/s)
        inline bool prediction_ignore_y = true;  // Ignore vertical velocity (good for grounded games)
        inline bool sticky_aim = false;
        inline int target_selection = 0;

        // Air Part (different hitbox when target is jumping)
        inline bool air_part_enabled = false;
        inline int air_part_hitbox = 1; // 0 = Head, 1 = Body

        // Anti-Flick
        inline bool anti_flick = false;
        inline float anti_flick_distance = 500.0f;

        // Smoothing Style
        inline int smoothing_style = 1; // 0 = None, 1 = Linear, 2 = EaseIn, 3 = EaseOut, 4 = EaseInOut

        // Shake (humanization)
        inline bool shake = false;
        inline float shake_x = 2.0f;
        inline float shake_y = 2.0f;

        // Unlock on death
        inline bool unlock_on_death = true;
    }

    namespace combat {
        inline bool hitbox_expander = false;
        inline float hitbox_size_x = 8.0f;
        inline float hitbox_size_y = 8.0f;
        inline float hitbox_size_z = 8.0f;
        inline float hitbox_multiplier = 2.0f;
        inline bool hitbox_visible = false;
        inline bool hitbox_can_collide = false;
        inline bool hitbox_skip_teammates = true;
    }

    namespace speed_hack
    {
        inline bool toggled = false;
        inline float value = 16.0f;
    }

    namespace freecam
    {
        inline bool toggled = false;
        inline float speed = 2.0f;
        inline float sensitivity = 0.003f;
    }

    namespace set_fov 
    {
        inline float set_fov = 70.0f;
        inline bool toggled = false;
        inline bool unlock_zoom = false;
    }

    namespace noclip
    {
        inline bool toggled = false;
    }

    namespace jump_power
    {
        inline bool toggled = false;
        inline float value = 50.0f;
        inline float default_value = 50.0f;
    }

    namespace infinite_jump
    {
        inline bool toggled = false;
        inline float jump_power_value = 5000.0f;
    }

    namespace fly
    {
        inline bool toggled = false;
        inline float speed = 1.0f;
        inline int fly_toggle_key = 0x51; // Q key
        inline int fly_mode = 0; // 0 = Hold, 1 = Toggle
    }

    namespace airswim
    {
        inline bool toggled = false;
    }

    namespace misc
    {
        inline bool show_workspace_viewer = false;
        inline int font_index = 0;
        inline std::vector<ImFont*> fonts;
        inline std::vector<std::string> font_names;
        inline float teleport_offset_x = 0.0f;
        inline float teleport_offset_y = 3.0f;  // Default slight Y offset to spawn above target
        inline float teleport_offset_z = 0.0f;
        inline uintptr_t selected_player_for_info = 0;
        inline std::string spectating_player_name = "";
        inline uintptr_t spectating_camera = 0;
    }

    // Desync controls
    namespace desync
    {
        inline bool enabled = false; // Master toggle for desync feature
        // When true, writes a float value that corresponds to "yes" in the python script
        inline bool desync_on = false; // true = yes, false = no
        // Offset from module base
        inline uintptr_t offset = 0x69F6E10;
        // Sleep interval in milliseconds for writes
        inline int interval_ms = 200;
    }

    // Minimal Bee Swarm Simulator (BSS) stubs to satisfy remaining references
    // All features are disabled by default.
    namespace anti_afk
    {
        inline bool toggled = false;
        inline float interval = 15.0f;
    }

    namespace lag_switch
    {
        inline bool toggled = false;
        inline int activation_key = 0x4C; // L key
        inline float lag_duration = 0.5f; // How long to lag (seconds)
        inline float lag_interval = 2.0f; // How often to lag (seconds)
        inline bool auto_lag = false; // Automatically lag at intervals
        inline bool manual_lag = true; // Manual lag on key press
    }

    namespace luavm {
        inline bool installed = false;
        inline bool patches_applied = false;
        inline int patched_count = 0;
        inline uintptr_t codecave = 0;
        inline uintptr_t remote_vtable = 0;
        inline uintptr_t original_vtable = 0;
        inline char status_msg[256] = "Ready.";
    }

    // New exploits namespace
    namespace exploits
    {
        inline bool enabled = false; // Master toggle for exploits
        inline bool friendly_fire = false; // Toggle to enable friendly fire
    }
}