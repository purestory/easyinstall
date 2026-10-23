#include "InstallerWizard.h"
#include "Resource.h"
#include "miniz.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <thread>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

constexpr UINT WM_INSTALL_PROGRESS = WM_USER + 100;
constexpr UINT WM_INSTALL_COMPLETE = WM_USER + 101;

static std::wstring formatBytes(uint64_t bytes) {
    double d = static_cast<double>(bytes);
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    int idx = 0;
    while (d >= 1024.0 && idx < 4) {
        d /= 1024.0;
        idx++;
    }
    std::wstringstream ss;
    ss << std::fixed << std::setprecision(1) << d << L" " << units[idx];
    return ss.str();
}

static bool createShortcut(const std::wstring& targetExePath, const std::wstring& shortcutPath, const std::wstring& description, const std::wstring& workingDir, const std::wstring& arguments = L"") {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool coInited = SUCCEEDED(hr);

    IShellLinkW* psl = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&psl));
    if (SUCCEEDED(hr)) {
        psl->SetPath(targetExePath.c_str());
        if (!arguments.empty()) {
            psl->SetArguments(arguments.c_str());
        } else {
            psl->SetArguments(L"");
        }
        psl->SetDescription(description.c_str());
        if (!workingDir.empty()) {
            psl->SetWorkingDirectory(workingDir.c_str());
        }

        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(shortcutPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }

    if (coInited) CoUninitialize();
    return SUCCEEDED(hr);
}

bool InstallerWizard::detectPayload(PackageMeta& outMeta, EasyInstallFooter& outFooter) {
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);

    HANDLE hFile = CreateFileW(selfPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    if (fileSize.QuadPart <= sizeof(EasyInstallFooter)) {
        CloseHandle(hFile);
        return false;
    }

    SetFilePointer(hFile, -static_cast<LONG>(sizeof(EasyInstallFooter)), nullptr, FILE_END);
    EasyInstallFooter footer;
    DWORD readBytes = 0;
    if (!ReadFile(hFile, &footer, sizeof(footer), &readBytes, nullptr) || readBytes != sizeof(footer)) {
        CloseHandle(hFile);
        return false;
    }

    if (footer.magic != EASYINSTALL_MAGIC) {
        CloseHandle(hFile);
        return false;
    }

    // 메타데이터 읽기
    LARGE_INTEGER liMeta;
    liMeta.QuadPart = footer.metaOffset;
    SetFilePointerEx(hFile, liMeta, nullptr, FILE_BEGIN);

    std::vector<unsigned char> metaBuf(footer.metaSize);
    if (!ReadFile(hFile, metaBuf.data(), footer.metaSize, &readBytes, nullptr) || readBytes != footer.metaSize) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    if (!PackageMeta::deserialize(metaBuf.data(), metaBuf.size(), outMeta)) {
        return false;
    }

    outFooter = footer;
    return true;
}

struct WizardState {
    int currentPage; // 1: 환영, 2: 옵션/경로, 3: 진행, 4: 완료
    PackageMeta meta;
    EasyInstallFooter footer;
    std::wstring installPath;
    bool createDesktop;
    bool createStartMenu;
    bool launchNow;
    bool installSuccess;
    std::wstring installError;
};

static void updateWizardPage(HWND hDlg, WizardState* state) {
    // 1단계 컨트롤
    int show1 = (state->currentPage == 1) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hDlg, IDC_STATIC_WELCOME_TITLE), show1);
    ShowWindow(GetDlgItem(hDlg, IDC_STATIC_WELCOME_DESC), show1);

    // 2단계 컨트롤
    int show2 = (state->currentPage == 2) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hDlg, IDC_STATIC_PATH_LABEL), show2);
    ShowWindow(GetDlgItem(hDlg, IDC_EDIT_INSTALL_PATH), show2);
    ShowWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE_PATH), show2);
    ShowWindow(GetDlgItem(hDlg, IDC_STATIC_SPACE_REQ), show2);
    ShowWindow(GetDlgItem(hDlg, IDC_STATIC_SPACE_AVAIL), show2);
    ShowWindow(GetDlgItem(hDlg, IDC_GRP_OPTIONS), show2);
    ShowWindow(GetDlgItem(hDlg, IDC_CHK_OPT_DESKTOP), show2);
    ShowWindow(GetDlgItem(hDlg, IDC_CHK_OPT_STARTMENU), show2);

    // 3단계 컨트롤
    int show3 = (state->currentPage == 3) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hDlg, IDC_STATIC_INSTALLING_MSG), show3);
    ShowWindow(GetDlgItem(hDlg, IDC_PROGRESS_INSTALL), show3);
    ShowWindow(GetDlgItem(hDlg, IDC_STATIC_INSTALL_DETAIL), show3);

    // 4단계 컨트롤
    int show4 = (state->currentPage == 4) ? SW_SHOW : SW_HIDE;
    ShowWindow(GetDlgItem(hDlg, IDC_STATIC_COMPLETE_TITLE), show4);
    ShowWindow(GetDlgItem(hDlg, IDC_STATIC_COMPLETE_DESC), show4);
    ShowWindow(GetDlgItem(hDlg, IDC_CHK_LAUNCH_NOW), show4);

    // 잔상 및 컨트롤 겹침 방지 재도색
    InvalidateRect(hDlg, nullptr, TRUE);
    UpdateWindow(hDlg);

    // 헤더 및 버튼 갱신
    switch (state->currentPage) {
    case 1:
        SetDlgItemTextW(hDlg, IDC_WIZARD_HEADER_TITLE, L"설치 마법사를 시작합니다");
        SetDlgItemTextW(hDlg, IDC_WIZARD_HEADER_SUBTITLE, (state->meta.appName + L" " + state->meta.appVersion + L" 버전을 설치합니다.").c_str());
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_BACK), FALSE);
        SetDlgItemTextW(hDlg, IDC_BTN_NEXT, L"다음(N) >");
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_NEXT), TRUE);
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_CANCEL), TRUE);
        break;
    case 2:
        SetDlgItemTextW(hDlg, IDC_WIZARD_HEADER_TITLE, L"설치 위치 및 옵션 선택");
        SetDlgItemTextW(hDlg, IDC_WIZARD_HEADER_SUBTITLE, L"설치할 대상 폴더와 바로가기 생성 여부를 지정하십시오.");
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_BACK), TRUE);
        SetDlgItemTextW(hDlg, IDC_BTN_NEXT, L"설치(I) >");
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_NEXT), TRUE);
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_CANCEL), TRUE);
        break;
    case 3:
        SetDlgItemTextW(hDlg, IDC_WIZARD_HEADER_TITLE, L"프로그램 설치 진행 중");
        SetDlgItemTextW(hDlg, IDC_WIZARD_HEADER_SUBTITLE, L"파일을 압축 해제하고 바로가기를 구성하고 있습니다...");
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_BACK), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_NEXT), FALSE);
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_CANCEL), FALSE);
        break;
    case 4:
        SetDlgItemTextW(hDlg, IDC_WIZARD_HEADER_TITLE, state->installSuccess ? L"설치 완료" : L"설치 실패");
        SetDlgItemTextW(hDlg, IDC_WIZARD_HEADER_SUBTITLE, state->installSuccess ? L"프로그램 설치가 정상적으로 완료되었습니다." : L"설치 도중 오류가 발생했습니다.");
        ShowWindow(GetDlgItem(hDlg, IDC_BTN_BACK), SW_HIDE);
        ShowWindow(GetDlgItem(hDlg, IDC_BTN_CANCEL), SW_HIDE);
        SetDlgItemTextW(hDlg, IDC_BTN_NEXT, L"완료(F)");
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_NEXT), TRUE);
        SetFocus(GetDlgItem(hDlg, IDC_BTN_NEXT));
        break;
    }
}

static void updateFreeSpace(HWND hDlg, const std::wstring& path) {
    ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
    if (GetDiskFreeSpaceExW(path.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes)) {
        SetDlgItemTextW(hDlg, IDC_STATIC_SPACE_AVAIL, (L"사용 가능 공간: " + formatBytes(freeBytesAvailable.QuadPart)).c_str());
    } else {
        SetDlgItemTextW(hDlg, IDC_STATIC_SPACE_AVAIL, L"사용 가능 공간: 확인 불가");
    }
}

static void runInstallWorker(HWND hDlg, WizardState* state) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    std::wstring targetDir = state->installPath;
    if (!PathFileExistsW(targetDir.c_str())) {
        int err = SHCreateDirectoryExW(nullptr, targetDir.c_str(), nullptr);
        if (err != ERROR_SUCCESS && err != ERROR_ALREADY_EXISTS && err != ERROR_FILE_EXISTS) {
            state->installSuccess = false;
            state->installError = L"설치 대상 폴더를 생성할 수 없습니다.\n경로: " + targetDir + L"\n(관리자 권한이 부족하거나 경로가 올바르지 않습니다.)";
            PostMessageW(hDlg, WM_INSTALL_COMPLETE, 0, 0);
            CoUninitialize();
            return;
        }
    }

    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);

    FILE* pFile = _wfopen(selfPath, L"rb");
    if (!pFile) {
        state->installSuccess = false;
        state->installError = L"설치 바이너리를 열 수 없습니다.";
        PostMessageW(hDlg, WM_INSTALL_COMPLETE, 0, 0);
        CoUninitialize();
        return;
    }

    _fseeki64(pFile, state->footer.zipOffset, SEEK_SET);

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_cfile(&zip, pFile, state->footer.zipSize, 0)) {
        fclose(pFile);
        state->installSuccess = false;
        state->installError = L"ZIP 압축 스트림을 열 수 없습니다.";
        PostMessageW(hDlg, WM_INSTALL_COMPLETE, 0, 0);
        CoUninitialize();
        return;
    }

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < numFiles; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;

        int percent = static_cast<int>((i * 85) / (numFiles > 0 ? numFiles : 1));
        std::wstring wFilename;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, stat.m_filename, -1, nullptr, 0);
        if (wlen > 0) {
            wFilename.resize(wlen - 1);
            MultiByteToWideChar(CP_UTF8, 0, stat.m_filename, -1, &wFilename[0], wlen - 1);
        }

        PostMessageW(hDlg, WM_INSTALL_PROGRESS, percent, reinterpret_cast<LPARAM>(_wcsdup(wFilename.c_str())));

        std::wstring outFilePath = targetDir + L"\\" + wFilename;
        for (wchar_t& c : outFilePath) {
            if (c == L'/') c = L'\\';
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            int err = SHCreateDirectoryExW(nullptr, outFilePath.c_str(), nullptr);
            if (err != ERROR_SUCCESS && err != ERROR_ALREADY_EXISTS && err != ERROR_FILE_EXISTS) {
                mz_zip_reader_end(&zip);
                fclose(pFile);
                state->installSuccess = false;
                state->installError = L"디렉터리 생성 실패:\n" + outFilePath;
                PostMessageW(hDlg, WM_INSTALL_COMPLETE, 0, 0);
                CoUninitialize();
                return;
            }
        } else {
            wchar_t dirPart[MAX_PATH];
            wcscpy_s(dirPart, outFilePath.c_str());
            PathRemoveFileSpecW(dirPart);
            if (wcslen(dirPart) > 0 && !PathFileExistsW(dirPart)) {
                int err = SHCreateDirectoryExW(nullptr, dirPart, nullptr);
                if (err != ERROR_SUCCESS && err != ERROR_ALREADY_EXISTS && err != ERROR_FILE_EXISTS) {
                    mz_zip_reader_end(&zip);
                    fclose(pFile);
                    state->installSuccess = false;
                    state->installError = L"하위 디렉터리 생성 실패:\n" + std::wstring(dirPart);
                    PostMessageW(hDlg, WM_INSTALL_COMPLETE, 0, 0);
                    CoUninitialize();
                    return;
                }
            }

            size_t uncompSize = 0;
            void* pBuf = mz_zip_reader_extract_to_heap(&zip, i, &uncompSize, 0);
            if (!pBuf && stat.m_uncomp_size > 0) {
                mz_zip_reader_end(&zip);
                fclose(pFile);
                state->installSuccess = false;
                state->installError = L"파일 압축 해제 실패:\n" + wFilename;
                PostMessageW(hDlg, WM_INSTALL_COMPLETE, 0, 0);
                CoUninitialize();
                return;
            }

            HANDLE hOut = CreateFileW(outFilePath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hOut == INVALID_HANDLE_VALUE) {
                DWORD dwErr = GetLastError();
                if (pBuf) mz_free(pBuf);
                mz_zip_reader_end(&zip);
                fclose(pFile);
                state->installSuccess = false;
                state->installError = L"파일 생성 실패 (오류 코드 " + std::to_wstring(dwErr) + L"):\n" + outFilePath;
                PostMessageW(hDlg, WM_INSTALL_COMPLETE, 0, 0);
                CoUninitialize();
                return;
            }

            if (pBuf && uncompSize > 0) {
                DWORD written = 0;
                if (!WriteFile(hOut, pBuf, static_cast<DWORD>(uncompSize), &written, nullptr) || written != uncompSize) {
                    CloseHandle(hOut);
                    mz_free(pBuf);
                    mz_zip_reader_end(&zip);
                    fclose(pFile);
                    state->installSuccess = false;
                    state->installError = L"파일 쓰기 실패:\n" + outFilePath;
                    PostMessageW(hDlg, WM_INSTALL_COMPLETE, 0, 0);
                    CoUninitialize();
                    return;
                }
            }
            CloseHandle(hOut);
            if (pBuf) mz_free(pBuf);
        }
    }

    mz_zip_reader_end(&zip);
    fclose(pFile);

    PostMessageW(hDlg, WM_INSTALL_PROGRESS, 90, reinterpret_cast<LPARAM>(_wcsdup(L"바로가기 및 제거 프로그램 구성 중...")));

    std::wstring mainExePath = targetDir + L"\\" + state->meta.mainExeName;
    std::wstring uninstExePath = targetDir + L"\\uninstall.exe";

    // 1. 언인스톨러 복제 (targetDir\uninstall.exe)
    if (!CopyFileW(selfPath, uninstExePath.c_str(), FALSE)) {
        DWORD dwErr = GetLastError();
        state->installSuccess = false;
        state->installError = L"제거 프로그램(uninstall.exe) 복제 실패 (오류 코드 " + std::to_wstring(dwErr) + L"):\n" + uninstExePath;
        PostMessageW(hDlg, WM_INSTALL_COMPLETE, 0, 0);
        CoUninitialize();
        return;
    }

    // 2. 바탕화면 바로가기
    if (state->createDesktop) {
        wchar_t desktopDir[MAX_PATH];
        int csidl = (state->meta.defaultScope == EasyInstallScope::PerMachine) ? CSIDL_COMMON_DESKTOPDIRECTORY : CSIDL_DESKTOPDIRECTORY;
        if (!SHGetSpecialFolderPathW(nullptr, desktopDir, csidl, FALSE)) {
            SHGetSpecialFolderPathW(nullptr, desktopDir, CSIDL_DESKTOPDIRECTORY, FALSE);
        }
        std::wstring shortcutPath = std::wstring(desktopDir) + L"\\" + state->meta.appName + L".lnk";
        createShortcut(mainExePath, shortcutPath, state->meta.appName, targetDir);
    }

    // 3. 시작 메뉴 바로가기 및 프로그램 폴더 구성
    if (state->createStartMenu) {
        wchar_t programsDir[MAX_PATH];
        int csidl = (state->meta.defaultScope == EasyInstallScope::PerMachine) ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS;
        if (!SHGetSpecialFolderPathW(nullptr, programsDir, csidl, FALSE)) {
            SHGetSpecialFolderPathW(nullptr, programsDir, CSIDL_PROGRAMS, FALSE);
        }
        std::wstring appMenuDir = std::wstring(programsDir) + L"\\" + state->meta.appName;
        SHCreateDirectoryExW(nullptr, appMenuDir.c_str(), nullptr);

        // 실행 바로가기
        std::wstring shortcutPath = appMenuDir + L"\\" + state->meta.appName + L".lnk";
        createShortcut(mainExePath, shortcutPath, state->meta.appName, targetDir);

        // 제거 바로가기 (폴더 내 2개 이상의 항목을 배치하여 Windows 10/11 시작메뉴가 폴더 그룹으로 표시하도록 강제)
        std::wstring uninstShortcutPath = appMenuDir + L"\\" + state->meta.appName + L" 제거.lnk";
        createShortcut(uninstExePath, uninstShortcutPath, state->meta.appName + L" 제거", targetDir, L"--uninstall");
    }

    // 4. 제어판 [앱 및 기능] 등록
    HKEY hRoot = (state->meta.defaultScope == EasyInstallScope::PerMachine) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    std::wstring subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + state->meta.appName;
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(hRoot, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &hKey, nullptr) != ERROR_SUCCESS) {
        if (hRoot == HKEY_LOCAL_MACHINE) {
            hRoot = HKEY_CURRENT_USER;
            RegCreateKeyExW(hRoot, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &hKey, nullptr);
        }
    }
    if (hKey) {
        RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(state->meta.appName.c_str()), static_cast<DWORD>((state->meta.appName.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE*>(state->meta.appVersion.c_str()), static_cast<DWORD>((state->meta.appVersion.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"Publisher", 0, REG_SZ, reinterpret_cast<const BYTE*>(state->meta.publisher.c_str()), static_cast<DWORD>((state->meta.publisher.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"InstallLocation", 0, REG_SZ, reinterpret_cast<const BYTE*>(targetDir.c_str()), static_cast<DWORD>((targetDir.size() + 1) * sizeof(wchar_t)));

        std::wstring iconVal = L"\"" + mainExePath + L"\",0";
        RegSetValueExW(hKey, L"DisplayIcon", 0, REG_SZ, reinterpret_cast<const BYTE*>(iconVal.c_str()), static_cast<DWORD>((iconVal.size() + 1) * sizeof(wchar_t)));

        std::wstring uninstCmd = L"\"" + uninstExePath + L"\" --uninstall";
        RegSetValueExW(hKey, L"UninstallString", 0, REG_SZ, reinterpret_cast<const BYTE*>(uninstCmd.c_str()), static_cast<DWORD>((uninstCmd.size() + 1) * sizeof(wchar_t)));

        DWORD sizeKB = static_cast<DWORD>(state->meta.uncompressedSize / 1024);
        RegSetValueExW(hKey, L"EstimatedSize", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&sizeKB), sizeof(sizeKB));

        DWORD dOne = 1;
        RegSetValueExW(hKey, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dOne), sizeof(dOne));
        RegSetValueExW(hKey, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&dOne), sizeof(dOne));

        RegCloseKey(hKey);
    }

    // 5. 셸 새로고침 알림
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    PostMessageW(hDlg, WM_INSTALL_PROGRESS, 100, reinterpret_cast<LPARAM>(_wcsdup(L"설치 완료")));
    state->installSuccess = true;
    PostMessageW(hDlg, WM_INSTALL_COMPLETE, 1, 0);

    CoUninitialize();
}

static INT_PTR CALLBACK WizardDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    WizardState* state = reinterpret_cast<WizardState*>(GetWindowLongPtr(hDlg, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<WizardState*>(lParam);
        SetWindowLongPtr(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(state));

        // 창 캡션
        SetWindowTextW(hDlg, (state->meta.appName + L" 설치 마법사").c_str());

        // 아이콘 설정
        HICON hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_EASYINSTALL));
        if (hIcon) {
            SendMessageW(hDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            SendMessageW(hDlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }

        // 기본 설치 경로 결정
        wchar_t baseFolder[MAX_PATH];
        if (state->meta.defaultScope == EasyInstallScope::PerMachine) {
            SHGetSpecialFolderPathW(nullptr, baseFolder, CSIDL_PROGRAM_FILES, FALSE);
        } else {
            SHGetSpecialFolderPathW(nullptr, baseFolder, CSIDL_LOCAL_APPDATA, FALSE);
            PathAppendW(baseFolder, L"Programs");
        }
        std::wstring defaultDir = std::wstring(baseFolder) + L"\\" + state->meta.defaultInstallDir;
        state->installPath = defaultDir;
        SetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_PATH, defaultDir.c_str());

        // 필요한 공간 표시
        SetDlgItemTextW(hDlg, IDC_STATIC_SPACE_REQ, (L"필요한 공간: " + formatBytes(state->meta.uncompressedSize)).c_str());
        updateFreeSpace(hDlg, defaultDir);

        // 옵션 체크박스 기본값
        CheckDlgButton(hDlg, IDC_CHK_OPT_DESKTOP, state->meta.createDesktopShortcut ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_OPT_STARTMENU, state->meta.createStartMenuShortcut ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_LAUNCH_NOW, state->meta.launchAfterInstall ? BST_CHECKED : BST_UNCHECKED);

        // 프로그레스바 범위 설정
        SendDlgItemMessageW(hDlg, IDC_PROGRESS_INSTALL, PBM_SETRANGE32, 0, 100);

        state->currentPage = 1;
        updateWizardPage(hDlg, state);
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD notify = HIWORD(wParam);

        if (id == IDC_EDIT_INSTALL_PATH && notify == EN_CHANGE) {
            wchar_t pathBuf[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_PATH, pathBuf, MAX_PATH);
            state->installPath = pathBuf;
            updateFreeSpace(hDlg, state->installPath);
            return TRUE;
        }

        if (id == IDC_BTN_BROWSE_PATH) {
            BROWSEINFOW bi = { 0 };
            bi.hwndOwner = hDlg;
            bi.lpszTitle = L"프로그램을 설치할 폴더를 선택하십시오:";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t selected[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, selected)) {
                    std::wstring fullTarget = std::wstring(selected) + L"\\" + state->meta.defaultInstallDir;
                    state->installPath = fullTarget;
                    SetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_PATH, fullTarget.c_str());
                    updateFreeSpace(hDlg, fullTarget);
                }
                CoTaskMemFree(pidl);
            }
            return TRUE;
        }

        if (id == IDC_BTN_BACK) {
            if (state->currentPage > 1 && state->currentPage < 3) {
                state->currentPage--;
                updateWizardPage(hDlg, state);
            }
            return TRUE;
        }

        if (id == IDC_BTN_NEXT) {
            if (state->currentPage == 1) {
                state->currentPage = 2;
                updateWizardPage(hDlg, state);
            } else if (state->currentPage == 2) {
                // 설치 시작
                wchar_t pathBuf[MAX_PATH];
                GetDlgItemTextW(hDlg, IDC_EDIT_INSTALL_PATH, pathBuf, MAX_PATH);
                state->installPath = pathBuf;
                if (state->installPath.empty()) {
                    MessageBoxW(hDlg, L"설치 폴더 경로를 입력해 주십시오.", L"안내", MB_OK | MB_ICONWARNING);
                    return TRUE;
                }

                state->createDesktop = (IsDlgButtonChecked(hDlg, IDC_CHK_OPT_DESKTOP) == BST_CHECKED);
                state->createStartMenu = (IsDlgButtonChecked(hDlg, IDC_CHK_OPT_STARTMENU) == BST_CHECKED);

                state->currentPage = 3;
                updateWizardPage(hDlg, state);

                // 백그라운드 설치 스레드 시작
                std::thread(runInstallWorker, hDlg, state).detach();
            } else if (state->currentPage == 4) {
                // 완료 및 종료
                bool launch = (IsDlgButtonChecked(hDlg, IDC_CHK_LAUNCH_NOW) == BST_CHECKED);
                if (launch && state->installSuccess) {
                    std::wstring mainExe = state->installPath + L"\\" + state->meta.mainExeName;
                    ShellExecuteW(nullptr, L"open", mainExe.c_str(), nullptr, state->installPath.c_str(), SW_SHOW);
                }
                EndDialog(hDlg, IDOK);
            }
            return TRUE;
        }

        if (id == IDC_BTN_CANCEL || id == IDCANCEL) {
            if (state->currentPage == 3) {
                // 진행 중에는 취소 불가
                return TRUE;
            }
            if (MessageBoxW(hDlg, L"설치를 중단하고 마법사를 종료하시겠습니까?", L"설치 취소", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        }
        break;
    }

    case WM_INSTALL_PROGRESS: {
        int percent = static_cast<int>(wParam);
        wchar_t* pDetail = reinterpret_cast<wchar_t*>(lParam);
        SendDlgItemMessageW(hDlg, IDC_PROGRESS_INSTALL, PBM_SETPOS, percent, 0);
        if (pDetail) {
            SetDlgItemTextW(hDlg, IDC_STATIC_INSTALL_DETAIL, pDetail);
            free(pDetail);
        }
        return TRUE;
    }

    case WM_INSTALL_COMPLETE: {
        state->installSuccess = (wParam == 1);
        state->currentPage = 4;
        updateWizardPage(hDlg, state);
        if (!state->installSuccess) {
            SetDlgItemTextW(hDlg, IDC_STATIC_COMPLETE_TITLE, L"설치가 실패하였습니다.");
            SetDlgItemTextW(hDlg, IDC_STATIC_COMPLETE_DESC, state->installError.c_str());
            ShowWindow(GetDlgItem(hDlg, IDC_CHK_LAUNCH_NOW), SW_HIDE);
        }
        return TRUE;
    }
    }
    return FALSE;
}

INT_PTR InstallerWizard::showWizard(HINSTANCE hInstance, HWND hParent, const PackageMeta& meta, const EasyInstallFooter& footer) {
    WizardState state;
    state.currentPage = 1;
    state.meta = meta;
    state.footer = footer;
    state.createDesktop = meta.createDesktopShortcut;
    state.createStartMenu = meta.createStartMenuShortcut;
    state.launchNow = meta.launchAfterInstall;
    state.installSuccess = false;

    return DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_WIZARD_DIALOG), hParent, WizardDlgProc, reinterpret_cast<LPARAM>(&state));
}
