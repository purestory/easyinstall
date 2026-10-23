#pragma once
#include <windows.h>
#include <string>
#include "PackageMeta.h"

class InstallerWizard {
public:
    // 현재 실행 바이너리가 설치 패키지인지 확인하고 메타데이터를 추출
    static bool detectPayload(PackageMeta& outMeta, EasyInstallFooter& outFooter);

    // 설치 마법사 대화상자 실행
    static INT_PTR showWizard(HINSTANCE hInstance, HWND hParent, const PackageMeta& meta, const EasyInstallFooter& footer);
};
