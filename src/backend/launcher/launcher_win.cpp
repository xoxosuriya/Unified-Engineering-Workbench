#include <windows.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <memory>
#include <sstream>
#include "http_server.hpp"

using namespace workbench;

static std::unique_ptr<HttpServer> g_server;
static HWND g_hwnd = NULL;
static NOTIFYICONDATAW g_nid = {0};
static HANDLE g_hMutex = NULL;

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_OPEN 1001
#define ID_TRAY_EXIT 1002
#define ID_BTN_OPEN  2001
#define ID_BTN_EXIT  2002

std::wstring get_module_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring str(path);
    size_t pos = str.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? str.substr(0, pos) : L".";
}

std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Create controls in the status dialog
            CreateWindowW(L"STATIC", L"Unified Engineering Workbench is active.", 
                          WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 20, 360, 25, hwnd, NULL, NULL, NULL);

            std::wstring port_msg = L"Local Server: " + std::wstring(g_server ? get_module_dir() : L"");
            if (g_server) {
                std::string url = g_server->get_base_url();
                std::wstring wurl(url.begin(), url.end());
                CreateWindowW(L"STATIC", (L"URL: " + wurl).c_str(), 
                              WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 50, 360, 25, hwnd, NULL, NULL, NULL);
            }

            CreateWindowW(L"BUTTON", L"Open in Browser", 
                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 50, 95, 140, 35, hwnd, (HMENU)ID_BTN_OPEN, NULL, NULL);
            CreateWindowW(L"BUTTON", L"Exit Workbench", 
                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 210, 95, 140, 35, hwnd, (HMENU)ID_BTN_EXIT, NULL, NULL);
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == ID_BTN_OPEN || wmId == ID_TRAY_OPEN) {
                if (g_server) {
                    std::string url = g_server->get_base_url();
                    std::wstring wurl(url.begin(), url.end());
                    ShellExecuteW(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }
            } else if (wmId == ID_BTN_EXIT || wmId == ID_TRAY_EXIT) {
                DestroyWindow(hwnd);
            }
            break;
        }

        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
                if (lParam == WM_LBUTTONDBLCLK) {
                    if (g_server) {
                        std::string url = g_server->get_base_url();
                        std::wstring wurl(url.begin(), url.end());
                        ShellExecuteW(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);
                    }
                } else {
                    POINT pt;
                    GetCursorPos(&pt);
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN, L"Open Workbench in Browser");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit Workbench");
                    SetForegroundWindow(hwnd);
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                    DestroyMenu(hMenu);
                }
            }
            break;
        }

        case WM_CLOSE: {
            DestroyWindow(hwnd);
            break;
        }

        case WM_DESTROY: {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            if (g_server) {
                g_server->stop();
            }
            if (g_hMutex) {
                ReleaseMutex(g_hMutex);
                CloseHandle(g_hMutex);
                g_hMutex = NULL;
            }
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    HttpServer::set_process_start_time(std::chrono::steady_clock::now());

    // Determine path to frontend web assets
    std::wstring root_dir = get_module_dir();

    // Single-instance guard (named mutex)
    g_hMutex = CreateMutexW(NULL, TRUE, L"Local\\PassScienceWorks_Workbench_Launch_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        int existing_port = 8080;
        std::wstring port_file = root_dir + L"\\logs\\port.txt";
        FILE* fp = _wfopen(port_file.c_str(), L"r");
        if (fp) {
            if (fscanf(fp, "%d", &existing_port) != 1 || existing_port <= 0) {
                existing_port = 8080;
            }
            fclose(fp);
        }
        std::wstring open_url = L"http://127.0.0.1:" + std::to_wstring(existing_port);
        ShellExecuteW(NULL, L"open", open_url.c_str(), NULL, NULL, SW_SHOWNORMAL);
        if (g_hMutex) {
            CloseHandle(g_hMutex);
            g_hMutex = NULL;
        }
        return 0;
    }

    std::string web_dir = wstring_to_utf8(root_dir) + "/src/frontend";

    // Start background C++ HTTP server
    g_server = std::make_unique<HttpServer>("127.0.0.1", 8080, web_dir);
    if (!g_server->start()) {
        MessageBoxW(NULL, L"Failed to start local C++ server on port 8080-8100.", 
                    L"Workbench Launch Error", MB_ICONERROR | MB_OK);
        if (g_hMutex) {
            ReleaseMutex(g_hMutex);
            CloseHandle(g_hMutex);
            g_hMutex = NULL;
        }
        return 1;
    }

    // Automatically open user's default browser immediately
    std::string base_url = g_server->get_base_url();
    std::wstring wurl(base_url.begin(), base_url.end());
    ShellExecuteW(NULL, L"open", wurl.c_str(), NULL, NULL, SW_SHOWNORMAL);

    // Register simple status window class
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"PASS_Engineering_Workbench_Class";

    RegisterClassExW(&wc);

    // Position window centered
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int win_w = 420;
    int win_h = 190;
    int pos_x = (screen_w - win_w) / 2;
    int pos_y = (screen_h - win_h) / 2;

    g_hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        wc.lpszClassName,
        L"PASS Science Works - Unified Workbench Status",
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        pos_x, pos_y, win_w, win_h,
        NULL, NULL, hInstance, NULL
    );

    // Setup system tray notification icon
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"PASS Engineering Workbench (Active)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    // Display status window
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // Standard Win32 message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
