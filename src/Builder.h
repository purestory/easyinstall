#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "PackageMeta.h"

struct BuilderConfig {
    std::wstring inputPath;             // 원본 파일 또는 디렉터리 경로
    std::wstring mainExeName;           // 메인 실행 파일명 (비어있으면 자동 감지)
    std::wstring appName;               // 프로그램 이름
    std::wstring appVersion;            // 버전
    std::wstring publisher;             // 게시자
    std::wstring outputPath;            // 출력 Setup.exe 경로
    EasyInstallScope defaultScope;      // 기본 설치 위치 (PerMachine / PerUser)
    bool createDesktopShortcut;         // 바탕화면 바로가기 기본 체크
    bool createStartMenuShortcut;       // 시작메뉴 등록 기본 체크
    bool launchAfterInstall;            // 완료 후 실행 기본 체크

    BuilderConfig()
        : defaultScope(EasyInstallScope::PerMachine),
          createDesktopShortcut(true),
          createStartMenuShortcut(true),
          launchAfterInstall(true) {}
};

struct BuildResult {
    bool success;
    std::wstring errorMessage;
    uint32_t totalFiles;
    uint64_t uncompressedBytes;
    uint64_t outputBytes;

    BuildResult()
        : success(false), totalFiles(0), uncompressedBytes(0), outputBytes(0) {}
};

class Builder {
public:
    using ProgressCallback = std::function<void(int percent, const std::wstring& status)>;

    // 빌드 실행
    static BuildResult build(const BuilderConfig& config, ProgressCallback callback = nullptr);

    // 디렉터리 내 exe 파일 목록 검색 헬퍼 (GUI 콤보박스 채우기용)
    static std::vector<std::wstring> findExecutableFiles(const std::wstring& dirPath);
};
