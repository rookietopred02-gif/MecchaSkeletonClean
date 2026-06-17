#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <tchar.h>
#include <TlHelp32.h>
#include <cstring>
#include <fstream>
#include <string>

#include "MecchaReader.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

void LogInfo(const std::string& msg) {
    std::ofstream out("overlay_log.txt", std::ios::app);
    if (out.is_open()) {
        out << msg << "\n";
    }
}

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_deviceContext = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

void CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (backBuffer) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
        backBuffer->Release();
    }
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

bool CreateDeviceD3D(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel{};
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
        &g_swapChain, &g_device, &featureLevel, &g_deviceContext);

    if (result == DXGI_ERROR_UNSUPPORTED) {
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_swapChain, &g_device, &featureLevel, &g_deviceContext);
    }

    if (FAILED(result)) {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swapChain) {
        g_swapChain->Release();
        g_swapChain = nullptr;
    }
    if (g_deviceContext) {
        g_deviceContext->Release();
        g_deviceContext = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
}

DWORD FindProcessId(const wchar_t* processName)
{
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

struct WindowSearch {
    DWORD Pid = 0;
    HWND Hwnd = nullptr;
};

BOOL CALLBACK EnumWindowsByPid(HWND hwnd, LPARAM lparam)
{
    auto* search = reinterpret_cast<WindowSearch*>(lparam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->Pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) {
        return TRUE;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) {
        return TRUE;
    }

    search->Hwnd = hwnd;
    return FALSE;
}

HWND FindMainWindow(DWORD pid)
{
    if (!pid) {
        return nullptr;
    }

    WindowSearch search{};
    search.Pid = pid;
    EnumWindows(EnumWindowsByPid, reinterpret_cast<LPARAM>(&search));
    return search.Hwnd;
}

bool GetClientScreenRect(HWND hwnd, RECT& out)
{
    if (!hwnd || !IsWindow(hwnd) || IsIconic(hwnd)) {
        return false;
    }

    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
        return false;
    }

    POINT topLeft{ client.left, client.top };
    POINT bottomRight{ client.right, client.bottom };
    ClientToScreen(hwnd, &topLeft);
    ClientToScreen(hwnd, &bottomRight);

    out.left = topLeft.x;
    out.top = topLeft.y;
    out.right = bottomRight.x;
    out.bottom = bottomRight.y;
    return out.right > out.left && out.bottom > out.top;
}

void SetOverlayClickThrough(HWND hwnd, bool clickThrough)
{
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (clickThrough) {
        exStyle |= WS_EX_TRANSPARENT;
    }
    else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && g_device != nullptr) {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    LogInfo("Starting MecchaBoxEsp (Release)...");
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"MecchaBoxEspOverlay";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"MECCHA Box ESP",
        WS_POPUP,
        100, 100, 1280, 720,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!hwnd) {
        LogInfo("CreateWindowExW failed.");
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    MARGINS margins{ -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    if (!CreateDeviceD3D(hwnd)) {
        LogInfo("CreateDeviceD3D failed.");
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    LogInfo("D3D11 device created successfully.");

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_deviceContext);

    MecchaReader reader;
    bool showMenu = true;
    bool done = false;
    RECT lastOverlayRect{ 100, 100, 1380, 820 };
    SetOverlayClickThrough(hwnd, false);

    while (!done) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        if ((GetAsyncKeyState(VK_INSERT) & 1) != 0) {
            showMenu = !showMenu;
            SetOverlayClickThrough(hwnd, !showMenu);
        }

        if ((GetAsyncKeyState(VK_F5) & 0x8000) != 0) {
            done = true;
        }

        DWORD targetPid = reader.TargetPid();
        if (!targetPid) {
            targetPid = FindProcessId(L"PenguinHotel-Win64-Shipping.exe");
        }

        RECT targetRect{};
        HWND targetWindow = FindMainWindow(targetPid);
        if (GetClientScreenRect(targetWindow, targetRect)) {
            if (memcmp(&targetRect, &lastOverlayRect, sizeof(RECT)) != 0) {
                lastOverlayRect = targetRect;
                LogInfo("Overlay rect updated: " + std::to_string(targetRect.left) + ", " + std::to_string(targetRect.top));
                SetWindowPos(hwnd, HWND_TOPMOST,
                    targetRect.left, targetRect.top,
                    targetRect.right - targetRect.left,
                    targetRect.bottom - targetRect.top,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        reader.DrawEsp(ImGui::GetBackgroundDrawList(), io.DisplaySize);

        if (showMenu) {
            ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(430.0f, 0.0f), ImGuiCond_FirstUseEver);
            ImGui::Begin("MECCHA Box ESP", &showMenu, ImGuiWindowFlags_NoCollapse);
            ImGui::TextUnformatted("Insert toggles this panel");
            reader.DrawControls();
            ImGui::End();
        }

        static int frameCount = 0;
        if (frameCount++ % 600 == 0) {
            LogInfo("Frame: " + std::to_string(frameCount) + " Status: " + std::string(reader.Status()) + " Attached: " + std::to_string(reader.IsAttached()));
        }

        ImGui::Render();
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_deviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_deviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
