#include "Uninstaller.h"
#include "InstallerWizard.h"
#include "Resource.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <vector>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

// 관리자 권한 프로세스 제어를 위한 디버그 권한 활성화
static bool enableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        return false;
    }
    LUID luid;
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return false;
    }
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD dwErr = GetLastError();
    CloseHandle(hToken);
    return (ok && dwErr != ERROR_NOT_ALL_ASSIGNED);
}

struct UninstCloseContext {
    DWORD pid;
    std::vector<HWND> hwnds;
};

static BOOL CALLBACK EnumWindowsUninstProc(HWND hWnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<UninstCloseContext*>(lParam);
    DWORD wndPid = 0;
    GetWindowThreadProcessId(hWnd, &wndPid);
    if (wndPid == ctx->pid && IsWindowVisible(hWnd)) {
        ctx->hwnds.push_back(hWnd);
    }
    return TRUE;
}

// 대상 설치 폴더에서 실행 중인 모든 프로세스(PID) 검색
static std::vector<DWORD> findProcessesInDir(const std::wstring& targetDir) {
    std::vector<DWORD> pids;
    DWORD currentPid = GetCurrentProcessId();

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return pids;

    std::wstring normTargetDir = targetDir;
    while (!normTargetDir.empty() && (normTargetDir.back() == L'\\' || normTargetDir.back() == L'/')) {
        normTargetDir.pop_back();
    }
    for (wchar_t& c : normTargetDir) c = towlower(c);

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == currentPid || pe.th32ProcessID <= 4) {
                continue;
            }

            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (hProc) {
                wchar_t fullPath[MAX_PATH] = { 0 };
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(hProc, 0, fullPath, &size)) {
                    std::wstring lowerFullPath = fullPath;
                    for (wchar_t& c : lowerFullPath) c = towlower(c);

                    if (!normTargetDir.empty() && lowerFullPath.find(normTargetDir) == 0) {
                        pids.push_back(pe.th32ProcessID);
                    }
                }
                CloseHandle(hProc);
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pids;
}

// 대상 설치 폴더에서 실행 중인 프로세스 일괄 안전 종료
static void terminateProcessesInDir(const std::wstring& targetDir) {
    enableDebugPrivilege();
    std::vector<DWORD> pids = findProcessesInDir(targetDir);
    for (DWORD pid : pids) {
        // 1. WM_CLOSE 정상 종료 시도
        UninstCloseContext ctx = { pid, {} };
        EnumWindows(EnumWindowsUninstProc, reinterpret_cast<LPARAM>(&ctx));
        for (HWND hWnd : ctx.hwnds) {
            SendMessageTimeoutW(hWnd, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 500, nullptr);
        }

        // 2. 프로세스 핸들 열기 및 강제 종료
        HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (hProc) {
            DWORD waitRes = WaitForSingleObject(hProc, 1500);
            if (waitRes != WAIT_OBJECT_0) {
                TerminateProcess(hProc, 0);
                WaitForSingleObject(hProc, 2000);
            }
            CloseHandle(hProc);
        }
    }
    if (!pids.empty()) {
        Sleep(300);
    }
}

static bool safeDeleteFile(const std::wstring& filePath, int retries = 10, int delayMs = 150) {
    SetFileAttributesW(filePath.c_str(), FILE_ATTRIBUTE_NORMAL);
    for (int i = 0; i < retries; ++i) {
        if (DeleteFileW(filePath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        Sleep(delayMs);
    }
    return false;
}

static bool safeRemoveDirectory(const std::wstring& dirPath, int retries = 10, int delayMs = 150) {
    SetFileAttributesW(dirPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    for (int i = 0; i < retries; ++i) {
        if (RemoveDirectoryW(dirPath.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        Sleep(delayMs);
    }
    return false;
}

static void deleteDirectoryRecursive(const std::wstring& dirPath) {
    std::wstring searchPattern = dirPath + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        std::wstring itemPath = dirPath + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            deleteDirectoryRecursive(itemPath);
        } else {
            safeDeleteFile(itemPath);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    safeRemoveDirectory(dirPath);
}

static void removeShortcutsAndRegistry(const std::wstring& appName, EasyInstallScope scope) {
    // 1. 바탕화면 바로가기 삭제 (User & Common)
    const int desktopCsidls[] = { CSIDL_DESKTOPDIRECTORY, CSIDL_COMMON_DESKTOPDIRECTORY };
    for (int csidl : desktopCsidls) {
        wchar_t desktopDir[MAX_PATH];
        if (SHGetSpecialFolderPathW(nullptr, desktopDir, csidl, FALSE)) {
            std::wstring deskLnk = std::wstring(desktopDir) + L"\\" + appName + L".lnk";
            if (PathFileExistsW(deskLnk.c_str())) {
                DeleteFileW(deskLnk.c_str());
            }
        }
    }

    // 2. 시작 메뉴 바로가기 및 프로그램 폴더 삭제 (User & Common)
    const int programCsidls[] = { CSIDL_PROGRAMS, CSIDL_COMMON_PROGRAMS };
    for (int csidl : programCsidls) {
        wchar_t programsDir[MAX_PATH];
        if (SHGetSpecialFolderPathW(nullptr, programsDir, csidl, FALSE)) {
            std::wstring menuDir = std::wstring(programsDir) + L"\\" + appName;
            if (PathFileExistsW(menuDir.c_str())) {
                deleteDirectoryRecursive(menuDir);
            }
            std::wstring singleLnk = std::wstring(programsDir) + L"\\" + appName + L".lnk";
            if (PathFileExistsW(singleLnk.c_str())) {
                DeleteFileW(singleLnk.c_str());
            }
        }
    }

    // 3. 제어판 등록 정보 삭제 (HKLM & HKCU)
    const HKEY roots[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    for (HKEY hRoot : roots) {
        std::wstring subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + appName;
        RegDeleteKeyW(hRoot, subKey.c_str());
    }

    // 4. 셸 아이콘 및 바로가기 변경 알림
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

static INT_PTR CALLBACK UninstDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static PackageMeta s_meta;

    switch (msg) {
    case WM_INITDIALOG: {
        PackageMeta* pMeta = reinterpret_cast<PackageMeta*>(lParam);
        if (pMeta) {
            s_meta = *pMeta;
        }

        SetWindowTextW(hDlg, (s_meta.appName + L" 제거").c_str());
        SetDlgItemTextW(hDlg, IDC_STATIC_UNINST_TITLE, (s_meta.appName + L" 프로그램을 컴퓨터에서 완전히 제거하시겠습니까?").c_str());
        SendDlgItemMessageW(hDlg, IDC_PROGRESS_UNINSTALL, PBM_SETRANGE32, 0, 100);
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == IDC_BTN_UNINST_START) {
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_UNINST_START), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_UNINST_CANCEL), FALSE);

            SendDlgItemMessageW(hDlg, IDC_PROGRESS_UNINSTALL, PBM_SETPOS, 20, 0);
            SetDlgItemTextW(hDlg, IDC_STATIC_UNINST_STATUS, L"임시 작업자 프로세스 준비 중...");

            // 현재 실행 파일 및 디렉터리
            wchar_t selfExe[MAX_PATH];
            GetModuleFileNameW(nullptr, selfExe, MAX_PATH);
            wchar_t installDir[MAX_PATH];
            wcscpy_s(installDir, selfExe);
            PathRemoveFileSpecW(installDir);

            // 실행 중인 프로그램 감지 및 사용자 안내
            std::vector<DWORD> running = findProcessesInDir(installDir);
            if (!running.empty()) {
                std::wstring msg = s_meta.appName + L" 프로그램 또는 관련 프로세스가 현재 실행 중입니다.\n\n프로그램을 완전히 제거하기 위해 실행 중인 프로세스를 자동으로 종료하고 계속 진행하시겠습니까?";
                int choice = MessageBoxW(hDlg, msg.c_str(), L"실행 중인 프로그램 감지", MB_YESNO | MB_ICONQUESTION);
                if (choice != IDYES) {
                    EnableWindow(GetDlgItem(hDlg, IDC_BTN_UNINST_START), TRUE);
                    EnableWindow(GetDlgItem(hDlg, IDC_BTN_UNINST_CANCEL), TRUE);
                    SendDlgItemMessageW(hDlg, IDC_PROGRESS_UNINSTALL, PBM_SETPOS, 0, 0);
                    SetDlgItemTextW(hDlg, IDC_STATIC_UNINST_STATUS, L"제거가 취소되었습니다.");
                    return TRUE;
                }
                terminateProcessesInDir(installDir);
            }

            // 임시 폴더에 복제
            wchar_t tempDir[MAX_PATH];
            GetTempPathW(MAX_PATH, tempDir);
            wchar_t tempExe[MAX_PATH];
            swprintf_s(tempExe, MAX_PATH, L"%seasyinstall_uninst_%u.exe", tempDir, GetTickCount());

            if (!CopyFileW(selfExe, tempExe, FALSE)) {
                MessageBoxW(hDlg, L"임시 제거 프로그램을 생성하지 못했습니다.", L"오류", MB_OK | MB_ICONERROR);
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }

            // 임시 exe를 --uninstall-worker 모드로 실행
            DWORD currentPid = GetCurrentProcessId();
            std::wstring args = L"--uninstall-worker \"" + std::wstring(installDir) + L"\" \"" + s_meta.appName + L"\" " + std::to_wstring(static_cast<int>(s_meta.defaultScope)) + L" " + std::to_wstring(currentPid);
            
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.hwnd = hDlg;
            sei.lpVerb = (s_meta.defaultScope == EasyInstallScope::PerMachine) ? L"runas" : L"open";
            sei.lpFile = tempExe;
            sei.lpParameters = args.c_str();
            sei.nShow = SW_SHOW;

            if (ShellExecuteExW(&sei)) {
                EndDialog(hDlg, IDOK);
            } else {
                DeleteFileW(tempExe);
                MessageBoxW(hDlg, L"제거 프로세스를 시작할 수 없습니다.", L"오류", MB_OK | MB_ICONERROR);
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        }

        if (id == IDC_BTN_UNINST_CANCEL || id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

int Uninstaller::run(HINSTANCE hInstance, const std::wstring& cmdLine) {
    // 1. 만약 --uninstall-worker 인자가 있다면 (임시 폴더에서 실행된 실제 삭제 루틴)
    if (cmdLine.find(L"--uninstall-worker") != std::wstring::npos) {
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(cmdLine.c_str(), &argc);
        if (argv) {
            int workerIdx = -1;
            for (int i = 0; i < argc; ++i) {
                if (wcscmp(argv[i], L"--uninstall-worker") == 0) {
                    workerIdx = i;
                    break;
                }
            }

            if (workerIdx >= 0 && (workerIdx + 2) < argc) {
                std::wstring targetDir = argv[workerIdx + 1];
                std::wstring appName = argv[workerIdx + 2];
                EasyInstallScope scope = EasyInstallScope::PerMachine;
                if (workerIdx + 3 < argc) {
                    scope = static_cast<EasyInstallScope>(_wtoi(argv[workerIdx + 3]));
                }
                DWORD parentPid = 0;
                if (workerIdx + 4 < argc) {
                    parentPid = static_cast<DWORD>(_wtoi(argv[workerIdx + 4]));
                }

                // 부모 프로세스(uninstall.exe)가 완전히 종료되어 파일 핸들을 놓을 때까지 대기
                if (parentPid > 0) {
                    HANDLE hParent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
                    if (hParent) {
                        WaitForSingleObject(hParent, 5000);
                        CloseHandle(hParent);
                    }
                }
                Sleep(500);

                // 설치 폴더 내 남아있는 모든 프로세스(워커, 백그라운드 프로세스 등) 일괄 강제 종료
                terminateProcessesInDir(targetDir);
                Sleep(300);

                // 바로가기 및 레지스트리 제거
                removeShortcutsAndRegistry(appName, scope);

                // 설치 디렉터리 내 파일 제거
                if (!targetDir.empty() && PathFileExistsW(targetDir.c_str())) {
                    deleteDirectoryRecursive(targetDir);
                }

                // 사용자 알림
                MessageBoxW(nullptr, (appName + L" 프로그램이 컴퓨터에서 성공적으로 제거되었습니다.").c_str(), L"제거 완료", MB_OK | MB_ICONINFORMATION);

                // 자기 자신(임시 파일) 자폭 삭제 명령 실행
                wchar_t selfExe[MAX_PATH];
                GetModuleFileNameW(nullptr, selfExe, MAX_PATH);

                std::wstring cmd = L"/c timeout /t 1 /nobreak > NUL & del /f /q \"" + std::wstring(selfExe) + L"\"";
                ShellExecuteW(nullptr, L"open", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE);

                LocalFree(argv);
                return 0;
            }
            LocalFree(argv);
        }
    }

    // 2. 일반 --uninstall 인자 또는 직접 실행된 경우: 사용자에게 확인 다이얼로그 표시
    PackageMeta meta;
    EasyInstallFooter footer;
    if (!InstallerWizard::detectPayload(meta, footer)) {
        // 푸터가 없으면 폴더명 기반 기본값 생성
        wchar_t selfPath[MAX_PATH];
        GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
        PathRemoveFileSpecW(selfPath);
        meta.appName = PathFindFileNameW(selfPath);
        meta.defaultScope = EasyInstallScope::PerMachine;
    }

    DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_UNINSTALL_DIALOG), nullptr, UninstDlgProc, reinterpret_cast<LPARAM>(&meta));
    return 0;
}
