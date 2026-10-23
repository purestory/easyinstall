#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

// 설치 대상 범위
enum class EasyInstallScope : unsigned int {
    PerMachine = 0, // C:\Program Files\<AppName> (관리자 권한 UAC 필요)
    PerUser = 1     // %LocalAppData%\Programs\<AppName> (일반 사용자 권한)
};

// 푸터 시그니처 ("INSTEASY")
constexpr unsigned long long EASYINSTALL_MAGIC = 0x5953414554534E49ULL;

#pragma pack(push, 1)
struct EasyInstallFooter {
    unsigned long long magic;       // EASYINSTALL_MAGIC
    unsigned long long zipOffset;   // 파일 시작점 기준 ZIP 데이터 오프셋
    unsigned long long zipSize;     // ZIP 데이터 크기
    unsigned long long metaOffset;  // 파일 시작점 기준 메타데이터 오프셋
    unsigned int metaSize;          // 메타데이터 크기
    unsigned int reserved;          // 예약 (0)
};
#pragma pack(pop)

// 패키지 메타데이터 구조체
struct PackageMeta {
    std::wstring appName;               // 프로그램 이름
    std::wstring appVersion;            // 버전
    std::wstring publisher;             // 게시자
    std::wstring mainExeName;           // 메인 실행 파일명
    std::wstring defaultInstallDir;     // 기본 설치 폴더명
    EasyInstallScope defaultScope;      // 기본 설치 범위
    bool createDesktopShortcut;         // 바탕화면 바로가기 기본 체크
    bool createStartMenuShortcut;       // 시작메뉴 등록 기본 체크
    bool launchAfterInstall;            // 설치 후 즉시 실행 기본 체크
    unsigned long long uncompressedSize;// 전체 압축 해제 용량 (바이트)

    PackageMeta()
        : defaultScope(EasyInstallScope::PerMachine),
          createDesktopShortcut(true),
          createStartMenuShortcut(true),
          launchAfterInstall(true),
          uncompressedSize(0) {}

    // 직렬화
    std::vector<unsigned char> serialize() const;
    // 역직렬화
    static bool deserialize(const unsigned char* data, size_t size, PackageMeta& outMeta);
};
