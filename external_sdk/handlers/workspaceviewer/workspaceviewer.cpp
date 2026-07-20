#include "workspaceviewer.hpp"
#include <cmath>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <winhttp.h>
#include <cstring>
#include <cstdint>
#include <zstd.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "zstd_static.lib")
#include "../misc/misc.hpp"

void c_workspace_viewer::draw_selected_instance_highlight() {
    if (selected_instance == 0) return;
    std::string class_name = core.get_instance_classname(selected_instance);
    if (class_name.find("Part") == std::string::npos && class_name.find("Model") == std::string::npos) return;
    uintptr_t instance_to_highlight = selected_instance;
    vector world_pos = { 0,0,0 };
    if (class_name.find("Model") != std::string::npos) {
        uintptr_t primary_part = 0;
        uintptr_t hrp_in_model = core.find_first_child(selected_instance, "HumanoidRootPart");
        if (hrp_in_model) primary_part = hrp_in_model;
        else { for (uintptr_t child : core.children(selected_instance)) { if (core.get_instance_classname(child).find("Part") != std::string::npos) { primary_part = child; break; } } }
        if (primary_part) instance_to_highlight = primary_part; else return;
    }
    uintptr_t primitive = memory->read<uintptr_t>(instance_to_highlight + offsets::Primitive);
    if (!primitive) return;
    world_pos = memory->read<vector>(primitive + offsets::Position);
    vector2d screen_pos; view_matrix_t view_matrix;
    uintptr_t local_player_obj = g_main::localplayer; if (!local_player_obj) return;
    uintptr_t player_mouse = memory->read<uintptr_t>(local_player_obj + offsets::Player::Mouse); if (!player_mouse) return;
    uintptr_t camera_obj = memory->read<uintptr_t>(player_mouse + offsets::Workspace::CurrentCamera); if (!camera_obj) return;
    view_matrix = memory->read<view_matrix_t>(g_main::v_engine + offsets::viewmatrix);
    if (core.world_to_screen(world_pos, screen_pos, view_matrix)) {
        float fixed_box_size = 50.0f;
        ImVec2 tl(screen_pos.x - fixed_box_size/2, screen_pos.y - fixed_box_size/2);
        ImVec2 br(screen_pos.x + fixed_box_size/2, screen_pos.y + fixed_box_size/2);
        draw.outlined_rectangle(tl, br, ImColor(255,255,0,255), ImColor(0,0,0,255), 1.0f);
    }
}

static std::string rsb1_decompress(const std::string& c) {
    if (c.size() < 12) return {};
    uint8_t h[4]; memcpy(h, c.data(), 4);
    for (int i = 0; i < 4; i++) h[i] = (h[i] ^ (uint8_t)"RSB1"[i]) - i * 41;
    std::vector<uint8_t> v(c.begin(), c.end());
    for (size_t i = 0; i < v.size(); i++) v[i] ^= h[i % 4] + i * 41;
    int len; memcpy(&len, v.data() + 4, 4);
    if (len <= 0 || len > 0x1000000) return {};
    std::string out(len, 0);
    size_t r = ZSTD_decompress(out.data(), len, v.data() + 8, v.size() - 8);
    if (r != (size_t)len) return {};
    return out;
}

std::string c_workspace_viewer::dump_script_to_string(uintptr_t script_instance, const std::string& script_type) {
    std::string script_name = core.get_instance_name(script_instance);

    uintptr_t embedded = 0;
    for (uintptr_t off = 0x100; off < 0x300; off += 8) {
        uintptr_t cand = memory->read<uintptr_t>(script_instance + off);
        if (cand < 0x10000) continue;
        uintptr_t ptr = memory->read<uintptr_t>(cand + 0x10);
        uint64_t sz = memory->read<uint64_t>(cand + 0x20);
        if (ptr > 0x10000 && sz > 8 && sz < 0x1000000) {
            uint8_t m[12];
            for (int j = 0; j < 12; j++) m[j] = memory->read<uint8_t>(ptr + j);
            uint8_t hh[4];
            for (int j = 0; j < 4; j++) hh[j] = (m[j] ^ (uint8_t)"RSB1"[j]) - j * 41;
            for (int j = 0; j < 12; j++) m[j] ^= hh[j % 4] + j * 41;
            if (m[0] == 'R' && m[1] == 'S' && m[2] == 'B' && m[3] == '1') {
                int dsize; memcpy(&dsize, m + 4, 4);
                if (dsize > 0 && dsize < 0x1000000) { embedded = cand; break; }
            }
        }
    }
    if (!embedded) return "Error: Could not find embedded struct";

    uintptr_t bc_ptr = memory->read<uintptr_t>(embedded + 0x10);
    uint64_t bc_size = memory->read<uint64_t>(embedded + 0x20);
    if (!bc_ptr || bc_ptr < 0x10000 || bc_size == 0 || bc_size > 0x1000000)
        return "Error: Invalid bytecode";

    std::string raw(static_cast<size_t>(bc_size), '\0');
    ReadProcessMemory(memory->get_handle(), (LPCVOID)bc_ptr, &raw[0], (SIZE_T)bc_size, NULL);

    std::string decompressed = rsb1_decompress(raw);
    if (decompressed.empty()) return "Error: RSB1 decompress failed";

    // Base64 encode
    static const char* b64t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    for (size_t i = 0; i < decompressed.size(); i += 3) {
        uint32_t n = ((uint8_t)decompressed[i] << 16) | ((i+1<decompressed.size()?(uint8_t)decompressed[i+1]:0) << 8) | (i+2<decompressed.size()?(uint8_t)decompressed[i+2]:0);
        b64 += b64t[(n>>18)&63]; b64 += b64t[(n>>12)&63];
        b64 += (i+1<decompressed.size())?b64t[(n>>6)&63]:'='; b64 += (i+2<decompressed.size())?b64t[n&63]:'=';
    }

    // POST to lua.expert
    std::string result;
    std::string body = "{\"script\":\"" + b64 + "\"}";
    HINTERNET hS = WinHttpOpen(L"luax/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (hS) {
        HINTERNET hC = WinHttpConnect(hS, L"api.lua.expert", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (hC) {
            HINTERNET hR = WinHttpOpenRequest(hC, L"POST", L"/decompile", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
            if (hR) {
                std::wstring wh = L"Content-Type: application/json\r\n";
                WinHttpAddRequestHeaders(hR, wh.c_str(), (DWORD)wh.size(), WINHTTP_ADDREQ_FLAG_ADD);
                WinHttpSendRequest(hR, NULL, 0, (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);
                WinHttpReceiveResponse(hR, NULL);
                char buf[4096]; DWORD n;
                while (WinHttpReadData(hR, buf, sizeof(buf)-1, &n) && n>0) { buf[n]=0; result+=buf; }
                WinHttpCloseHandle(hR);
            }
            WinHttpCloseHandle(hC);
        }
        WinHttpCloseHandle(hS);
    }
    if (result.empty()) result = "(no response)";
    // Strip lua.expert ad comment
    size_t pos = result.find("-- https://lua.expert/");
    if (pos != std::string::npos) {
        size_t end = result.find('\n', pos);
        if (end != std::string::npos) result.erase(pos, end - pos + 1);
        else result.erase(pos);
    }
    return script_name + " [" + script_type + "]\n\n" + result;
}

void c_workspace_viewer::draw_properties() {
    ImGui::BeginChild("Properties", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::Text("Properties"); ImGui::Separator();
    if (selected_instance == 0) { ImGui::Text("Select an instance to view its properties."); ImGui::EndChild(); return; }
    ImGui::Text("Address: 0x%llX", selected_instance);
    std::string name = core.get_instance_name(selected_instance); ImGui::Text("Name: %s", name.c_str());
    std::string class_name = core.get_instance_classname(selected_instance); ImGui::Text("ClassName: %s", class_name.c_str());
    ImGui::Separator();
    if (class_name == "LocalScript" || class_name == "ModuleScript") {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        if (ImGui::Button("View Script")) { script_viewer_content = dump_script_to_string(selected_instance, class_name); script_viewer_title = name + " [" + class_name + "]"; show_script_viewer = true; }
        ImGui::PopStyleColor(); ImGui::Separator();
    }
    bool anchored = memory->read<bool>(selected_instance + offsets::Anchored); ImGui::Checkbox("Anchored", &anchored);
    vector position = memory->read<vector>(selected_instance + offsets::Position); ImGui::Text("Position: %.3f, %.3f, %.3f", position.x, position.y, position.z);
    if (class_name == "Player") { ImGui::Separator(); if (ImGui::Button("Teleport to Player")) misc.teleport_to(selected_instance); }
    else if (class_name.find("Part") != std::string::npos || class_name.find("Model") != std::string::npos) {
        ImGui::Separator();
        if (ImGui::Button("Teleport to")) {
            uintptr_t m = core.get_model_instance(g_main::localplayer);
            if (!m) { ImGui::EndChild(); return; }
            uintptr_t hrp = core.find_first_child(m, "HumanoidRootPart"); if (!hrp) { ImGui::EndChild(); return; }
            uintptr_t pl = memory->read<uintptr_t>(hrp + offsets::Primitive); if (!pl) { ImGui::EndChild(); return; }
            vector tp = {0,0,0}; bool ok = false;
            if (core.get_instance_classname(selected_instance).find("Model") != std::string::npos) {
                uintptr_t pp = core.find_first_child(selected_instance, "HumanoidRootPart");
                if (!pp) for (uintptr_t c : core.children(selected_instance)) if (core.get_instance_classname(c).find("Part") != std::string::npos) { pp = c; break; }
                if (pp) { uintptr_t pr = memory->read<uintptr_t>(pp + offsets::Primitive); if (pr) { tp = memory->read<vector>(pr + offsets::Position); ok = true; } }
            } else { uintptr_t pr = memory->read<uintptr_t>(selected_instance + offsets::Primitive); if (pr) { tp = memory->read<vector>(pr + offsets::Position); ok = true; } }
            if (ok) { tp.y += vars::misc::teleport_offset_y; misc.teleport_to_position(pl, tp); }
        }
    }
    ImGui::EndChild();
}

void c_workspace_viewer::draw_instance_node(uintptr_t instance) {
    if (instance == 0) return;
    bool is_sel = (navigated_instance == instance);
    if (is_sel) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (selected_instance == instance) f |= ImGuiTreeNodeFlags_Selected;
    if (is_sel) f |= ImGuiTreeNodeFlags_Selected;
    std::string n = core.get_instance_name(instance), cn = core.get_instance_classname(instance);
    bool open = ImGui::TreeNodeEx((void*)(intptr_t)instance, f, "%s", (n + " [" + cn + "]").c_str());
    if (is_sel) { ImGui::PopStyleColor(); if (pending_scroll) { ImGui::SetScrollHereY(); pending_scroll = false; } }
    if (ImGui::IsItemClicked()) selected_instance = instance;
    if (open) { for (uintptr_t c : core.children(instance)) draw_instance_node(c); ImGui::TreePop(); }
}

void c_workspace_viewer::search_recursive(uintptr_t i, const std::string& q, std::vector<uintptr_t>& r) {
    if (!i || r.size() >= 100) return;
    std::string nm = core.get_instance_name(i), cn = core.get_instance_classname(i);
    bool exact = !q.empty() && q[0] == '=';
    std::string qq = exact ? q.substr(1) : q;
    for (char& c : nm) c = tolower(c); for (char& c : cn) c = tolower(c);
    std::string ql = qq; for (char& c : ql) c = tolower(c);
    bool match = exact ? (cn == ql) : (nm.find(ql) != std::string::npos || cn.find(ql) != std::string::npos);
    if (match) r.push_back(i);
    for (uintptr_t c : core.children(i)) search_recursive(c, q, r);
}

void c_workspace_viewer::perform_search(uintptr_t root, const std::string& query) {
    search_results.clear();
    if (query.empty() || !root) { search_performed = false; return; }
    search_recursive(root, query, search_results); search_performed = true;
}

void c_workspace_viewer::run() {
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Workspace Viewer", &vars::misc::show_workspace_viewer)) {
        if (ImGui::Button("Workspace")) { navigated_instance = selected_instance = core.find_first_child_class(g_main::datamodel, "Workspace"); pending_scroll = true; search_performed = false; }
        ImGui::SameLine(); if (ImGui::Button("ReplicatedStorage")) { if (!cached_rs) { search_results.clear(); search_recursive(g_main::datamodel, "replicatedstorage", search_results); cached_rs = search_results.size() >= 2 ? search_results[1] : (!search_results.empty() ? search_results[0] : 0); search_results.clear(); } if (cached_rs) { pending_scroll = true; navigated_instance = selected_instance = cached_rs; } search_performed = false; }
        ImGui::SameLine(); if (ImGui::Button("Players")) { navigated_instance = selected_instance = core.find_first_child_class(g_main::datamodel, "Players"); pending_scroll = true; search_performed = false; }
        ImGui::Separator();
        ImGui::Text("Search:"); ImGui::SameLine(); ImGui::SetNextItemWidth(200);
        bool ep = ImGui::InputText("##search", search_buffer, sizeof(search_buffer), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(); if (ImGui::Button("Go") || ep) { if (g_main::datamodel) perform_search(g_main::datamodel, search_buffer); }
        ImGui::SameLine(); if (ImGui::Button("Clear")) { search_buffer[0] = '\0'; search_results.clear(); search_performed = false; }
        if (search_performed) { ImGui::SameLine(); ImGui::Text("(%d)", (int)search_results.size()); }
        ImGui::Separator();
        ImGui::BeginChild("LeftPanel", ImVec2(300, 0), ImGuiChildFlags_Borders);
        if (search_performed && !search_results.empty()) { ImGui::Text("Results:"); for (uintptr_t r : search_results) { std::string nm = core.get_instance_name(r), cn = core.get_instance_classname(r); if (ImGui::Selectable((nm + " [" + cn + "]").c_str(), selected_instance == r)) selected_instance = r; } }
        else if (search_performed) ImGui::Text("No results");
        else if (g_main::datamodel) draw_instance_node(g_main::datamodel);
        ImGui::EndChild(); ImGui::SameLine(); draw_properties();
    }
    ImGui::End();

    if (show_script_viewer) {
        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(script_viewer_title.c_str(), &show_script_viewer)) {
            static std::string prev; static std::vector<std::string> lines;
            static std::vector<char> sel; static int lc = 0;
            static int last = -1;
            if (script_viewer_content != prev) {
                prev = script_viewer_content;
                lines.clear();
                std::istringstream iss(prev); std::string ln;
                while (std::getline(iss, ln)) lines.push_back(ln);
                sel.assign(lines.size(), 0); last = -1;
                lc = (int)lines.size();
            }
            ImGui::Text("Line Count: %d", lc);
            ImGui::SameLine();
            if (ImGui::Button("Copy All")) ImGui::SetClipboardText(prev.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Copy Selected")) {
                std::string out; bool first = true;
                for (int i = 0; i < lc; i++)
                    if (sel[i]) { if (!first) out += "\n"; out += lines[i]; first = false; }
                ImGui::SetClipboardText(out.c_str());
            }
            ImGui::BeginChild("##sc", ImVec2(0,0), ImGuiChildFlags_Borders);
            // Ctrl+C to copy selected lines
            if (ImGui::IsWindowFocused() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
                std::string out; bool first = true;
                for (int i = 0; i < lc; i++)
                    if (sel[i]) { if (!first) out += "\n"; out += lines[i]; first = false; }
                if (!out.empty()) ImGui::SetClipboardText(out.c_str());
            }
            ImGuiListClipper clip; clip.Begin(lc);
            while (clip.Step()) {
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                    std::string lbl = std::to_string(i+1) + "  " + lines[i];
                    bool b = sel[i] != 0;
                    if (ImGui::Selectable(lbl.c_str(), &b, 0)) {
                        if (ImGui::GetIO().KeyShift && last >= 0) {
                            int a = last < i ? last : i, z = last < i ? i : last;
                            for (int j = a; j <= z; j++) sel[j] = 1;
                        } else { for (int j = 0; j < lc; j++) sel[j] = 0; sel[i] = 1; }
                        last = i;
                    }
                }
            }
            ImGui::EndChild();
            ImGui::End();
        }
    }
}