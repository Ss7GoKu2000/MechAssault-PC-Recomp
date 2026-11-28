// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>

#include "app/app.h"
#include "core/diag.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include "kernel/kernel.h"

namespace {
constexpr float kConsoleAspect = 4.0f / 3.0f;
constexpr float kHalfDegree = 0.00872664626f;
constexpr uint32_t kHorizontalFov = 0x88;

struct CameraCull {
    uint32_t camera = 0;
    float width_scale = 1;
    uint32_t original_fov = 0;
    bool adjusted = false;
};
thread_local CameraCull cull;

float ReadFloat(uint32_t address) {
    return std::bit_cast<float>(kernel::R32(address));
}

void WriteFloat(uint32_t address, float value) {
    kernel::W32(address, std::bit_cast<uint32_t>(value));
}
}  // namespace

void MechAssaultNoteGameCamera();
void MechAssaultOpenGraphics();
void MechAssaultNotePauseMenu(bool native_graphics);

// Rev 1 camera builder, after perspective/orthographic construction and before SetTransform.
__declspec(dllexport) void MechAssaultNativeProjection(X86Context& ctx, uint8_t*) {
    cull = {};
    const uint32_t matrix = ctx.esp + 0x40;
    const float x = ReadFloat(matrix);
    if (!x || ReadFloat(matrix + 44) != 1.0f) return;
    const float camera_aspect = ReadFloat(matrix + 20) / x;
    if (std::abs(camera_aspect - kConsoleAspect) > 0.01f * kConsoleAspect) return;
    MechAssaultNoteGameCamera();
    if (!diag::ultrawide_projection.load(std::memory_order_relaxed)) return;

    RECT client{};
    if (!GetClientRect(static_cast<HWND>(app::Window()), &client) || client.right <= 0 || client.bottom <= 0) return;
    const float aspect = float(client.right) / float(client.bottom);
    const float scale = aspect / kConsoleAspect;
    for (uint32_t row = 0; row < 4; ++row) {
        const uint32_t column_x = matrix + row * 16;
        WriteFloat(column_x, ReadFloat(column_x) / scale);
    }
    cull = {ctx.ebx, scale, 0, false};
    LOG_FIRST(4, "mechassault: camera aspect %.4f, frustum width scale %.4f", aspect, scale);
}

// The game's culler builds horizontal and vertical planes from the same stored FOV.
__declspec(dllexport) void MechAssaultNativeCullSides(X86Context& ctx, uint8_t*) {
    if (!cull.camera || ctx.ebx != cull.camera || cull.adjusted) return;
    const uint32_t address = ctx.ebx + kHorizontalFov;
    const float fov = ReadFloat(address);
    if (!(fov > 0.0f && fov < 180.0f)) return;
    cull.original_fov = kernel::R32(address);
    cull.adjusted = true;
    WriteFloat(address, std::atan(std::tan(fov * kHalfDegree) * cull.width_scale) / kHalfDegree);
}

__declspec(dllexport) void MechAssaultNativeCullVertical(X86Context& ctx, uint8_t*) {
    if (cull.adjusted && ctx.ebx == cull.camera) {
        kernel::W32(ctx.ebx + kHorizontalFov, cull.original_fov);
        cull.adjusted = false;
    }
}

__declspec(dllexport) void MechAssaultNativeCullDone(X86Context& ctx, uint8_t* base) {
    MechAssaultNativeCullVertical(ctx, base);
    cull = {};
}
#include "kernel/memory.h"

namespace {

constexpr uint32_t kMenuButton = 0x00313288;
constexpr uint32_t kDashboardButtonName = 0x4A000245;
constexpr uint32_t kDashboardButtonId = 5;
constexpr uint32_t kOptionsMenu = 0x00315AB0;
constexpr uint32_t kPauseMenu = 0x003153F0;
constexpr uint32_t kDemosButtonId = 3;
constexpr uint32_t kButtonVisible = 0x02;
constexpr uint32_t kButtonFocusable = 0x20;
constexpr uint32_t kGraphicsButtonId = 0x4D410001;
constexpr uint32_t kMenuButtonClassId = 0x00371C2C;
constexpr uint32_t kMenuWidgetType = 0x00392138;
constexpr uint32_t kMenuSprite = 0x00317F88;
constexpr uint32_t kMenuSpriteClassId = 0x00374568;

bool IsMainMenuExitButton(uint32_t button) {
    return button && kernel::R32(button) == kMenuButton &&
           kernel::R32(button + 0x30) == kDashboardButtonName &&
           kernel::R32(button + 0x68) == kDashboardButtonId;
}

bool IsOptionsDemosButton(uint32_t button) {
    return button && kernel::R32(button) == kMenuButton && kernel::R32(button + 0x68) == kDemosButtonId;
}

bool IsPauseGraphicsButton(uint32_t button) {
    return button && kernel::R32(button) == kMenuButton && kernel::R32(button + 0x68) == kGraphicsButtonId;
}

uint32_t FindPauseGraphics(uint32_t menu) {
    const uint32_t root = kernel::R32(menu + 0x50);
    if (!root) return 0;
    for (uint32_t node = kernel::R32(root + 0x30); node; node = kernel::R32(node + 0x40))
        if (IsPauseGraphicsButton(node)) return node;
    return 0;
}

struct MenuRect {
    int x, y, width, height;
};

MenuRect WidgetRect(uint32_t node) {
    return {int32_t(kernel::R32(node + 0x58)), int32_t(kernel::R32(node + 0x5C)),
            int32_t(kernel::R32(node + 0x60)), int32_t(kernel::R32(node + 0x64))};
}

bool IsMenuWidget(X86Context& ctx, uint32_t node) {
    const uint32_t type = kernel::R32(kMenuWidgetType);
    if (!type) return false;
    X86Context call = ctx;
    call.ecx = node;
    return (kernel::CallGuest(call, 0x0001DFD0, {type}) & 0xFF) != 0;
}

void AttachPauseFocusSprite(X86Context& ctx, uint32_t root, uint32_t donor, uint32_t button) {
    uint32_t source = 0;
    for (uint32_t child = kernel::R32(donor + 0x1C); child; child = kernel::R32(child + 0x18)) {
        if (kernel::R32(child) != kMenuSprite) continue;
        const uint32_t show_state = kernel::R32(child + 0xBC);
        if (show_state == 3 || show_state == 4) {
            source = child;
            break;
        }
    }
    if (!source) return;
    const uint32_t begin = kernel::R32(source + 0x98), end = kernel::R32(source + 0x9C);
    const uint32_t class_id = kernel::R32(kMenuSpriteClassId);
    if (!class_id || !begin || end <= begin || end - begin > 256) return;

    X86Context call = ctx;
    call.ecx = root;
    const uint32_t sprite = kernel::CallGuest(call, 0x00023BC0, {class_id});
    if (!sprite || kernel::R32(sprite) != kMenuSprite) return;

    // Copy the stock sprite's authored properties. Its constructor and the scene
    // initialiser own the decoder and frame timing for this new instance.
    call = ctx;
    call.ecx = sprite + 0x98;
    kernel::CallGuest(call, 0x00014150, {begin, end});
    for (const uint32_t offset : {0x60u, 0x64u, 0x70u, 0xA4u, 0xA8u, 0xB0u, 0xB4u, 0xBCu, 0xC4u})
        kernel::W32(sprite + offset, kernel::R32(source + offset));

    const auto original = WidgetRect(source), from = WidgetRect(donor), to = WidgetRect(button);
    kernel::W32(sprite + 0x58, uint32_t(original.x + to.x - from.x));
    kernel::W32(sprite + 0x5C, uint32_t(original.y + to.y - from.y));
    call = ctx;
    call.ecx = sprite + 0x10;
    kernel::CallGuest(call, 0x00023CF0, {button, 1});
}

int ExpandPauseLayout(X86Context& ctx, uint32_t root, const MenuRect& first, const MenuRect& last, int step) {
    uint32_t panel = 0;
    MenuRect bounds{};
    for (uint32_t node = kernel::R32(root + 0x30); node; node = kernel::R32(node + 0x40)) {
        if (kernel::R32(node) == kMenuButton || !IsMenuWidget(ctx, node)) continue;
        const auto rect = WidgetRect(node);
        // Locate the pause frame by its existing bounds around the centered stock rows.
        if (rect.width < last.width || rect.width > 480 || rect.height <= 0 || rect.height > 340 ||
            rect.x > last.x || rect.x + rect.width < last.x + last.width ||
            rect.y > first.y || rect.y < first.y - 2 * step ||
            rect.y + rect.height < last.y + step ||
            rect.y + rect.height + step > 468) continue;
        if (!panel || rect.width * rect.height < bounds.width * bounds.height) {
            panel = node;
            bounds = rect;
        }
    }
    // Unknown frames keep their original bounds and prompts. Place the row below them.
    if (!panel) return last.y + 2 * step;

    std::array<uint32_t, 12> footer{};
    size_t footer_count = 0;
    for (uint32_t node = kernel::R32(root + 0x30); node; node = kernel::R32(node + 0x40)) {
        if (node == panel || !IsMenuWidget(ctx, node)) continue;
        if ((kernel::R32(node + 0x70) & kButtonFocusable) != 0) continue;
        const auto rect = WidgetRect(node);
        if (rect.y >= last.y + 3 * step / 4 && rect.y + rect.height <= bounds.y + bounds.height &&
            rect.x >= bounds.x && rect.x + rect.width <= bounds.x + bounds.width &&
            rect.width > 0 && rect.height > 0 && rect.height <= 2 * step) {
            if (footer_count == footer.size()) return last.y + 2 * step;
            footer[footer_count++] = node;
        }
    }
    if (!footer_count) return last.y + 2 * step;
    for (size_t i = 0; i < footer_count; ++i)
        kernel::W32(footer[i] + 0x5C, uint32_t(WidgetRect(footer[i]).y + step));
    kernel::W32(panel + 0x64, uint32_t(bounds.height + step));
    return last.y + step;
}

template <size_t N>
bool SetMenuButtonLabel(X86Context& ctx, uint32_t button, const uint16_t (&label)[N]) {
    const uint32_t text = mem::AllocSystem(sizeof(label));
    if (!text) return false;
    for (unsigned i = 0; i < N; ++i) kernel::W16(text + i * sizeof(label[0]), label[i]);
    X86Context call = ctx;
    call.ecx = button;
    kernel::CallGuest(call, 0x0008F520, {text});
    mem::FreeSystem(text);
    return true;
}

void TraceMenuRows(uint32_t menu, const char* name) {
    static const bool trace = std::getenv("MECHASSAULT_TRACE_MENU_ROWS") != nullptr;
    if (!trace || !menu) return;
    const uint32_t root = kernel::R32(menu + 0x50);
    if (!root) return;
    LOGR("Menu rows: %s", name);
    unsigned count = 0;
    for (uint32_t node = kernel::R32(root + 0x30); node && count < 64;
         node = kernel::R32(node + 0x40), ++count) {
        const auto type = kernel::R32(node);
        if (type != kMenuButton) continue;
        LOGR("  button id=%u label=%08X flags=%08X", kernel::R32(node + 0x68),
             kernel::R32(node + 0x30), kernel::R32(node + 0x70));
    }
}

}  // namespace

// The Xbox Demos disc is absent in the PC build. Its Options row uses the native button layout.
__declspec(dllexport) void MechAssaultSetOptionsGraphics(X86Context& ctx, uint8_t*) {
    if (kernel::R32(ctx.edi) != kOptionsMenu) return;
    const uint32_t root = kernel::R32(ctx.edi + 0x50);
    if (!root) return;
    for (uint32_t node = kernel::R32(root + 0x30); node; node = kernel::R32(node + 0x40)) {
        if (!IsOptionsDemosButton(node)) continue;
        constexpr uint16_t label[] = {'G', 'R', 'A', 'P', 'H', 'I', 'C', 'S', 0};
        if (SetMenuButtonLabel(ctx, node, label))
            kernel::W32(node + 0x70, kernel::R32(node + 0x70) | kButtonVisible);
        return;
    }
}

__declspec(dllexport) bool MechAssaultOptionsGraphicsSelected(X86Context& ctx, uint8_t*) {
    if (!IsOptionsDemosButton(kernel::R32(ctx.esp + 4))) return false;
    MechAssaultOpenGraphics();
    ctx.eax = 0;
    return true;
}

__declspec(dllexport) void MechAssaultTracePauseRows(X86Context& ctx, uint8_t*) {
    TraceMenuRows(ctx.esi, "Pause");
}

// Register with the loaded pause scene before the game's normal scene initialisation.
__declspec(dllexport) void MechAssaultAddPauseGraphics(X86Context& ctx, uint8_t*) {
    const uint32_t menu = ctx.esi;
    if (!menu || kernel::R32(menu) != kPauseMenu || FindPauseGraphics(menu)) return;
    const uint32_t root = kernel::R32(menu + 0x50);
    if (!root) return;
    std::array<uint32_t, 8> rows{};
    size_t count = 0;
    constexpr uint32_t selectable = kButtonVisible | kButtonFocusable;
    for (uint32_t node = kernel::R32(root + 0x30); node; node = kernel::R32(node + 0x40)) {
        if (kernel::R32(node) != kMenuButton || (kernel::R32(node + 0x70) & selectable) != selectable)
            continue;
        if (count == rows.size()) return;
        rows[count++] = node;
    }
    if (count != 5) return;
    const auto y = [](uint32_t node) { return int32_t(kernel::R32(node + 0x5C)); };
    std::sort(rows.begin(), rows.begin() + count, [&](uint32_t a, uint32_t b) { return y(a) < y(b); });
    const uint32_t donor = rows[count - 1];
    const auto first = WidgetRect(rows[0]), last = WidgetRect(donor);
    const int step = y(donor) - y(rows[count - 2]);
    if (step < 18 || step > 48 || first.y < 0 || last.width <= 0 || last.height <= 0 ||
        last.y + 2 * step + last.height > 468) return;
    for (size_t i = 1; i < count; ++i)
        if (std::abs(y(rows[i]) - y(rows[i - 1]) - step) > 3) return;
    const uint32_t scene_parent = kernel::R32(donor + 0x14);
    const uint32_t font_start = kernel::R32(donor + 0x94), font_end = kernel::R32(donor + 0x98);
    const uint32_t class_id = kernel::R32(kMenuButtonClassId);
    if (!scene_parent || !class_id || !font_start || font_end <= font_start || font_end - font_start > 256) return;

    // The game allocates the button, gives it a parent and registers it with the scene.
    X86Context call = ctx;
    call.ecx = root;
    const uint32_t button = kernel::CallGuest(call, 0x00023BC0, {class_id});
    if (!button || kernel::R32(button) != kMenuButton) return;
    for (const uint32_t offset : {0x58u, 0x60u, 0x64u, 0x6Cu, 0x74u, 0xD4u, 0xD8u})
        kernel::W32(button + offset, kernel::R32(donor + offset));
    for (unsigned offset = 0xDE; offset < 0xF6; offset += 4)
        kernel::W32(button + offset, kernel::R32(donor + offset));
    for (unsigned offset = 0xF8; offset <= 0x10C; offset += 4)
        kernel::W32(button + offset, kernel::R32(donor + offset));
    kernel::W16(button + 0x110, kernel::R16(donor + 0x110));
    call = ctx;
    call.ecx = button + 0x94;
    kernel::CallGuest(call, 0x00014150, {font_start, font_end});
    constexpr uint16_t label[] = {'G', 'R', 'A', 'P', 'H', 'I', 'C', 'S', 0};
    if (!SetMenuButtonLabel(ctx, button, label)) {
        kernel::W32(button + 0x70, 0);
        return;
    }
    // The menu list handles focus. The scene tree handles drawing and widget initialisation.
    call = ctx;
    call.ecx = button + 0x10;
    kernel::CallGuest(call, 0x00023CF0, {scene_parent, 1});
    const int new_y = ExpandPauseLayout(ctx, root, first, last, step);
    kernel::W32(button + 0x68, kGraphicsButtonId);
    kernel::W32(button + 0x5C, uint32_t(new_y));
    kernel::W32(button + 0x70, kernel::R32(donor + 0x70));
    AttachPauseFocusSprite(ctx, root, donor, button);
}

__declspec(dllexport) bool MechAssaultPauseGraphicsSelected(X86Context& ctx, uint8_t*) {
    if (!ctx.esi || kernel::R32(ctx.esi) != kPauseMenu || !ctx.edi) return false;
    if (ctx.edi != FindPauseGraphics(ctx.esi)) return false;
    MechAssaultOpenGraphics();
    ctx.ecx = ctx.esi;
    return true;
}

__declspec(dllexport) void MechAssaultPauseMenuFrame(X86Context& ctx, uint8_t*) {
    if (!ctx.ecx || kernel::R32(ctx.ecx) != kPauseMenu || (kernel::R32(ctx.ecx + 0x20) & 0xFF)) return;
    MechAssaultNotePauseMenu(FindPauseGraphics(ctx.ecx) != 0);
}

// The Xbox hides its dashboard row on this path. The PC uses that row for Exit.
__declspec(dllexport) bool MechAssaultKeepExitVisible(X86Context& ctx, uint8_t*) {
    return IsMainMenuExitButton(ctx.esi);
}

// Use the button's native wide-text setter, which copies the label and lays it out.
__declspec(dllexport) void MechAssaultSetExitLabel(X86Context& ctx, uint8_t*) {
    const uint32_t menu = ctx.edi;
    const uint32_t root = kernel::R32(menu + 0x50);
    if (!root) return;
    uint32_t node = kernel::R32(root + 0x30);
    for (; node; node = kernel::R32(node + 0x40)) {
        if (!IsMainMenuExitButton(node)) continue;
        constexpr uint16_t label[] = {'E', 'X', 'I', 'T', 0};
        SetMenuButtonLabel(ctx, node, label);
        return;
    }
}

__declspec(dllexport) bool MechAssaultExitFromMainMenu(X86Context& ctx, uint8_t*) {
    if (!IsMainMenuExitButton(kernel::R32(ctx.esp + 4))) return false;
    PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
    ctx.eax = 0;
    return true;
}
