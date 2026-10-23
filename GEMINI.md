# AI 에이전트 개발 행동 강령 및 실수 방지 지침 (Core Rules & Anti-Patterns)

**[경고] 본 문서는 단순 기능 설명서나 커밋 로그가 아닙니다.**  
AI 에이전트(당신)가 개발 중 **실제로 저질렀던 치명적인 실수들을 박제하고 영구히 재발을 방지**하기 위한 강령입니다.  
다른 프로젝트에서도 공통으로 적용되는 **공통 규칙 및 빌드 환경**이 최상단에 배치되어 있으므로, 새로운 작업을 진행하기 전 반드시 숙지하세요.

### 🚨 최우선 원칙 1: 코딩/개발 지시 수신 시 구현 전 계획(Plan) 사전 공유 필수 (선 계획, 후 구현)
- **철칙**: 사용자가 기능 추가, 버그 수정, 리팩토링 등 **코드베이스를 수정하는 코딩 작업을 지시할 때**, 임의로 코드를 곧바로 작성하거나 수정하는 구현 작업을 시작하기 전에 반드시 무엇을 어떻게 진행할 것인지 명확한 실행 계획(Plan)을 먼저 상세히 설명하고 진행할 것.
- **예외(실행/운영 명령 즉시 수행)**: 빌드, 테스트, 패키징, 릴리스 등록 등 **이미 동작과 목표가 명확한 실행/운영 명령(Operational Execution)**은 계획 확인 질문으로 사용자를 번거롭게 되묻지 말고 즉시 원스톱으로 실행할 것.
- **방지 목적**: 사전 계획 공유 없이 성급하게 코드를 수정하여 엉뚱한 방향으로 구현되거나 불필요한 시행착오와 번복이 발생하는 것을 원천 차단함.

### 🚨 최우선 원칙 2: 실수 발생 즉시 자발적 문서 박제 (사용자 요청 대기 절대 금지)
- **실제 안일한 태도**: 빌드 실패, UI 깨짐, 텍스트 짤림 등의 치명적인 실수를 저지르고 수정 코드를 반영했음에도, "수정 완료되었습니다"라고만 보고하고 사용자가 "GEMINI.md에 추가할 건 없냐?"고 직접 지적할 때까지 가만히 있는 수동적인 태도를 보임.
- **철칙**: 버그 수정이나 실패 수습이 완료되면, **사용자가 묻기 전에 무조건 작업 완료 단계에서 본 문서(GEMINI.md)에 '실제 실수'와 '방지 원칙'을 자동으로 기록**하고 그 사실을 함께 보고할 것. 사용자가 먼저 문서를 챙기게 만드는 것은 에이전트의 심각한 직무유기임.
- **지식 자동 업데이트**: 향후 개발을 진행하면서 특정 프로젝트에 국한되지 않는 **일반적인 Win32 C++ 개발, 성능 최적화, 혹은 UI/UX 개선 패턴**을 새롭게 발견하거나 구현하게 되면, 스스로 판단하여 '개발 규칙' 항목에 해당 내용을 자동으로 요약 및 추가할 것 (단, 너무 지엽적인 예외 사항은 억지로 추가하지 않음).

---

# 1. 모든 C++/Win32 프로젝트 공통 필수 규칙 (Global & Common Rules)

다른 프로젝트에서도 동일하게 준수해야 하는 최우선 공통 규칙들입니다.

### ⚙️ 빌드 및 CLI 명령어 규칙 (Build & Environment)
- **절대 경로 MSBuild 사용**: Windows 환경 변수(PATH)에 `msbuild`나 `cmake`가 등록되어 있지 않은 경우가 많으므로 터미널에서 직접 실행하지 말 것.  
  반드시 Visual Studio 2022 Community MSBuild 절대 경로를 사용:
  `& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" easyinstall.sln /p:Configuration=Release /p:Platform=x64`
- **인클루드 디렉터리 표준화**: 하위 폴더 소스들이 루트 헤더를 참조할 때 상대 경로 지옥(`../../`)이나 `C1083` 오류가 발생하지 않도록, `.vcxproj`의 `AdditionalIncludeDirectories`에 `$(ProjectDir);%(AdditionalIncludeDirectories)`를 필수로 등록할 것.
- **Git 명령어 임의 실행 절대 금지**: 사용자의 명시적인 지시가 없는 한 `git diff`, `git log`, `git checkout`, `git reset` 등 Git 명령어를 절대 임의로 실행하지 말 것.
- **바이너리 파일 잠금(`LNK1104`) 간과 및 미검증 완료 보고 절대 금지**:
  - **실제 실수**: 사용자가 테스트 중 프로그램을 켜둔 상태에서 빌드를 돌리면 `LINK : fatal error LNK1104: '...exe' 파일을 열 수 없습니다`로 링크가 실패함. 이 에러를 대충 넘기고 "수정 완료"라고 보고하여, 사용자가 버그가 전혀 고쳐지지 않은 구버전 바이너리를 보고 극도로 분노하게 만듦.
  - **방지 원칙**: 코드를 수정한 뒤에는 반드시 MSBuild 출력에서 **`빌드했습니다. 오류 0개, 경고 0개`와 `.exe` 생성 성공**을 100% 확인한 후에만 보고할 것. 실행 중인 프로세스로 인한 `LNK1104` 발생 시 프로세스 종료 후 재빌드를 완결할 것.
- **신규 설치 CLI 도구 환경 설정 사용자 떠넘기기 및 세션 PATH 미반영 금지**:
  - **실제 실수**: 도구 설치 후 에이전트가 직접 터미널 환경 변수를 갱신하거나 절대 경로로 후속 작업을 자동화할 수 있는 권한(`run_command`)이 있음에도 불구하고, 사용자에게 수동 작업을 떠넘기거나 부모 IDE 환경 변수 미반영으로 인한 오류를 유발함.
  - **방지 원칙**:
    1. 도구 설치 후 필요한 환경 변수 갱신, 바이너리 검증, 후속 작업은 **에이전트가 직접 실행(`run_command`)하여 사용자가 곧바로 쓸 수 있는 상태로 완결**해 둘 것.
    2. 사용자에게 터미널 명령어를 안내할 때는 IDE 재시작 전까지 환경 변수가 상속되지 않는 문제를 감안하여 **무조건 검증된 절대 경로**를 기본으로 제공할 것.
- **배포 산출물 중복 생성 및 패치 수정 시 버전(Version) 미갱신 금지**:
  - 배포 아카이브는 중복 파일 없이 **단일 표준 버전 명칭(예: `easyinstall_v1.0.0_Portable.zip`) 딱 1개만** 정갈하게 생성할 것.
  - 코드/리소스 수정 및 배포가 발생하면 반드시 **시맨틱 버저닝(SemVer) 규칙에 따라 버전 번호를 명확히 상향(`v1.0.0` ➡️ `v1.0.1`)**하여 릴리스할 것.

### 🛠️ 도구 사용 실수 방지: `replace_file_content` 줄 번호 포함 금지
- **실제 실수**: `view_file`로 확인한 내용 앞의 줄 번호(`136: `, `150: ` 등)를 복사해 `TargetContent`나 `ReplacementContent`에 전달했다가 `chunk 0: target content not found` 에러를 반복 유발함.
- **방지 원칙**: 파일 수정 도구를 호출할 때는 파일의 **순수한 실제 코드(공백, 들여쓰기 포함, 줄 번호 절대 제외)**만을 정확하게 지정할 것.

### 🖥️ UI 컨트롤 라이프사이클 4단계 및 너비·줄바꿈·동적 리사이징 원칙 ('반쪽짜리 구현' 금지)
- **동적 레이아웃 및 리사이징 (WM_SIZE 처리)**:
  - `.rc` 파일에 정의된 컨트롤 크기 및 위치는 픽셀이 아닌 **DLU(Dialog Units)** 기반이므로, 시스템 DPI나 시스템 폰트에 따라 렌더링되는 크기가 달라집니다.
  - 따라서 `WM_SIZE` 핸들러에서 하드코딩된 픽셀 값(예: `width - 90`)을 사용한 위치/크기 계산을 피해야 합니다.
  - 항상 `GetWindowRect`로 컨트롤의 실제 렌더링 크기(width/height)를 구하거나, `MapDialogRect`를 통해 DLU를 픽셀로 변환한 후, 부모 클라이언트 영역(`GetClientRect`)에 비례하여 동적으로 배치(우측/하단 정렬 등)하세요.
- **UI 구현 4단계 완결**: 리소스 선언(`.rc`) ➡️ 컨트롤 초기화(`WM_INITDIALOG`) ➡️ 조작/렌더링(`WM_COMMAND`, `WM_DRAWITEM` 등) ➡️ 반응형 재배치(`WM_SIZE`).
- **1줄 라벨 줄바꿈 방지**: 1줄 라벨에는 반드시 `SS_LEFTNOWORDWRAP`을 사용하고, 한글 기준 글자당 최소 14~15px 이상의 넉넉한 너비를 할당할 것.
- **체크박스/라디오 너비 계산**: `텍스트 글자 폭 + 체크마크(24px) + 안전 마진(20~30%)`을 무조건 합산하여 할당할 것.
- **최소 창 크기 방어**: 반응형 대화상자는 `WM_GETMINMAXINFO`를 필수로 구현하여 사용자가 창을 줄이더라도 컨트롤이 겹치거나 깨지지 않도록 최소 너비/높이를 제한할 것.

### 🎨 깜빡임 없는 UI 업데이트 (In-place Update & Notification Suppression)
- 리스트뷰(`ListView`) 등의 아이템 상태가 일부 변경되었을 때, 전체 리스트를 지우고 새로고침(`DeleteAllItems` 후 재로드)하지 말 것.
- 화면 깜빡임과 스크롤 초기화를 방지하기 위해 `ListView_SetItemText` 등을 사용하여 **변경된 특정 행(Row)만 부분 업데이트(In-place Update)** 해야 함.
- 대량의 아이템 갱신 시 불리언 가드(`g_bUpdating = true`)와 `WM_SETREDRAW`를 걸어 `LVN_ITEMCHANGED` 연쇄 이벤트 폭주 및 화면 깜빡임을 원천 차단할 것.

### 🖌️ Win32 Dialog 내 Custom Draw 처리 (`DWLP_MSGRESULT`)
- DialogBox 기반의 윈도우(DialogProc)에서 `WM_NOTIFY` 메시지의 `NM_CUSTOMDRAW`를 처리할 때, 직접 return 값을 반환하면 시스템이 인식하지 못하고 무시하는 경우가 발생함.
- 반드시 `SetWindowLongPtr(hWnd, DWLP_MSGRESULT, 반환값)`을 호출하여 결과를 명시한 후, `return (INT_PTR)TRUE;`를 반환해야 정상적으로 렌더링을 가로채거나(`CDRF_NOTIFYPOSTPAINT`) 넘길 수 있음.
- 기존 컨트롤의 아이콘이나 체크박스 등을 보존하면서 텍스트만 다르게 그리고 싶다면, `CDRF_SKIPDEFAULT` 대신 `CDRF_NOTIFYPOSTPAINT`를 사용하여 기본 렌더링이 끝난 후 `CDDS_ITEMPOSTPAINT` 단계에서 덮어쓰는 방식을 사용해야 함.

### 🌲 TreeView 폴더 확장 시 Auto-Scroll(화면 튐) 방지
- `SysTreeView32` 컨트롤에서 사용자가 폴더(+)를 확장하여 다수의 자식 노드를 삽입(`TVN_ITEMEXPANDING`)할 때, 컨트롤이 자식들을 화면에 욱여넣으려고 자동 스크롤하면서 부모 노드가 화면 최상단으로 튕겨 올라가는 불쾌한 UX가 발생함.
- 이를 방지하기 위해 노드 삽입 직전 `GetScrollPos(hTree, SB_VERT)`로 스크롤 위치를 기억하고, `PostMessage` 등을 이용해 비동기로(즉시) 해당 스크롤 위치를 복구(`WM_VSCROLL`)하여 화면이 그 상태 그대로 유지되도록 처리해야 함.

### 🖥️ 고DPI 및 다중 모니터 환경에서의 폰트/창 비트맵 스트레칭 방지 (Per-Monitor V2 DPI & MulDiv 스케일링)
- 구형 API인 `SetProcessDPIAware()`는 프로세스를 단일 시스템 DPI에 고정시킴. 이로 인해 다중 모니터(배율 불일치), 절전 모드(Sleep) 복귀, 화면 해상도/배율 변경 시 Windows DWM이 창을 강제로 비트맵 확대/축소(Bitmap Stretching)하여 폰트가 저해상도 이미지처럼 뭉개지고 블러(Blur) 처리되는 현상이 간헐적으로 발생함.
- 반드시 런타임에 `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`를 최우선 적용(실패 시 `SetProcessDpiAwareness` -> `SetProcessDPIAware` Fallback)하여 DWM 가상화 확대를 원천 차단해야 함.
- 또한 모든 폰트 높이(`CreateFontW`) 및 윈도우 배치 좌표/패딩은 `-MulDiv(기본픽셀, dpi, 96)`로 현재 모니터의 실제 DPI(`GetDpiForWindow`)에 비례하여 동적으로 계산하고, `WM_DPICHANGED`와 `WM_SETTINGCHANGE` / `WM_THEMECHANGED` 메시지를 핸들링하여 배율 및 테마 변화 시 레이아웃과 폰트를 즉시 갱신해야 함.

### 🔤 C++ 트레일링 역슬래시(`\`) 줄바꿈(Line-Splicing) 버그 방지
- C/C++ 소스 및 헤더의 한 줄 주석(`//`) 끝에는 절대로 단독 역슬래시(`\`)를 두지 말고, `\\` 또는 `(예: C:\\)` 등으로 처리할 것. 전처리기가 다음 줄을 주석으로 삼키는 참사 방지.

### ⌨️ 사용자 입력 시나리오 검증 및 키보드 편의성 보장
- 표준 Edit 컨트롤을 사용하고 유연한 파서를 작성할 것. 마우스 클릭 또는 포커스 진입 시 `EM_SETSEL(0, -1)`을 보내 즉시 전체 선택 수정 모드를 제공하고, Enter 키 입력 시 대화상자가 닫히지 않도록 서브클래싱(`DLGC_WANTALLKEYS`)할 것.

### 🔒 멀티스레드 동기화 뮤텍스(Lock) 내부 동기식 디스크 I/O 배제
- 워커 스레드 동기화 임계 영역(Mutex Lock) 내부에서는 디스크/네트워크 시스템 콜을 일절 배제하고 **순수 RAM 메모리 해시셋/맵 조회 및 삽입(0.0001ms)**만 수행할 것. 이미 확인된 경로는 RAM 해시셋에 캐싱하여 중복 디스크 I/O를 0ms로 통과시킬 것.

### 🧵 백그라운드 워커 스레드의 COM 다중 스레드 아파트먼트 초기화
- Shell Property Store, WIC, IShellItem, BITS, WinINet 등을 워커 스레드에서 사용할 때는 시작 시 `CoInitializeEx(NULL, COINIT_MULTITHREADED)`를, 종료 시 `CoUninitialize()`를 반드시 쌍으로 호출할 것.

### 🗑️ 데이터 유실 방지를 위한 파일 안전 삭제 원칙
- 일반 임시 파일 정리 시 영구 삭제보다는 가능하면 `SHFileOperationW` + `FOF_ALLOWUNDO`로 휴지통 복구 여지를 남길 것.

### 📜 PowerShell 텍스트 파일 조작 시 UTF-8 인코딩 강제
- Windows PowerShell에서 `Add-Content`나 `Out-File` 등의 명령어로 텍스트 파일에 내용을 덧붙이거나 작성할 때, 기본 인코딩(ANSI 등)이 적용되어 기존 UTF-8(특히 한글)이 깨지는 현상이 발생함.
- 따라서 파일 I/O를 수행하는 셸 스크립트를 작성할 때는 반드시 `-Encoding UTF8` 파라미터를 명시하여 인코딩 깨짐을 원천 차단할 것.

### 📝 C++ 파일/스크립트 I/O 시 std::wofstream 사용 절대 금지 (유니코드 변환 중단 참사)
- **실제 실수**: `std::wofstream`을 기본 로케일 상태에서 사용하여 파일 작성 중 한글 주석이 들어가는 순간 유니코드 변환 에러(`failbit`)가 발생하여 파일 출력이 중단되는 참사가 일어남.
- **방지 원칙**: 스크립트/배치 파일이나 설정 파일 생성 시 `std::wofstream`을 절대 쓰지 말 것. 반드시 명시적인 UTF-8 변환(`WideCharToMultiByte(CP_UTF8, ...)` 또는 `toUtf8` 람다)을 거쳐 `std::ofstream` + 바이너리(`ios::binary`)로 출력하거나, 배치 파일 주석 및 명령어는 100% 영문 ASCII로만 작성하여 인코딩 오류 가능성을 0%로 차단할 것.

---

# 2. easyinstall 프로젝트 특화 실수 및 개발 규칙 (Project-Specific Traps)

본 프로젝트 개발을 진행하며 **실제로 겪은 시행착오, 버그, 프로젝트 고유의 도메인 특화 규칙**을 영구 재발 방지를 위해 기록하는 공간입니다.  
*(※ 개발 진행 중 실수가 발생하거나 새로운 패턴을 정립할 때마다 최우선 원칙 2에 따라 자발적으로 즉시 추가할 것)*

### ⚠️ Windows SDK 식별자 충돌 방지 (`InstallScope` 등 전역 네임스페이스 오염)
- **실제 실수**: 설치 대상 범위를 정의하기 위해 `enum class InstallScope`를 선언했다가, Windows 10/11 SDK의 `AppModel.h` 등에 이미 정의된 Windows 내부 `enum InstallScope`와 충돌하여 헤더 파서가 완전히 깨지면서 200여 개의 연쇄 컴파일 에러가 발생함.
- **방지 원칙**: `<windows.h>` 및 Shell 헤더를 포함하는 C++ 프로젝트에서는 Windows SDK에 존재할 가능성이 있는 일반적인 단어(`InstallScope`, `Package`, `Storage`, `Environment` 등)를 네임스페이스 없이 단독 선언하지 말 것. 반드시 프로젝트 고유의 접두사(`EasyInstallScope`)나 네임스페이스(`namespace EasyInstall`)를 감싸서 선언할 것.

### 📁 `SHCreateDirectoryExW` 상대 경로 호출 실패 참사 방지 (절대 경로 정규화 필수)
- **실제 실수**: CLI 또는 GUI에서 사용자가 입력한 상대 경로(`test_output\Setup.exe`)를 그대로 `PathRemoveFileSpecW` 후 `SHCreateDirectoryExW`에 넘겼다가, Windows Shell API가 상대 경로 디렉터리 생성을 지원하지 않아 `ERROR_PATH_NOT_FOUND`로 설치 파일 생성이 즉시 실패함.
- **방지 원칙**: 파일/폴더 I/O나 디렉터리 생성 전에는 반드시 `GetFullPathNameW`를 호출하여 **정규화된 절대 경로(Canonical Full Path)**로 사전 변환한 후 Shell API 및 `CreateFileW`에 전달할 것.

### 🖥️ CLI/GUI 하이브리드(듀얼 모드) 바이너리 아키텍처 및 무결점 콘솔 출력
- **실제 실수**: 프로그램을 `SUBSYSTEM:WINDOWS`로 컴파일한 상태에서 `AttachConsole(ATTACH_PARENT_PROCESS)`를 시도했으나, PowerShell/CMD에서 비동기로 간주되어 프로세스가 기다리지 않고 즉시 반환되며 표준 출력 파이프가 끊겨 CLI 로그가 터미널에 전혀 찍히지 않는 현상 발생. 또한 `std::wcout`은 CRT 로케일/버퍼링 문제로 한글 출력 시 문자열이 중간에 잘려버림.
- **방지 원칙**:
  1. CLI 자동화와 GUI를 동시에 지원해야 하는 도구는 **`SUBSYSTEM:CONSOLE`(`wmain`)으로 컴파일**할 것.
  2. 사용자가 더블클릭하여 인자 없이 실행하거나 마법사 창을 띄울 때는 시작 즉시 `HWND hCon = GetConsoleWindow(); ShowWindow(hCon, SW_HIDE); FreeConsole();`를 호출하여 콘솔 창을 눈 깜짝할 사이에 숨기고 순수 GUI 모드로 진입할 것.
  3. CLI 콘솔 출력은 불안정한 CRT 스트림 대신 **`WriteConsoleW`(터미널 직접 출력)**와 **UTF-8 `WriteFile`(파이프 리다이렉트)**을 이중 지원하는 전용 헬퍼를 사용하여 어떤 셸 환경에서도 100% 한글 깨짐 없이 출력되도록 보장할 것.

### 🪟 마법사 다이얼로그 페이지 전환 시 `IDC_STATIC`(-1) 은닉 누락 및 텍스트 겹침(간섭) 방지
- **실제 실수**: 다이얼로그 리소스(`.rc`)에서 그룹박스(`GROUPBOX`)나 상태 라벨(`LTEXT`)을 기본 `IDC_STATIC`(`-1`)으로 방치한 채, 코드에서 `ShowWindow`를 호출하지 않거나 `-1`로 호출하여 1단계, 2단계, 3단계 컨트롤들이 다음 페이지로 넘어가도 숨겨지지 않고 화면에 그대로 남아 글자가 서로 겹치고 간섭하는 현상이 발생함.
- **방지 원칙**:
  1. 마법사 형태의 단계별 페이지 전환 다이얼로그에서는 화면에 동적으로 나타나고 사라져야 하는 **모든 컨트롤(Static Text, GroupBox 포함)에 고유한 Resource ID(`IDC_GRP_OPTIONS`, `IDC_STATIC_INSTALLING_MSG` 등)를 100% 부여**할 것.
  2. 페이지 전환 함수(`updateWizardPage`)에서 해당 페이지에 속하지 않는 모든 컨트롤을 빈틈없이 `SW_HIDE` 시키고, 전환 직후 `InvalidateRect(hDlg, NULL, TRUE); UpdateWindow(hDlg);`를 명시적으로 호출하여 이전 단계의 잔상을 완벽히 지워낼 것.

### 📁 Windows 10/11 시작 메뉴 폴더 축약(Flattening) 방지 및 프로그램 그룹 보존
- **실제 실수**: 시작 메뉴 프로그램 디렉터리(`CSIDL_PROGRAMS` / `CSIDL_COMMON_PROGRAMS`)에 앱 이름 폴더(`\Programs\AppName`)를 생성하고 바로가기를 1개만 넣었더니, Windows 10/11의 시작 메뉴 셸이 단일 링크 폴더를 자동으로 평탄화(Flattening)하여 폴더는 사라지고 최상위에 링크만 달랑 표시되는 현상이 발생함.
- **방지 원칙**: Windows 시작 메뉴에 정식 폴더 그룹으로 노출되도록 보장하려면, 해당 폴더 내에 메인 프로그램 바로가기(`[AppName].lnk`)뿐만 아니라 표준 제거 프로그램 바로가기(`[AppName] 제거.lnk`) 등 2개 이상의 의미 있는 항목을 함께 생성할 것. 또한 바로가기 생성/삭제 직후 `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL)`를 호출하여 셸 캐시를 즉시 갱신할 것.

### 🗑️ `CommandLineToArgvW` 인자 오프셋 착오 및 언인스톨러 디렉터리 삭제 실패 참사 방지
- **실제 실수**: 임시 워커 프로세스(`--uninstall-worker`)를 띄워 삭제를 진행할 때, `CommandLineToArgvW`의 반환값 중 `argv[0]`(실행 파일 경로)을 간과하고 `argv[1]`이 대상 설치 디렉터리인 것으로 착각하여 `argv[1]`(`--uninstall-worker`)을 삭제 경로로 전달함. 이로 인해 실제 설치 폴더는 전혀 삭제되지 않는 치명적인 버그가 발생함. 또한 `uninstall.exe`가 실행 중인 상태에서 부모 프로세스 핸들 해제를 기다리지 않아 파일 잠금으로 삭제가 거부됨.
- **방지 원칙**:
  1. `CommandLineToArgvW`는 첫 번째 요소(`argv[0]`)가 항상 프로세스 실행 파일 자체의 경로이므로, 플래그를 찾을 때는 인덱스를 하드코딩하지 말고 `argv` 배열 전체를 루프 탐색하여 플래그 위치(`workerIdx`) 기준으로 상대 오프셋(`workerIdx + 1`, `workerIdx + 2`...)을 취할 것.
  2. 언인스톨러 임시 워커는 부모 프로세스의 PID를 인자로 받아 `OpenProcess(SYNCHRONIZE, ...)` -> `WaitForSingleObject`로 원본 프로세스가 완전히 종료될 때까지 대기한 후 파일 삭제를 시작할 것.
  3. Windows의 일시적인 파일 잠금을 극복하기 위해 `safeDeleteFile` / `safeRemoveDirectory` 재시도 루프(최소 10회, 150ms)를 적용하고, `PerMachine` 설치본의 경우 워커를 UAC 권한 승격(`runas`)으로 실행하여 권한 부족으로 인한 삭제 실패를 차단할 것.

### 🛡️ 설치 마법사 UAC 관리자 권한 미승격 및 압축 해제 루틴의 침묵형 가짜 성공(Fake Success) 방지
- **실제 실수**: 기본 설치 범위가 `PerMachine`(모든 사용자)으로 설정된 패키지가 `C:\Program Files` 및 `HKLM`에 파일을 설치해야 함에도, 매니페스트가 `asInvoker`로 되어 있어 더블클릭 시 UAC 승격 팝업 없이 일반 권한으로 실행됨. 이로 인해 `SHCreateDirectoryExW` 및 `CreateFileW`가 `ERROR_ACCESS_DENIED`(5)로 모조리 실패했음에도, 워커 스레드에서 반환값을 검사하지 않고 무시한 채 `installSuccess = true`로 처리하여 "설치가 성공적으로 완료되었습니다!"라는 가짜 성공 화면을 사용자에게 노출함.
- **방지 원칙**:
  1. `isSetupBinary` 실행 시 패키지 스코프가 `PerMachine`이면 현재 토큰이 승격 상태인지(`isProcessElevated()`) 반드시 확인하고, 미승격 상태라면 `ShellExecuteExW`(`lpVerb = L"runas"`)를 통해 Windows UAC 권한 승격을 강제할 것.
  2. 설치 워커 루틴에서는 디렉터리 생성(`SHCreateDirectoryExW`), 압축 해제(`mz_zip_reader_extract_to_heap`), 파일 쓰기(`CreateFileW`, `WriteFile`)의 반환값을 100% 엄격히 검사하여 실패 즉시 중단하고 상세 에러 메시지(오류 코드, 경로 등)를 수집하여 실패 화면으로 유도할 것 (가짜 성공 절대 금지).
  3. 바로가기 등록 시 `CSIDL_COMMON_*` 실패에 대비하여 `CSIDL_*`(사용자 전용)으로의 안전한 폴백을 구축할 것.

### 🔄 버전업(재설치) 시 관리자 권한 프로세스 파일 잠금(`ERROR_SHARING_VIOLATION`) 방지 및 자동 종료 시퀀스
- **실제 실수 및 잠재 함정**: 구버전 프로그램이 관리자 권한(Elevated)으로 백그라운드나 포그라운드에서 실행 중인 상태에서 사용자가 새 버전 인스톨러로 덮어쓰기 설치를 시도하면, 실행 파일(`.exe`, `.dll`)에 OS 파일 잠금(File Lock)이 걸려 `CreateFileW(..., CREATE_ALWAYS)` 시 `ERROR_SHARING_VIOLATION`(32) 또는 `ERROR_ACCESS_DENIED`(5)로 설치가 실패함. 또한 일반 권한으로는 관리자 권한 프로세스에 `PROCESS_TERMINATE` 핸들을 열지 못함.
- **방지 원칙**:
  1. `SeDebugPrivilege` 활성화: 인스톨러 토큰에 `AdjustTokenPrivileges(SE_DEBUG_NAME)`을 선행 활성화하여 관리자 권한 프로세스라도 안전하게 제어/종료할 수 있는 시스템 디버그 권한을 확보할 것.
  2. 대상 설치 디렉터리 기반 정밀 타겟팅: 단순 파일명 대조에 그치지 않고 `QueryFullProcessImageNameW`를 통해 대상 설치 폴더(`installPath`)에 속한 프로세스인지 100% 대조하여 무관한 동명 프로세스 오종료를 원천 차단할 것.
  3. 2단계 종료 시퀀스: 사용자에게 종료 동의를 구한 뒤, 먼저 `WM_CLOSE` 정상 종료(Graceful close)를 시도하고 타임아웃(1.5초) 초과 시 `TerminateProcess`로 강제 종료한 후 `WaitForSingleObject`로 프로세스 핸들이 완전히 닫힐 때까지 대기하여 파일 잠금이 해제된 후 파일 쓰기를 시작할 것.
  4. 이전 설치 경로 우선 탐색: 레지스트리 `Uninstall` 키의 `InstallLocation`을 조회하여 커스텀 경로 사용자도 원클릭 재설치(업그레이드)가 가능하도록 지원할 것.



