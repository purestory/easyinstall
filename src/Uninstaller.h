#pragma once
#include <windows.h>
#include <string>
#include "PackageMeta.h"

class Uninstaller {
public:
    // 언인스톨러 진입점
    static int run(HINSTANCE hInstance, const std::wstring& cmdLine);
};
