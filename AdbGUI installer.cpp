#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <thread>
#include <chrono>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#define ID_BTN_CLEAR 1001
#define ID_BTN_INSTALL 1002
#define ID_LISTBOX_APK 1003
#define ID_BTN_ADB_PATH 1004
#define ID_EDIT_ADB_PATH 1005
#define ID_LISTBOX_LOG 1006
#define ID_PROGRESS 1007

HWND hListBoxApk, hListBoxLog, hBtnClear, hBtnInstall, hBtnAdbPath, hEditAdbPath, hProgress;
std::vector<std::wstring> apkFiles;
std::wstring adbPath;
wchar_t currentPath[MAX_PATH] = { 0 };

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributes(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

void FindADB() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t pos = exeDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        exeDir = exeDir.substr(0, pos + 1);
        std::wstring possiblePath = exeDir + L"adb.exe";
        if (FileExists(possiblePath)) {
            adbPath = possiblePath;
            return;
        }
    }

    if (FileExists(L"adb.exe")) {
        adbPath = L"adb.exe";
        return;
    }

    wchar_t pathEnv[32767];
    GetEnvironmentVariable(L"PATH", pathEnv, 32767);
    std::wstring pathStr = pathEnv;
    size_t start = 0, end = 0;

    while ((end = pathStr.find(L';', start)) != std::wstring::npos) {
        std::wstring dir = pathStr.substr(start, end - start);
        std::wstring possiblePath = dir + L"\\adb.exe";
        if (FileExists(possiblePath)) {
            adbPath = possiblePath;
            return;
        }
        start = end + 1;
    }

    adbPath = L"adb.exe";
}

void RefreshApkList() {
    SendMessage(hListBoxApk, LB_RESETCONTENT, 0, 0);
    if (apkFiles.empty()) {
        SendMessage(hListBoxApk, LB_ADDSTRING, 0, (LPARAM)L"Drag and drop APK files here or add them manually");
    }
    else {
        for (const auto& file : apkFiles) {
            SendMessage(hListBoxApk, LB_ADDSTRING, 0, (LPARAM)file.c_str());
        }
    }
}

void AddToLog(const std::wstring& message) {
    if (SendMessage(hListBoxLog, LB_GETCOUNT, 0, 0) == 1) {
        wchar_t buffer[256];
        SendMessage(hListBoxLog, LB_GETTEXT, 0, (LPARAM)buffer);
        if (wcscmp(buffer, L"Installation logs will appear here") == 0) {
            SendMessage(hListBoxLog, LB_RESETCONTENT, 0, 0);
        }
    }

    SendMessage(hListBoxLog, LB_ADDSTRING, 0, (LPARAM)message.c_str());
    int count = (int)SendMessage(hListBoxLog, LB_GETCOUNT, 0, 0);
    SendMessage(hListBoxLog, LB_SETTOPINDEX, count - 1, 0);
}

DWORD WINAPI InstallThread(LPVOID lpParam) {
    int apkCount = (int)apkFiles.size();
    if (apkCount == 0) {
        PostMessage((HWND)lpParam, WM_USER + 1, 0, 0);
        return 0;
    }

    wchar_t buffer[1024];
    GetWindowText(hEditAdbPath, buffer, 1024);
    std::wstring currentAdbPath = buffer;

    if (currentAdbPath.empty()) {
        PostMessage((HWND)lpParam, WM_USER + 2, 0, (LPARAM)L"Error: ADB path is empty");
        return 0;
    }

    for (int i = 0; i < apkCount; i++) {
        std::wstring logMsg = L"Installing: " + apkFiles[i];
        PostMessage((HWND)lpParam, WM_USER + 4, 0, (LPARAM)logMsg.c_str());

        std::wstring cmd = L"\"" + currentAdbPath + L"\" install \"" + apkFiles[i] + L"\"";

        STARTUPINFO si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        if (CreateProcess(NULL, (LPWSTR)cmd.c_str(), NULL, NULL, FALSE,
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD exitCode;
            GetExitCodeProcess(pi.hProcess, &exitCode);

            if (exitCode == 0) {
                logMsg = L"Success: " + apkFiles[i];
            }
            else {
                logMsg = L"Failed: " + apkFiles[i] + L" (Exit code: " + std::to_wstring(exitCode) + L")";
            }

            PostMessage((HWND)lpParam, WM_USER + 4, 0, (LPARAM)logMsg.c_str());
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        else {
            logMsg = L"Failed to start process for: " + apkFiles[i];
            PostMessage((HWND)lpParam, WM_USER + 4, 0, (LPARAM)logMsg.c_str());
        }

        int progress = (i + 1) * 100 / apkCount;
        PostMessage((HWND)lpParam, WM_USER + 3, progress, 0);
    }

    PostMessage((HWND)lpParam, WM_USER + 1, 0, 0);
    return 0;
}

void InstallAPKs() {
    if (apkFiles.empty()) {
        MessageBox(NULL, L"No APK files to install", L"Info", MB_ICONINFORMATION);
        return;
    }

    EnableWindow(hBtnInstall, FALSE);
    EnableWindow(hBtnClear, FALSE);
    EnableWindow(hBtnAdbPath, FALSE);
    EnableWindow(hEditAdbPath, FALSE);
    EnableWindow(hListBoxApk, FALSE);

    SendMessage(hProgress, PBM_SETPOS, 0, 0);

    std::thread installThread(InstallThread, GetActiveWindow());
    installThread.detach();
}

void BrowseForADB() {
    OPENFILENAME ofn;
    wchar_t fileName[MAX_PATH] = L"";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = fileName;
    ofn.lpstrFile[0] = L'\0';
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"ADB Executable\0adb.exe\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = L"Select ADB executable";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn)) {
        SetWindowText(hEditAdbPath, fileName);
    }
}

void ScanForAPKs() {
    apkFiles.clear();

    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile(L"*.apk", &findFileData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                apkFiles.push_back(findFileData.cFileName);
            }
        } while (FindNextFile(hFind, &findFileData));
        FindClose(hFind);
    }
    RefreshApkList();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        GetCurrentDirectory(MAX_PATH, currentPath);

        FindADB();

        HWND hLabelAdb = CreateWindow(L"STATIC", L"ADB Path:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 35, 100, 20, hwnd, NULL, NULL, NULL);

        HWND hLabelApk = CreateWindow(L"STATIC", L"APK Files",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 75, 100, 20, hwnd, NULL, NULL, NULL);

        HWND hLabelLog = CreateWindow(L"STATIC", L"Logs",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 245, 100, 20, hwnd, NULL, NULL, NULL);

        hEditAdbPath = CreateWindow(L"EDIT", adbPath.c_str(),
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            10, 10, 600, 25, hwnd, (HMENU)ID_EDIT_ADB_PATH, NULL, NULL);

        hBtnAdbPath = CreateWindow(L"BUTTON", L"Browse ADB...",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            620, 10, 100, 25, hwnd, (HMENU)ID_BTN_ADB_PATH, NULL, NULL);

        hListBoxApk = CreateWindow(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | WS_BORDER,
            10, 50, 710, 150, hwnd, (HMENU)ID_LISTBOX_APK, NULL, NULL);

        hListBoxLog = CreateWindow(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | WS_BORDER,
            10, 220, 710, 150, hwnd, (HMENU)ID_LISTBOX_LOG, NULL, NULL);

        hProgress = CreateWindow(PROGRESS_CLASS, NULL,
            WS_CHILD | WS_VISIBLE,
            10, 380, 710, 20, hwnd, (HMENU)ID_PROGRESS, NULL, NULL);
        SendMessage(hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(hProgress, PBM_SETPOS, 0, 0);

        hBtnClear = CreateWindow(L"BUTTON", L"Clear List",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            10, 410, 100, 30, hwnd, (HMENU)ID_BTN_CLEAR, NULL, NULL);

        hBtnInstall = CreateWindow(L"BUTTON", L"Install All",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            120, 410, 100, 30, hwnd, (HMENU)ID_BTN_INSTALL, NULL, NULL);

        RefreshApkList();
        SendMessage(hListBoxLog, LB_RESETCONTENT, 0, 0);
        SendMessage(hListBoxLog, LB_ADDSTRING, 0, (LPARAM)L"Installation logs will appear here");

        return 0;
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(0, 0, 0));
        return (LRESULT)GetStockObject(BLACK_BRUSH);
    }

    case WM_ERASEBKGND: {
        HBRUSH hbr = CreateSolidBrush(RGB(0, 0, 0));
        RECT rect;
        GetClientRect(hwnd, &rect);
        FillRect((HDC)wParam, &rect, hbr);
        DeleteObject(hbr);
        return 1;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        if (wmId == ID_BTN_CLEAR) {
            apkFiles.clear();
            RefreshApkList();
            SendMessage(hListBoxLog, LB_RESETCONTENT, 0, 0);
            SendMessage(hListBoxLog, LB_ADDSTRING, 0, (LPARAM)L"Installation logs will appear here");
            SendMessage(hProgress, PBM_SETPOS, 0, 0);
        }
        else if (wmId == ID_BTN_INSTALL) {
            InstallAPKs();
        }
        else if (wmId == ID_BTN_ADB_PATH) {
            BrowseForADB();
        }
        return 0;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT fileCount = DragQueryFile(hDrop, 0xFFFFFFFF, NULL, 0);

        for (UINT i = 0; i < fileCount; i++) {
            wchar_t filePath[MAX_PATH];
            DragQueryFile(hDrop, i, filePath, MAX_PATH);

            std::wstring fileExt = filePath;
            size_t dotPos = fileExt.find_last_of(L".");
            if (dotPos != std::wstring::npos) {
                std::wstring ext = fileExt.substr(dotPos);
                if (_wcsicmp(ext.c_str(), L".apk") == 0) {
                    wchar_t fileName[MAX_PATH];
                    wchar_t filePathOnly[MAX_PATH];
                    GetFullPathName(filePath, MAX_PATH, filePathOnly, NULL);
                    GetFileTitle(filePath, fileName, MAX_PATH);

                    SetCurrentDirectory(currentPath);
                    if (CopyFile(filePathOnly, fileName, FALSE)) {
                        apkFiles.push_back(std::wstring(fileName));
                        RefreshApkList();
                        AddToLog(L"Added: " + std::wstring(fileName));
                    }
                    else {
                        AddToLog(L"Failed to copy: " + std::wstring(fileName));
                    }
                }
            }
        }
        DragFinish(hDrop);
        return 0;
    }

    case WM_USER + 1:
        EnableWindow(hBtnInstall, TRUE);
        EnableWindow(hBtnClear, TRUE);
        EnableWindow(hBtnAdbPath, TRUE);
        EnableWindow(hEditAdbPath, TRUE);
        EnableWindow(hListBoxApk, TRUE);
        SendMessage(hProgress, PBM_SETPOS, 100, 0);
        AddToLog(L"Installation completed!");
        MessageBox(NULL, L"Installation completed", L"Info", MB_ICONINFORMATION);
        return 0;

    case WM_USER + 2:
        EnableWindow(hBtnInstall, TRUE);
        EnableWindow(hBtnClear, TRUE);
        EnableWindow(hBtnAdbPath, TRUE);
        EnableWindow(hEditAdbPath, TRUE);
        EnableWindow(hListBoxApk, TRUE);
        SendMessage(hProgress, PBM_SETPOS, 0, 0);
        AddToLog(L"Installation failed: " + std::wstring((LPCWSTR)lParam));
        MessageBox(NULL, (LPCWSTR)lParam, L"Error", MB_ICONERROR);
        return 0;

    case WM_USER + 3:
        SendMessage(hProgress, PBM_SETPOS, (int)wParam, 0);
        return 0;

    case WM_USER + 4:
        AddToLog(std::wstring((LPCWSTR)lParam));
        return 0;

    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        MoveWindow(hEditAdbPath, 10, 10, width - 130, 25, TRUE);
        MoveWindow(hBtnAdbPath, width - 110, 10, 100, 25, TRUE);
        MoveWindow(hListBoxApk, 10, 50, width - 20, 150, TRUE);
        MoveWindow(hListBoxLog, 10, 220, width - 20, 150, TRUE);
        MoveWindow(hProgress, 10, 380, width - 20, 20, TRUE);
        MoveWindow(hBtnClear, 10, height - 60, 100, 30, TRUE);
        MoveWindow(hBtnInstall, 120, height - 60, 100, 30, TRUE);

        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"APKInstaller";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, L"APKInstaller", L"APK Installer",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        750, 500, NULL, NULL, hInstance, NULL);

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}