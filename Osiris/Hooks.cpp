#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx9.h"
#include "imgui/imgui_impl_win32.h"
#include "xorstr.hpp"

#include "Config.h"
#include "GUI.h"
#include "Hacks/Misc.h"
#include "Hooks.h"
#include "Interfaces.h"
#include "Memory.h"
#include "SDK/UserCmd.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT __stdcall hookedWndProc(HWND window, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (GetAsyncKeyState(VK_INSERT) & 1)
        gui.isOpen = !gui.isOpen;

    if (gui.isOpen && !ImGui_ImplWin32_WndProcHandler(window, msg, wParam, lParam))
        return true;

    return CallWindowProc(hooks.originalWndProc, window, msg, wParam, lParam);
}

static HRESULT __stdcall hookedPresent(IDirect3DDevice9* device, const RECT* src, const RECT* dest, HWND windowOverride, const RGNDATA* dirtyRegion)
{
    static bool isInitialised{ false };

    if (!isInitialised) {
        ImGui::CreateContext();
        ImGui_ImplWin32_Init(FindWindowA(xorstr_("Valve001"), NULL));
        ImGui_ImplDX9_Init(device);

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 0.0f;
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        hooks.originalWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtr(FindWindowA(xorstr_("Valve001"), NULL), GWLP_WNDPROC, LONG_PTR(hookedWndProc))
            );
        isInitialised = true;
    }
    else if (gui.isOpen) {
        DWORD d3rsColorWrite;
        device->GetRenderState(D3DRS_COLORWRITEENABLE, &d3rsColorWrite);
        IDirect3DVertexDeclaration9* vertexDeclaration;
        device->GetVertexDeclaration(&vertexDeclaration);
        IDirect3DVertexShader9* vertexShader;
        device->GetVertexShader(&vertexShader);
        device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xffffffff);
        device->SetRenderState(D3DRS_SRGBWRITEENABLE, false);
        device->SetSamplerState(NULL, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        device->SetSamplerState(NULL, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        device->SetSamplerState(NULL, D3DSAMP_ADDRESSW, D3DTADDRESS_WRAP);
        device->SetSamplerState(NULL, D3DSAMP_SRGBTEXTURE, NULL);

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        gui.render();

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

        device->SetRenderState(D3DRS_COLORWRITEENABLE, d3rsColorWrite);
        device->SetRenderState(D3DRS_SRGBWRITEENABLE, true);
        device->SetVertexDeclaration(vertexDeclaration);
        device->SetVertexShader(vertexShader);
    }
    return hooks.originalPresent(device, src, dest, windowOverride, dirtyRegion);
}

static HRESULT __stdcall hookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params)
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    auto result = hooks.originalReset(device, params);
    ImGui_ImplDX9_CreateDeviceObjects();
    return result;
}

static bool __fastcall hookedCreateMove(void* thisptr, void*, float inputSampleTime, UserCmd* cmd)
{
    if (interfaces.engineClient->IsConnected() && interfaces.engineClient->IsInGame())
        Misc::bunnyHop(cmd);
    return false;
}

static void __declspec(naked) __stdcall hookedCreateMoveProxy(int sequenceNumber, float inputSampleFrametime, bool active)
{
    __asm {
        push ebp
        mov  ebp, esp
        push ebx
        lea  ecx, [esp]
        push ecx
        push dword ptr[active]
        push dword ptr[inputSampleFrametime]
        push dword ptr[sequenceNumber]
        call hookedCreateMove
        pop  ebx
        pop  ebp
        retn 0Ch
    }
}

void __fastcall hookedLockCursor(Surface* thisptr, void* edx)
{
    if (gui.isOpen)
        interfaces.surface->UnlockCursor();
    else
        hooks.surface.getOriginal<void(__fastcall*)(Surface*, void*)>(67)(thisptr, edx);
}

Hooks::Hooks()
{
    originalPresent = **reinterpret_cast<decltype(&originalPresent)*>(memory.present);
    **reinterpret_cast<void***>(memory.present) = reinterpret_cast<void*>(&hookedPresent);
    originalReset = **reinterpret_cast<decltype(&originalReset)*>(memory.reset);
    **reinterpret_cast<void***>(memory.reset) = reinterpret_cast<void*>(&hookedReset);
    // originalCreateMove = reinterpret_cast<decltype(originalCreateMove)>(reinterpret_cast<int*>(memory.clientMode) + 24);
    // originalLockCursor = reinterpret_cast<decltype(originalLockCursor)>(reinterpret_cast<int*>(interfaces.surface) + 67);
    surface.setup(interfaces.surface);
    surface.hook_index(67, hookedLockCursor);
    client.setup(memory.clientMode);
    client.hook_index(24, hookedCreateMove);
}
