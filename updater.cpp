#define UNICODE
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <urlmon.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <wchar.h>
#include <initguid.h>
DEFINE_GUID(IID_IUnknown, 0x00000000,0x0000,0x0000,0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46);
DEFINE_GUID(IID_IBindStatusCallback, 0x79eac9c1,0xbaF9,0x11ce,0x8C,0x82,0x00,0xaa,0x00,0x4b,0xa9,0x0b);


#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "uxtheme.lib")

#define WM_START_UPDATE (WM_USER + 1)

HWND hwndProgress, hwndLabel;
HFONT hFont;

// 下载回调
class DownloadCallback : public IBindStatusCallback {
    LONG _ref;
public:
    DownloadCallback() : _ref(1) {}
    STDMETHODIMP OnStartBinding(DWORD, IBinding*) { return S_OK; }
    STDMETHODIMP GetPriority(LONG*) { return E_NOTIMPL; }
    STDMETHODIMP OnLowResource(DWORD) { return S_OK; }
    STDMETHODIMP OnProgress(ULONG ulProgress, ULONG ulMax, ULONG, LPCWSTR) {
        if (ulMax > 0) {
            int percent = (int)(ulProgress * 100 / ulMax);
            SendMessage(hwndProgress, PBM_SETPOS, percent, 0);
        }
        return S_OK;
    }
    STDMETHODIMP OnStopBinding(HRESULT, LPCWSTR) { return S_OK; }
    STDMETHODIMP GetBindInfo(DWORD* grfBINDF, BINDINFO* pbindinfo) {
        *grfBINDF = BINDF_GETNEWESTVERSION | BINDF_ASYNCHRONOUS;
        return S_OK;
    }
    STDMETHODIMP OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) { return S_OK; }
    STDMETHODIMP OnObjectAvailable(REFIID, IUnknown*) { return S_OK; }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_IBindStatusCallback) {
            *ppv = static_cast<IBindStatusCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&_ref); }
    STDMETHODIMP_(ULONG) Release() {
        ULONG ref = InterlockedDecrement(&_ref);
        if (ref == 0) delete this;
        return ref;
    }
};

DWORD WINAPI UpdateThread(LPVOID lpParam);
void CenterWindow(HWND hwnd);

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_START_UPDATE:
            CreateThread(NULL, 0, UpdateThread, hwnd, 0, NULL);
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            if (hFont) DeleteObject(hFont);
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

DWORD WINAPI UpdateThread(LPVOID lpParam) {
    HWND hwnd = (HWND)lpParam;

    SetWindowText(hwndLabel, L"正在下载更新包...");
    SendMessage(hwndProgress, PBM_SETPOS, 0, 0);

    WCHAR exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    PathRemoveFileSpecW(exeDir);

    WCHAR zipPath[MAX_PATH];
    wcscpy(zipPath, exeDir);
    wcscat(zipPath, L"\\update_temp.zip");

    WCHAR extractDir[MAX_PATH];
    wcscpy(extractDir, exeDir);

    WCHAR mainExePath[MAX_PATH];
    wcscpy(mainExePath, exeDir);
    wcscat(mainExePath, L"\\main.exe");

    // 下载文件
    DownloadCallback* cb = new DownloadCallback();
    HRESULT hr = URLDownloadToFileW(NULL,
        L"https://github.com/MOMIEDEJIYI/novel-spider-release/releases/download/v1.0.1/new_version.zip",
        zipPath, 0, cb);
    cb->Release();

    if (FAILED(hr)) {
        SetWindowText(hwndLabel, L"下载失败！");
        MessageBox(hwnd, L"下载失败，请检查网络或链接。", L"错误", MB_OK | MB_ICONERROR);
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return 1;
    }

    SendMessage(hwndProgress, PBM_SETPOS, 50, 0);
    SetWindowText(hwndLabel, L"解压中...");

    // 解压 ZIP 文件
    WCHAR args[1024];
    swprintf(args, 1024,
        L"powershell.exe -Command \"Expand-Archive -Force -LiteralPath '%s' -DestinationPath '%s'\"",
        zipPath, extractDir);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessW(NULL, args, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        SetWindowText(hwndLabel, L"解压失败！");
        MessageBox(hwnd, L"解压失败，可能不支持 Expand-Archive。", L"错误", MB_OK | MB_ICONERROR);
        PostMessage(hwnd, WM_CLOSE, 0, 0);
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    DeleteFileW(zipPath);
    SendMessage(hwndProgress, PBM_SETPOS, 90, 0);
    SetWindowText(hwndLabel, L"启动主程序...");

    ShellExecuteW(NULL, L"open", mainExePath, NULL, NULL, SW_SHOWNORMAL);
    SendMessage(hwndProgress, PBM_SETPOS, 100, 0);
    PostMessage(hwnd, WM_CLOSE, 0, 0);
    return 0;
}

void CenterWindow(HWND hwnd) {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int win_w = rc.right - rc.left;
    int win_h = rc.bottom - rc.top;

    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);

    SetWindowPos(hwnd, NULL,
        (screen_w - win_w) / 2,
        (screen_h - win_h) / 2,
        0, 0,
        SWP_NOZORDER | SWP_NOSIZE);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"UpdaterWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_INFORMATION);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, L"UpdaterWindow", L"程序更新器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 150,
        NULL, NULL, hInstance, NULL
    );

    hFont = CreateFont(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    hwndLabel = CreateWindow(
        L"STATIC", L"准备开始更新...",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, 20, 360, 25,
        hwnd, NULL, hInstance, NULL
    );
    SendMessage(hwndLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

    hwndProgress = CreateWindowEx(
        0, PROGRESS_CLASS, NULL,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        20, 60, 360, 25,
        hwnd, NULL, hInstance, NULL
    );
    SendMessage(hwndProgress, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessage(hwndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(hwndProgress, PBM_SETPOS, 0, 0);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    CenterWindow(hwnd);

    SetWindowTheme(hwndProgress, L"Explorer", NULL);
    PostMessage(hwnd, WM_START_UPDATE, 0, 0);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
