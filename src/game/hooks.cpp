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

#include <bit>
#include <cmath>

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

// Rev 1 camera builder, after perspective/orthographic construction and before SetTransform.
__declspec(dllexport) void MechAssaultNativeProjection(X86Context& ctx, uint8_t*) {
    cull = {};
    if (!diag::ultrawide_projection.load(std::memory_order_relaxed)) return;

    RECT client{};
    if (!GetClientRect(static_cast<HWND>(app::Window()), &client) || client.right <= 0 || client.bottom <= 0) return;
    const float aspect = float(client.right) / float(client.bottom);
    const uint32_t matrix = ctx.esp + 0x40;
    const float x = ReadFloat(matrix);
    if (!x || ReadFloat(matrix + 44) != 1.0f) return;
    const float camera_aspect = ReadFloat(matrix + 20) / x;
    if (std::abs(camera_aspect - kConsoleAspect) > 0.01f * kConsoleAspect) return;

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

bool IsMainMenuExitButton(uint32_t button) {
    return button && kernel::R32(button) == kMenuButton &&
           kernel::R32(button + 0x30) == kDashboardButtonName &&
           kernel::R32(button + 0x68) == kDashboardButtonId;
}

}  // namespace

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
        const uint32_t text = mem::AllocSystem(sizeof(label));
        if (!text) return;
        for (unsigned i = 0; i < sizeof(label) / sizeof(label[0]); ++i)
            kernel::W16(text + i * sizeof(label[0]), label[i]);
        X86Context call = ctx;
        call.ecx = node;
        kernel::CallGuest(call, 0x0008F520, {text});
        mem::FreeSystem(text);
        return;
    }
}

__declspec(dllexport) bool MechAssaultExitFromMainMenu(X86Context& ctx, uint8_t*) {
    if (!IsMainMenuExitButton(kernel::R32(ctx.esp + 4))) return false;
    PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
    ctx.eax = 0;
    return true;
}
