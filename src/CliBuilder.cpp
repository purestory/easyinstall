#include "CliBuilder.h"
#include "Builder.h"
#include <shlwapi.h>
#include <vector>

#pragma comment(lib, "shlwapi.lib")

static void printConsole(const std::wstring& msg) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hOut || hOut == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0;
    DWORD written = 0;
    if (GetConsoleMode(hOut, &mode)) {
        WriteConsoleW(hOut, msg.c_str(), static_cast<DWORD>(msg.size()), &written, nullptr);
    } else {
        int len = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), static_cast<int>(msg.size()), nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::string utf8(len, '\0');
            WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), static_cast<int>(msg.size()), &utf8[0], len, nullptr, nullptr);
            WriteFile(hOut, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }
}

static void printHelp() {
    printConsole(L"\nEasyInstall - 설치 프로그램(Setup.exe) 자동 생성기\n");
    printConsole(L"사용법: easyinstall.exe [옵션]\n\n");
    printConsole(L"옵션 목록:\n");
    printConsole(L"  -i, --input <경로>           대상 원본 파일 또는 디렉터리 경로 (필수)\n");
    printConsole(L"  -o, --output <경로>          생성될 Setup.exe 파일 경로 (필수/생략 시 자동 생성)\n");
    printConsole(L"  -n, --name <이름>            프로그램 이름 (생략 시 입력 경로의 파일/폴더명)\n");
    printConsole(L"  -v, --version <버전>         프로그램 버전 (기본값: 1.0.0)\n");
    printConsole(L"  -p, --publisher <게시자>     제작자 또는 회사명 (기본값: Unknown)\n");
    printConsole(L"  -m, --main-exe <파일명>      메인 실행 파일명 (생략 시 폴더 내 exe 자동 검색)\n");
    printConsole(L"      --scope <machine|user>   기본 설치 위치 (machine: Program Files, user: LocalAppData)\n");
    printConsole(L"      --desktop-shortcut <1|0> 바탕화면 바로가기 기본 체크 (기본값: 1)\n");
    printConsole(L"      --start-menu <1|0>       시작 메뉴 바로가기 기본 체크 (기본값: 1)\n");
    printConsole(L"      --launch <1|0>           완료 후 즉시 실행 기본 체크 (기본값: 1)\n");
    printConsole(L"  -h, --help                   도움말 출력\n\n");
    printConsole(L"사용 예시:\n");
    printConsole(L"  easyinstall.exe -i \"D:\\AppFolder\" -o \"D:\\Dist\\App_Setup.exe\" -n \"MyApp\" -v \"1.0.0\"\n");
    printConsole(L"  easyinstall.exe --input \"D:\\Tool.exe\" --output \"D:\\Tool_Setup.exe\"\n\n");
}

int CliBuilder::run(int argc, wchar_t* argv[]) {
    if (argc <= 1) {
        return -1;
    }

    std::wstring firstArg = argv[1];
    if (firstArg == L"--uninstall" || firstArg == L"--uninstall-worker") {
        return -1;
    }

    BuilderConfig config;
    bool hasInput = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];

        if (arg == L"-h" || arg == L"--help" || arg == L"/?") {
            printHelp();
            return 0;
        } else if ((arg == L"-i" || arg == L"--input") && i + 1 < argc) {
            config.inputPath = argv[++i];
            hasInput = true;
        } else if ((arg == L"-o" || arg == L"--output") && i + 1 < argc) {
            config.outputPath = argv[++i];
        } else if ((arg == L"-n" || arg == L"--name") && i + 1 < argc) {
            config.appName = argv[++i];
        } else if ((arg == L"-v" || arg == L"--version") && i + 1 < argc) {
            config.appVersion = argv[++i];
        } else if ((arg == L"-p" || arg == L"--publisher") && i + 1 < argc) {
            config.publisher = argv[++i];
        } else if ((arg == L"-m" || arg == L"--main-exe") && i + 1 < argc) {
            config.mainExeName = argv[++i];
        } else if (arg == L"--scope" && i + 1 < argc) {
            std::wstring sc = argv[++i];
            if (sc == L"user") config.defaultScope = EasyInstallScope::PerUser;
            else config.defaultScope = EasyInstallScope::PerMachine;
        } else if (arg == L"--desktop-shortcut" && i + 1 < argc) {
            config.createDesktopShortcut = (_wtoi(argv[++i]) != 0);
        } else if (arg == L"--start-menu" && i + 1 < argc) {
            config.createStartMenuShortcut = (_wtoi(argv[++i]) != 0);
        } else if (arg == L"--launch" && i + 1 < argc) {
            config.launchAfterInstall = (_wtoi(argv[++i]) != 0);
        } else if (arg == L"build") {
            continue;
        }
    }

    if (!hasInput) {
        printConsole(L"[EasyInstall 오류] 대상 입력 경로(-i, --input)가 지정되지 않았습니다. --help를 참고하십시오.\n");
        return 1;
    }

    if (config.outputPath.empty()) {
        std::wstring baseName = config.appName.empty() ? PathFindFileNameW(config.inputPath.c_str()) : config.appName;
        wchar_t fullIn[MAX_PATH];
        GetFullPathNameW(config.inputPath.c_str(), MAX_PATH, fullIn, nullptr);
        PathRemoveFileSpecW(fullIn);
        config.outputPath = std::wstring(fullIn) + L"\\" + baseName + L"_Setup.exe";
    }

    printConsole(L"====================================================\n");
    printConsole(L"  EasyInstall 설치 파일 패키징 빌더\n");
    printConsole(L"====================================================\n");
    printConsole(L"대상 입력 : " + config.inputPath + L"\n");
    printConsole(L"출력 파일 : " + config.outputPath + L"\n");
    printConsole(L"프로그램명: " + (config.appName.empty() ? L"(자동)" : config.appName) + L"\n");
    printConsole(L"버    전  : " + config.appVersion + L"\n\n");

    BuildResult res = Builder::build(config, [](int percent, const std::wstring& status) {
        printConsole(L"[" + std::to_wstring(percent) + L"%] " + status + L"\n");
    });

    if (res.success) {
        printConsole(L"\n[성공] 설치 프로그램이 성공적으로 생성되었습니다!\n");
        printConsole(L"  - 총 압축 파일 수: " + std::to_wstring(res.totalFiles) + L" 개\n");
        printConsole(L"  - 원본 총 용량   : " + std::to_wstring(res.uncompressedBytes) + L" 바이트\n");
        printConsole(L"  - 생성 파일 크기 : " + std::to_wstring(res.outputBytes) + L" 바이트\n");
        printConsole(L"  - 결과 파일 위치 : " + config.outputPath + L"\n");
        return 0;
    } else {
        printConsole(L"\n[실패] 오류: " + res.errorMessage + L"\n");
        return 1;
    }
}
