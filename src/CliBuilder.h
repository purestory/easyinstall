#pragma once
#include <windows.h>
#include <string>

class CliBuilder {
public:
    // CLI 명령행 인자 파싱 및 빌드 실행
    // 반환값: 0 (성공), 1 (실패), -1 (CLI 인자 없음 -> GUI 모드로 진행해야 함)
    static int run(int argc, wchar_t* argv[]);
};
