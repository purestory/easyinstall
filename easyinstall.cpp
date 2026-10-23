#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include "Resource.h"
#include "PackageMeta.h"
#include "InstallerWizard.h"
#include "Uninstaller.h"
#include "CliBuilder.h"
#include "GuiBuilder.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

static void hideConsoleWindow() {
    HWND hConsole = GetConsoleWindow();
    if (hConsole) {
        ShowWindow(hConsole, SW_HIDE);
        FreeConsole();
    }
}

static bool isProcessElevated() {
    BOOL isElevated = FALSE;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            isElevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return isElevated != FALSE;
}

static const wchar_t* getCommandLineArgsOnly() {
    const wchar_t* pCmd = GetCommandLineW();
    if (!pCmd) return L"";
    if (*pCmd == L'\"') {
        pCmd++;
        while (*pCmd && *pCmd != L'\"') pCmd++;
        if (*pCmd == L'\"') pCmd++;
    } else {
        while (*pCmd && *pCmd > L' ') pCmd++;
    }
    while (*pCmd && *pCmd <= L' ') pCmd++;
    return pCmd;
}

int wmain(int argc, wchar_t* argv[]) {
    // 1. 고DPI Per-Monitor V2 인식 설정 (GEMINI.md 원칙 준수)
    typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto pSetDpi = reinterpret_cast<SetProcessDpiAwarenessContextProc>(GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (pSetDpi) {
            pSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    // 2. Common Controls v6 초기화
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    // 3. COM 아파트먼트 초기화
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    // 4. 설치 패키지(Setup.exe 또는 uninstall.exe) 페이로드 확인
    PackageMeta meta;
    EasyInstallFooter footer;
    bool isSetupBinary = InstallerWizard::detectPayload(meta, footer);

    if (isSetupBinary) {
        // 인스톨러/언인스톨러는 콘솔 창이 필요 없으므로 숨김
        hideConsoleWindow();

        std::wstring cmdLine = GetCommandLineW();

        wchar_t selfExe[MAX_PATH];
        GetModuleFileNameW(nullptr, selfExe, MAX_PATH);
        std::wstring exeName = PathFindFileNameW(selfExe);
        for (wchar_t& c : exeName) c = towlower(c);

        // 언인스톨 모드인지 확인 (인자에 --uninstall이 있거나 실행 바이너리명이 uninstall.exe인 경우)
        if (cmdLine.find(L"--uninstall") != std::wstring::npos || exeName.find(L"uninst") != std::wstring::npos) {
            int ret = Uninstaller::run(hInstance, cmdLine);
            CoUninitialize();
            return ret;
        }

        // PerMachine 설치본인데 관리자 권한이 없는 경우: UAC 관리자 권한 승격(runas) 실행
        if (meta.defaultScope == EasyInstallScope::PerMachine && !isProcessElevated()) {
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.hwnd = nullptr;
            sei.lpVerb = L"runas";
            sei.lpFile = selfExe;
            sei.lpParameters = getCommandLineArgsOnly();
            sei.nShow = SW_SHOW;

            if (ShellExecuteExW(&sei)) {
                // 승격된 프로세스가 성공적으로 실행되었으므로 현재 비권한 프로세스는 즉시 종료
                CoUninitialize();
                return 0;
            } else {
                // 사용자가 UAC '아니오'를 누르거나 거부한 경우
                DWORD dwErr = GetLastError();
                if (dwErr == ERROR_CANCELLED) {
                    int choice = MessageBoxW(nullptr,
                        L"이 프로그램은 모든 사용자용(Program Files)으로 설치하기 위해 관리자 권한이 필요합니다.\n\n관리자 권한 없이 현재 사용자 전용(LocalAppData)으로 설치를 계속 진행하시겠습니까?",
                        L"설치 권한 안내", MB_YESNO | MB_ICONQUESTION);
                    if (choice == IDYES) {
                        meta.defaultScope = EasyInstallScope::PerUser;
                    } else {
                        CoUninitialize();
                        return 1;
                    }
                }
            }
        }

        // 4단계 정식 설치 마법사 실행
        INT_PTR res = InstallerWizard::showWizard(hInstance, nullptr, meta, footer);
        CoUninitialize();
        return (res == IDOK) ? 0 : 1;
    }

    // 5. 빌더(easyinstall.exe) 모드
    // CLI 인자가 주어졌는지 검사
    if (argc > 1) {
        int cliRes = CliBuilder::run(argc, argv);
        if (cliRes >= 0) {
            CoUninitialize();
            return cliRes;
        }
    }

    // 6. GUI 대화상자 빌더 실행 (인자 없음 -> 콘솔 창 숨기고 GUI 표시)
    hideConsoleWindow();
    INT_PTR guiRes = GuiBuilder::showDialog(hInstance);
    CoUninitialize();
    return (guiRes == IDOK) ? 0 : 0;
}
