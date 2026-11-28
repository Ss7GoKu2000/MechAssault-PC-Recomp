#include "app/app.h"
#include "core/input.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstdio>

#include <imgui.h>

#include "core/diag.h"

namespace {
std::mutex keys_mutex;
std::bitset<256> keys;
constexpr unsigned menu_keys[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE, VK_BACK};
std::array<ULONGLONG, std::size(menu_keys)> menu_taps{};
uint32_t move_forward = 'W';
uint32_t move_backward = 'S';
uint32_t move_left = 'A';
uint32_t move_right = 'D';
uint32_t mouse_sensitivity = 100;
bool mouse_invert_y = false;
uint32_t fire_weapon = VK_LBUTTON;
uint32_t select_energy = '1';
uint32_t select_ballistic = '2';
uint32_t select_missile = '3';
uint32_t cycle_weapon = 0;
uint32_t jump_jets = VK_SPACE;
bool mouse_wheel_cycle = true;
int wheel_remainder = 0;
int wheel_steps = 0;
ULONGLONG wheel_press_until = 0;
ULONGLONG wheel_release_until = 0;
int mouse_x = 0;
int mouse_y = 0;
std::atomic<ULONGLONG> last_game_camera{0};
std::atomic<ULONGLONG> last_pause_menu{0};
std::atomic<bool> pause_has_native_graphics{false};
constexpr UINT_PTR kMouseTimer = 0x4D41;
constexpr UINT kOpenGraphics = WM_APP + 0x4D;
constexpr ULONGLONG kCameraTimeoutMs = 200;
bool cursor_locked = false;
bool pause_requested = false;
bool pause_menu_was_visible = false;
bool start_held = false;
bool escape_as_start = false;
bool controls_open = false;
bool controls_changed = false;
bool graphics_open = false;
bool pause_graphics_selected = false;
bool pad_menu_right = false;
bool pad_menu_accept = false;
bool pad_menu_back = false;
bool consume_menu_back = false;
POINT locked_center{};

app::GraphicsSettings DefaultGraphics() {
    const app::Settings defaults;
    return {defaults.display_mode, defaults.width, defaults.height, defaults.render_percent,
            defaults.fps_cap, defaults.brightness_percent, defaults.vsync};
}

app::GraphicsSettings graphics_draft = DefaultGraphics();
app::GraphicsSettings graphics_previous = DefaultGraphics();
ULONGLONG graphics_deadline = 0;

void ApplyGraphics() {
    graphics_previous = app::ReadGraphics();
    const bool changes_display = graphics_draft.mode != graphics_previous.mode ||
        (graphics_draft.mode != app::DisplayMode::Borderless &&
         (graphics_draft.width != graphics_previous.width || graphics_draft.height != graphics_previous.height));
    app::SetGraphics(graphics_draft);
    if (changes_display) {
        app::ApplyDisplay();
        graphics_deadline = GetTickCount64() + 15000;
    } else {
        app::Save();
    }
}

void KeepGraphics() {
    graphics_deadline = 0;
    app::Save();
}

void RevertGraphics() {
    app::SetGraphics(graphics_previous);
    graphics_draft = graphics_previous;
    graphics_deadline = 0;
    app::ApplyDisplay();
}

bool GameCameraActive() {
    const auto last = last_game_camera.load(std::memory_order_relaxed);
    return last && GetTickCount64() - last <= kCameraTimeoutMs;
}

bool PauseMenuActive() {
    const auto last = last_pause_menu.load(std::memory_order_relaxed);
    return last && GetTickCount64() - last <= kCameraTimeoutMs;
}

bool PauseGraphicsOverlayAvailable() {
    return PauseMenuActive() && !pause_has_native_graphics.load(std::memory_order_relaxed);
}

void ResetWheel() {
    wheel_remainder = wheel_steps = 0;
    wheel_press_until = wheel_release_until = 0;
}

bool WheelPressed() {
    const auto now = GetTickCount64();
    if (wheel_press_until) {
        if (now < wheel_press_until) return true;
        wheel_press_until = 0;
        wheel_release_until = now + 40;
        return false;
    }
    if (!wheel_steps || now < wheel_release_until) return false;
    --wheel_steps;
    wheel_press_until = now + 40;
    return true;
}

void ReleaseCursor() {
    if (!cursor_locked) return;
    ClipCursor(nullptr);
    ShowCursor(TRUE);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    cursor_locked = false;
    mouse_x = mouse_y = 0;
    ResetWheel();
}

void UpdateCursor(HWND window) {
    const bool gameplay = GameCameraActive();
    const bool pause_menu = PauseMenuActive();
    if (!gameplay || (pause_menu_was_visible && !pause_menu)) pause_requested = false;
    pause_menu_was_visible = pause_menu;
    if (!gameplay || pause_menu || pause_requested || controls_open || graphics_open || !mouse_sensitivity || IsIconic(window) ||
        GetForegroundWindow() != window || GetFocus() != window || diag::overlay_open.load()) {
        ReleaseCursor();
        return;
    }

    RECT client{};
    if (!GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return;
    POINT center{client.right / 2, client.bottom / 2};
    if (!ClientToScreen(window, &center)) return;
    if (cursor_locked && locked_center.x == center.x && locked_center.y == center.y) return;
    RECT clip{center.x, center.y, center.x + 1, center.y + 1};
    if (!ClipCursor(&clip)) {
        ReleaseCursor();
        return;
    }
    locked_center = center;
    if (!cursor_locked) ShowCursor(FALSE);
    cursor_locked = true;
    SetCursor(nullptr);
}

void RegisterMouse(void* window) {
    const auto hwnd = static_cast<HWND>(window);
    RAWINPUTDEVICE mouse{1, 2, 0, hwnd};
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
    SetTimer(hwnd, kMouseTimer, 16, nullptr);
}

bool HandleKeyboard(void* window, uint32_t message, uintptr_t key, intptr_t flags, intptr_t* result) {
    std::lock_guard lock(keys_mutex);
    if (message == kOpenGraphics) {
        if (!graphics_open) graphics_draft = app::ReadGraphics();
        graphics_open = true;
        if (controls_changed) {
            app::Save();
            controls_changed = false;
        }
        controls_open = false;
        ReleaseCursor();
        keys.reset();
        menu_taps.fill(0);
        mouse_x = mouse_y = 0;
        ResetWheel();
        *result = 0;
        return true;
    }
    if (message == WM_TIMER && key == kMouseTimer) {
        if (graphics_deadline && GetTickCount64() >= graphics_deadline) RevertGraphics();
        if (!PauseMenuActive()) pause_graphics_selected = false;
        UpdateCursor(static_cast<HWND>(window));
        *result = 0;
        return true;
    }
    if (message == WM_DESTROY) KillTimer(static_cast<HWND>(window), kMouseTimer);
    if (message == WM_KILLFOCUS || message == WM_DESTROY || (message == WM_ACTIVATEAPP && !key)) {
        ReleaseCursor();
        keys.reset();
        menu_taps.fill(0);
        mouse_x = mouse_y = 0;
        ResetWheel();
    } else if (message == WM_KEYDOWN &&
               (key == VK_F7 || key == VK_F8 || ((controls_open || graphics_open) && key == VK_ESCAPE))) {
        if (!(flags & (1u << 30))) {
            if (key == VK_F8 || (graphics_open && key == VK_ESCAPE)) {
                if (graphics_deadline) RevertGraphics();
                graphics_open = key == VK_F8 && !graphics_open;
                if (graphics_open) graphics_draft = app::ReadGraphics();
                controls_open = false;
            } else if (!graphics_open) {
                controls_open = key == VK_F7 && !controls_open;
            }
            if (!controls_open && controls_changed) {
                app::Save();
                controls_changed = false;
            }
            ReleaseCursor();
            keys.reset();
            menu_taps.fill(0);
            mouse_x = mouse_y = 0;
            ResetWheel();
        }
        *result = 0;
        return true;
    } else if (message == WM_KEYDOWN && !graphics_open && !controls_open && PauseGraphicsOverlayAvailable() &&
               (key == VK_RIGHT || (pause_graphics_selected &&
                (key == VK_RETURN || key == VK_ESCAPE || key == VK_BACK || key == VK_LEFT)))) {
        if (!(flags & (1u << 30))) {
            if (key == VK_RETURN) PostMessageW(static_cast<HWND>(window), kOpenGraphics, 0, 0);
            else pause_graphics_selected = key == VK_RIGHT;
        }
        *result = 0;
        return true;
    } else if (message == WM_INPUT && GET_RAWINPUT_CODE_WPARAM(key) == RIM_INPUT &&
               !controls_open && !graphics_open && !diag::overlay_open.load()) {
        RAWINPUT input{};
        UINT size = sizeof(input);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(flags), RID_INPUT, &input, &size,
                            sizeof(RAWINPUTHEADER)) == sizeof(input) &&
            input.header.dwType == RIM_TYPEMOUSE) {
            const auto& mouse = input.data.mouse;
            if (!(mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                mouse_x = int(std::clamp<int64_t>(int64_t(mouse_x) + mouse.lLastX, -2048, 2048));
                mouse_y = int(std::clamp<int64_t>(int64_t(mouse_y) + mouse.lLastY, -2048, 2048));
            }
            constexpr uint32_t mouse_keys[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
            const bool gameplay = GameCameraActive() && !PauseMenuActive() && !pause_requested;
            for (unsigned i = 0; i < std::size(mouse_keys); ++i) {
                if (mouse.usButtonFlags & (RI_MOUSE_LEFT_BUTTON_UP << (2 * i))) keys.reset(mouse_keys[i]);
                if (gameplay && (mouse.usButtonFlags & (RI_MOUSE_LEFT_BUTTON_DOWN << (2 * i)))) keys.set(mouse_keys[i]);
            }
            if (gameplay && mouse_wheel_cycle && (mouse.usButtonFlags & RI_MOUSE_WHEEL)) {
                wheel_remainder += static_cast<SHORT>(mouse.usButtonData);
                wheel_steps = std::min(wheel_steps + std::abs(wheel_remainder / WHEEL_DELTA), 12);
                wheel_remainder %= WHEEL_DELTA;
            }
        }
    } else if ((message == WM_KEYDOWN || message == WM_KEYUP) && key < keys.size()) {
        if (message == WM_KEYUP) keys.reset(key);
        else if (!(flags & (1u << 30))) {
            if (key == VK_ESCAPE) escape_as_start = GameCameraActive() && !PauseMenuActive() && !pause_requested;
            for (size_t i = 0; i < menu_taps.size(); ++i)
                if (key == menu_keys[i]) menu_taps[i] = GetTickCount64() + 40;
            keys.set(key);
        }
    }
    return false;
}

void ApplyKeyboard(int port, input::Pad& pad) {
    if (port != 0) return;
    std::lock_guard lock(keys_mutex);
    const bool right_edge = (pad.buttons & (1 << 3)) && !pad_menu_right;
    const bool accept_edge = pad.analog[0] && !pad_menu_accept;
    const bool back_edge = pad.analog[1] && !pad_menu_back;
    pad_menu_right = (pad.buttons & (1 << 3)) != 0;
    pad_menu_accept = pad.analog[0] != 0;
    pad_menu_back = pad.analog[1] != 0;
    if (!pad_menu_back) consume_menu_back = false;
    if (consume_menu_back) pad.analog[1] = 0;
    if (graphics_open && back_edge) {
        if (graphics_deadline) RevertGraphics();
        graphics_open = false;
        consume_menu_back = true;
        pad.analog[1] = 0;
        return;
    }
    if (!controls_open && !graphics_open && PauseGraphicsOverlayAvailable()) {
        if (right_edge) pause_graphics_selected = true;
        if (pause_graphics_selected) {
            if (accept_edge) PostMessageW(static_cast<HWND>(app::Window()), kOpenGraphics, 0, 0);
            if (back_edge || (pad.buttons & (1 << 2))) pause_graphics_selected = false;
            pad = {};
            pad.connected = true;
            return;
        }
    }
    if (controls_open || graphics_open || diag::overlay_open.load()) {
        keys.reset();
        menu_taps.fill(0);
        mouse_x = mouse_y = 0;
        ResetWheel();
        if (controls_open || graphics_open) {
            const bool connected = port == 0 || pad.connected;
            pad = {};
            pad.connected = connected;
        }
        return;
    }
    pad.connected = true;
    const auto now = GetTickCount64();
    auto navigation = keys;
    for (size_t i = 0; i < menu_taps.size(); ++i)
        if (now < menu_taps[i]) navigation.set(menu_keys[i]);
    if (navigation[VK_UP]) pad.buttons |= 1 << 0;
    if (navigation[VK_DOWN]) pad.buttons |= 1 << 1;
    if (navigation[VK_LEFT]) pad.buttons |= 1 << 2;
    if (navigation[VK_RIGHT]) pad.buttons |= 1 << 3;
    if (navigation[VK_ESCAPE] && escape_as_start) pad.buttons |= 1 << 4;
    const bool start = (pad.buttons & (1 << 4)) != 0;
    if (start && !start_held && GameCameraActive()) {
        pause_requested = !pause_requested;
        mouse_x = mouse_y = 0;
    }
    start_held = start;
    if (navigation[VK_RETURN]) pad.analog[0] = 255;
    if (navigation[VK_BACK] || (navigation[VK_ESCAPE] && !escape_as_start)) pad.analog[1] = 255;

    const auto held = [](uint32_t key) { return key && key < keys.size() && keys[key]; };
    const bool left = held(move_left), right = held(move_right);
    const bool forward = held(move_forward), backward = held(move_backward);
    if (left || right) pad.lx = int16_t(right * 32767 - left * 32767);
    if (forward || backward) pad.ly = int16_t(forward * 32767 - backward * 32767);
    if (GameCameraActive() && !PauseMenuActive() && !pause_requested) {
        if (held(jump_jets)) pad.buttons |= 1 << 6;
        // Xbox input report: B selects missiles, X energy, Y ballistics, LT cycles and RT fires.
        if (held(select_missile)) pad.analog[1] = 255;
        if (held(select_energy)) pad.analog[2] = 255;
        if (held(select_ballistic)) pad.analog[3] = 255;
        const bool wheel = WheelPressed();
        if (held(cycle_weapon) || wheel) pad.analog[6] = 255;
        if (held(fire_weapon)) pad.analog[7] = 255;
    } else {
        ResetWheel();
    }

    const int sensitivity = pause_requested ? 0 : int(std::min(mouse_sensitivity, 500u));
    if (mouse_x && sensitivity)
        pad.rx = int16_t(std::clamp(mouse_x * sensitivity * 12, -32767, 32767));
    if (mouse_y && sensitivity)
        pad.ry = int16_t(std::clamp(mouse_y * sensitivity * (mouse_invert_y ? 12 : -12), -32767, 32767));
    mouse_x = mouse_y = 0;
}

void DrawControls() {
    if (!controls_open) return;

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Mouse controls", &controls_open, ImGuiWindowFlags_NoCollapse)) {
        int sensitivity = int(std::min(mouse_sensitivity, 500u));
        if (ImGui::SliderInt("Sensitivity", &sensitivity, 0, 500, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            mouse_sensitivity = uint32_t(sensitivity);
            controls_changed = true;
        }
        if (ImGui::Checkbox("Invert Y", &mouse_invert_y)) controls_changed = true;
        if (ImGui::Button("Done")) controls_open = false;
    }
    ImGui::End();
    if (!controls_open && controls_changed) {
        app::Save();
        controls_changed = false;
    }
}

void DrawOutputSize() {
    using app::DisplayMode;
    if (graphics_draft.mode == DisplayMode::Borderless) return;

    const bool fullscreen = graphics_draft.mode == DisplayMode::Fullscreen;
    const char* label = fullscreen ? "Resolution" : "Window size";
    char current[32] = "Desktop";
    if (graphics_draft.width && graphics_draft.height)
        std::snprintf(current, sizeof(current), "%u x %u", graphics_draft.width, graphics_draft.height);
    if (!ImGui::BeginCombo(label, current)) return;

    if (ImGui::Selectable("Desktop", !graphics_draft.width || !graphics_draft.height))
        graphics_draft.width = graphics_draft.height = 0;

    std::vector<std::pair<uint32_t, uint32_t>> sizes;
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    const auto hwnd = static_cast<HWND>(app::Window());
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
        if (fullscreen) {
            DEVMODEW display{};
            display.dmSize = sizeof(display);
            for (DWORD index = 0; EnumDisplaySettingsW(monitor.szDevice, index, &display); ++index)
                if (display.dmBitsPerPel >= 24 && display.dmPelsWidth >= 640 && display.dmPelsHeight >= 480)
                    sizes.emplace_back(display.dmPelsWidth, display.dmPelsHeight);
        } else {
            constexpr std::pair<uint32_t, uint32_t> presets[] = {
                {640, 480}, {960, 540}, {1024, 768}, {1280, 720}, {1600, 900},
                {1920, 1080}, {2560, 1440}, {3440, 1440}, {3840, 2160},
            };
            for (const auto& [width, height] : presets) {
                RECT outer{0, 0, LONG(width), LONG(height)};
                AdjustWindowRect(&outer, WS_OVERLAPPEDWINDOW, FALSE);
                if (outer.right - outer.left <= monitor.rcWork.right - monitor.rcWork.left &&
                    outer.bottom - outer.top <= monitor.rcWork.bottom - monitor.rcWork.top)
                    sizes.emplace_back(width, height);
            }
        }
    }
    std::sort(sizes.begin(), sizes.end());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    for (const auto& [width, height] : sizes) {
        char choice[32];
        std::snprintf(choice, sizeof(choice), "%u x %u", width, height);
        if (ImGui::Selectable(choice, width == graphics_draft.width && height == graphics_draft.height)) {
            graphics_draft.width = width;
            graphics_draft.height = height;
        }
    }
    ImGui::EndCombo();
}

void DrawGraphics() {
    if (!graphics_open) return;
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Graphics", &graphics_open, ImGuiWindowFlags_NoCollapse)) {
        if (graphics_deadline) {
            const auto now = GetTickCount64();
            if (now >= graphics_deadline) {
                RevertGraphics();
            } else {
                ImGui::Text("Keep changes? Reverting in %d seconds.", int((graphics_deadline - now + 999) / 1000));
                if (ImGui::Button("Keep changes")) KeepGraphics();
                ImGui::SameLine();
                if (ImGui::Button("Revert")) RevertGraphics();
            }
        } else {
            constexpr const char* modes[] = {"Fullscreen", "Borderless", "Windowed"};
            int mode = int(graphics_draft.mode);
            if (ImGui::Combo("Display mode", &mode, modes, IM_ARRAYSIZE(modes))) {
                graphics_draft.mode = app::DisplayMode(mode);
                graphics_draft.width = graphics_draft.height = 0;
            }
            ImGui::SetItemDefaultFocus();
            DrawOutputSize();
            const auto slider = [](const char* label, uint32_t& value, int min, int max, const char* format) {
                int edited = int(std::min(value, uint32_t(max)));
                if (ImGui::SliderInt(label, &edited, min, max, format, ImGuiSliderFlags_AlwaysClamp))
                    value = uint32_t(edited);
            };
            slider("Render scale", graphics_draft.scale, 50, 200, "%d%%");
            ImGui::Checkbox("VSync", &graphics_draft.vsync);
            const char* cap_format = graphics_draft.cap == 0 ? "Unlimited"
                : graphics_draft.cap == app::kFpsCapRefresh ? "Refresh rate" : "%d FPS";
            slider("Frame-rate cap", graphics_draft.cap, 0, 360, cap_format);
            slider("Brightness", graphics_draft.brightness, 50, 150, "%d%%");
            if (ImGui::Button("Apply")) ApplyGraphics();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) graphics_open = false;
            ImGui::SameLine();
            if (ImGui::Button("Restore defaults")) graphics_draft = DefaultGraphics();
        }
    }
    ImGui::End();
    if (!graphics_open && graphics_deadline) RevertGraphics();
}

void DrawPauseGraphicsEntry() {
    if (graphics_open || controls_open || !PauseGraphicsOverlayAvailable()) return;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 anchor(viewport->Pos.x + viewport->Size.x / 2,
                        viewport->Pos.y + viewport->Size.y * 0.89f);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##PauseGraphics", nullptr, flags)) {
        if (pause_graphics_selected)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button("GRAPHICS", ImVec2(210, 28)))
            PostMessageW(static_cast<HWND>(app::Window()), kOpenGraphics, 0, 0);
        if (pause_graphics_selected) ImGui::PopStyleColor();
        ImGui::TextUnformatted(pause_graphics_selected ? "Enter / A   Select" : "Right   Select Graphics");
    }
    ImGui::End();
}

void DrawSettings(double) {
    std::lock_guard lock(keys_mutex);
    DrawControls();
    DrawGraphics();
    DrawPauseGraphicsEntry();
}
}  // namespace

void MechAssaultNoteGameCamera() {
    last_game_camera.store(GetTickCount64(), std::memory_order_relaxed);
}

void MechAssaultOpenGraphics() {
    PostMessageW(static_cast<HWND>(app::Window()), kOpenGraphics, 0, 0);
}

void MechAssaultNotePauseMenu(bool native_graphics) {
    pause_has_native_graphics.store(native_graphics, std::memory_order_relaxed);
    last_pause_menu.store(GetTickCount64(), std::memory_order_relaxed);
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"MechAssault Rev 1",               // window and splash
    .short_title = L"MechAssault",               // message boxes
    .name = L"MechAssault",                      // window classes, crash dumps, handover event
    .log = "game",                        // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "MechAssault"}},  // each recompiled xbe and its exe, for title launches
    .options = {
        {"key_move_forward", nullptr, &move_forward},
        {"key_move_backward", nullptr, &move_backward},
        {"key_move_left", nullptr, &move_left},
        {"key_move_right", nullptr, &move_right},
        {"mouse_sensitivity", nullptr, &mouse_sensitivity},
        {"mouse_invert_y", &mouse_invert_y, nullptr},
        {"key_fire_weapon", nullptr, &fire_weapon},
        {"key_select_energy", nullptr, &select_energy},
        {"key_select_ballistic", nullptr, &select_ballistic},
        {"key_select_missile", nullptr, &select_missile},
        {"key_cycle_weapon", nullptr, &cycle_weapon},
        {"key_jump_jets", nullptr, &jump_jets},
        {"mouse_wheel_cycle", &mouse_wheel_cycle, nullptr},
    },
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = nullptr,                      // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = RegisterMouse,
    .message = HandleKeyboard,
    .ui = DrawSettings,
    .pad = ApplyKeyboard,
    .rumble = nullptr,                    // changes a rumble before it is set
};
