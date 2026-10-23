#include "GuiBuilder.h"
#include "Builder.h"
#include "Resource.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <thread>
#include <vector>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "comctl32.lib")

constexpr UINT WM_BUILD_PROGRESS = WM_USER + 200;
constexpr UINT WM_BUILD_COMPLETE = WM_USER + 201;

struct GuiBuildAsyncContext {
    HWND hDlg;
    BuilderConfig config;
    BuildResult result;
};

static void onInputPathChanged(HWND hDlg, const std::wstring& path) {
    if (path.empty() || !PathFileExistsW(path.c_str())) return;

    // 프로그램 이름 기본값 설정
    wchar_t curName[MAX_PATH];
    GetDlgItemTextW(hDlg, IDC_EDIT_APP_NAME, curName, MAX_PATH);
    if (wcslen(curName) == 0) {
        std::wstring name = PathFindFileNameW(path.c_str());
        // .exe 확장자 제거
        if (name.size() > 4 && name.substr(name.size() - 4) == L".exe") {
            name = name.substr(0, name.size() - 4);
        }
        SetDlgItemTextW(hDlg, IDC_EDIT_APP_NAME, name.c_str());
    }

    // 버전 기본값
    wchar_t curVer[MAX_PATH];
    GetDlgItemTextW(hDlg, IDC_EDIT_APP_VERSION, curVer, MAX_PATH);
    if (wcslen(curVer) == 0) {
        SetDlgItemTextW(hDlg, IDC_EDIT_APP_VERSION, L"1.0.0");
    }

    // 메인 실행 파일 자동 검색
    wchar_t curExe[MAX_PATH];
    GetDlgItemTextW(hDlg, IDC_EDIT_MAIN_EXE, curExe, MAX_PATH);
    if (wcslen(curExe) == 0) {
        std::vector<std::wstring> exes = Builder::findExecutableFiles(path);
        if (!exes.empty()) {
            SetDlgItemTextW(hDlg, IDC_EDIT_MAIN_EXE, exes[0].c_str());
        }
    }

    // 출력 파일 경로 기본값 설정
    wchar_t curOut[MAX_PATH];
    GetDlgItemTextW(hDlg, IDC_EDIT_OUTPUT, curOut, MAX_PATH);
    if (wcslen(curOut) == 0) {
        wchar_t fullIn[MAX_PATH];
        GetFullPathNameW(path.c_str(), MAX_PATH, fullIn, nullptr);
        PathRemoveFileSpecW(fullIn);

        wchar_t appName[MAX_PATH];
        GetDlgItemTextW(hDlg, IDC_EDIT_APP_NAME, appName, MAX_PATH);
        std::wstring outName = (wcslen(appName) > 0 ? appName : L"App") + std::wstring(L"_Setup.exe");
        std::wstring outPath = std::wstring(fullIn) + L"\\" + outName;
        SetDlgItemTextW(hDlg, IDC_EDIT_OUTPUT, outPath.c_str());
    }
}

static void runBuildWorker(GuiBuildAsyncContext* ctx) {
    ctx->result = Builder::build(ctx->config, [ctx](int percent, const std::wstring& status) {
        PostMessageW(ctx->hDlg, WM_BUILD_PROGRESS, percent, reinterpret_cast<LPARAM>(_wcsdup(status.c_str())));
    });

    PostMessageW(ctx->hDlg, WM_BUILD_COMPLETE, 0, reinterpret_cast<LPARAM>(ctx));
}

static INT_PTR CALLBACK BuilderDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        // 아이콘 설정
        HICON hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_EASYINSTALL));
        if (hIcon) {
            SendMessageW(hDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            SendMessageW(hDlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }

        // 드래그 앤 드롭 수신 활성화
        DragAcceptFiles(hDlg, TRUE);

        // 콤보박스 채우기
        HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_SCOPE);
        SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Program Files (전체 사용자)"));
        SendMessageW(hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"AppData\\Local (현재 사용자)"));
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

        // 기본 체크박스
        CheckDlgButton(hDlg, IDC_CHK_DESKTOP, BST_CHECKED);
        CheckDlgButton(hDlg, IDC_CHK_STARTMENU, BST_CHECKED);

        // 프로그레스바 범위
        SendDlgItemMessageW(hDlg, IDC_PROGRESS_BUILD, PBM_SETRANGE32, 0, 100);

        SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"대상 폴더/파일을 드래그하여 올려놓거나 [찾아보기]를 선택하십시오.");
        return TRUE;
    }

    case WM_DROPFILES: {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        wchar_t droppedPath[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, droppedPath, MAX_PATH)) {
            SetDlgItemTextW(hDlg, IDC_EDIT_INPUT, droppedPath);
            onInputPathChanged(hDlg, droppedPath);
        }
        DragFinish(hDrop);
        return TRUE;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD notify = HIWORD(wParam);

        if (id == IDC_EDIT_INPUT && notify == EN_KILLFOCUS) {
            wchar_t path[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_INPUT, path, MAX_PATH);
            onInputPathChanged(hDlg, path);
            return TRUE;
        }

        if (id == IDC_BTN_BROWSE_INPUT) {
            // 폴더 또는 단일 파일 선택을 지원하는 팝업 메뉴
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, 1, L"프로그램 폴더 선택 (&Folder)...");
            AppendMenuW(hMenu, MF_STRING, 2, L"단일 실행 파일 선택 (&Executable)...");
            int sel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, nullptr);
            DestroyMenu(hMenu);

            if (sel == 1) {
                BROWSEINFOW bi = { 0 };
                bi.hwndOwner = hDlg;
                bi.lpszTitle = L"대상 프로그램 폴더를 선택하십시오:";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
                if (pidl) {
                    wchar_t path[MAX_PATH];
                    if (SHGetPathFromIDListW(pidl, path)) {
                        SetDlgItemTextW(hDlg, IDC_EDIT_INPUT, path);
                        onInputPathChanged(hDlg, path);
                    }
                    CoTaskMemFree(pidl);
                }
            } else if (sel == 2) {
                wchar_t filePath[MAX_PATH] = { 0 };
                OPENFILENAMEW ofn = { sizeof(ofn) };
                ofn.hwndOwner = hDlg;
                ofn.lpstrFilter = L"실행 파일 (*.exe)\0*.exe\0모든 파일 (*.*)\0*.*\0";
                ofn.lpstrFile = filePath;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                if (GetOpenFileNameW(&ofn)) {
                    SetDlgItemTextW(hDlg, IDC_EDIT_INPUT, filePath);
                    onInputPathChanged(hDlg, filePath);
                }
            }
            return TRUE;
        }

        if (id == IDC_BTN_BROWSE_OUTPUT) {
            wchar_t savePath[MAX_PATH] = { 0 };
            GetDlgItemTextW(hDlg, IDC_EDIT_OUTPUT, savePath, MAX_PATH);

            OPENFILENAMEW ofn = { sizeof(ofn) };
            ofn.hwndOwner = hDlg;
            ofn.lpstrFilter = L"설치 실행 파일 (*.exe)\0*.exe\0";
            ofn.lpstrFile = savePath;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = L"exe";
            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (GetSaveFileNameW(&ofn)) {
                SetDlgItemTextW(hDlg, IDC_EDIT_OUTPUT, savePath);
            }
            return TRUE;
        }

        if (id == IDC_BTN_BUILD) {
            wchar_t inPath[MAX_PATH], outPath[MAX_PATH], appName[MAX_PATH], appVer[MAX_PATH], pub[MAX_PATH], mainExe[MAX_PATH];
            GetDlgItemTextW(hDlg, IDC_EDIT_INPUT, inPath, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_EDIT_OUTPUT, outPath, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_EDIT_APP_NAME, appName, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_EDIT_APP_VERSION, appVer, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_EDIT_PUBLISHER, pub, MAX_PATH);
            GetDlgItemTextW(hDlg, IDC_EDIT_MAIN_EXE, mainExe, MAX_PATH);

            if (wcslen(inPath) == 0 || !PathFileExistsW(inPath)) {
                MessageBoxW(hDlg, L"유효한 대상 파일 또는 폴더 경로를 지정하십시오.", L"입력 확인", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_INPUT));
                return TRUE;
            }

            if (wcslen(outPath) == 0) {
                MessageBoxW(hDlg, L"출력 파일 경로를 지정하십시오.", L"입력 확인", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(hDlg, IDC_EDIT_OUTPUT));
                return TRUE;
            }

            // 버튼 비활성화
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_BUILD), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE_INPUT), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE_OUTPUT), FALSE);

            auto* ctx = new GuiBuildAsyncContext();
            ctx->hDlg = hDlg;
            ctx->config.inputPath = inPath;
            ctx->config.outputPath = outPath;
            ctx->config.appName = appName;
            ctx->config.appVersion = appVer;
            ctx->config.publisher = pub;
            ctx->config.mainExeName = mainExe;

            int scopeIdx = static_cast<int>(SendDlgItemMessageW(hDlg, IDC_COMBO_SCOPE, CB_GETCURSEL, 0, 0));
            ctx->config.defaultScope = (scopeIdx == 1) ? EasyInstallScope::PerUser : EasyInstallScope::PerMachine;
            ctx->config.createDesktopShortcut = (IsDlgButtonChecked(hDlg, IDC_CHK_DESKTOP) == BST_CHECKED);
            ctx->config.createStartMenuShortcut = (IsDlgButtonChecked(hDlg, IDC_CHK_STARTMENU) == BST_CHECKED);

            std::thread(runBuildWorker, ctx).detach();
            return TRUE;
        }

        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    case WM_BUILD_PROGRESS: {
        int percent = static_cast<int>(wParam);
        wchar_t* pStatus = reinterpret_cast<wchar_t*>(lParam);
        SendDlgItemMessageW(hDlg, IDC_PROGRESS_BUILD, PBM_SETPOS, percent, 0);
        if (pStatus) {
            SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, pStatus);
            free(pStatus);
        }
        return TRUE;
    }

    case WM_BUILD_COMPLETE: {
        auto* ctx = reinterpret_cast<GuiBuildAsyncContext*>(lParam);
        if (!ctx) return TRUE;

        EnableWindow(GetDlgItem(hDlg, IDC_BTN_BUILD), TRUE);
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE_INPUT), TRUE);
        EnableWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE_OUTPUT), TRUE);

        if (ctx->result.success) {
            SendDlgItemMessageW(hDlg, IDC_PROGRESS_BUILD, PBM_SETPOS, 100, 0);
            SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"설치 파일 생성이 성공적으로 완료되었습니다!");

            std::wstring msg = L"설치 프로그램(Setup.exe)이 성공적으로 생성되었습니다!\n\n출력 파일: " + ctx->config.outputPath + L"\n\n결과 파일이 있는 폴더를 여시겠습니까?";
            if (MessageBoxW(hDlg, msg.c_str(), L"생성 완료", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                std::wstring selectArg = L"/select,\"" + ctx->config.outputPath + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", selectArg.c_str(), nullptr, SW_SHOW);
            }
        } else {
            SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, (L"오류: " + ctx->result.errorMessage).c_str());
            MessageBoxW(hDlg, (L"빌드 실패: " + ctx->result.errorMessage).c_str(), L"오류", MB_OK | MB_ICONERROR);
        }

        delete ctx;
        return TRUE;
    }
    }
    return FALSE;
}

INT_PTR GuiBuilder::showDialog(HINSTANCE hInstance) {
    return DialogBoxParamW(hInstance, MAKEINTRESOURCEW(IDD_BUILDER_DIALOG), nullptr, BuilderDlgProc, 0);
}
