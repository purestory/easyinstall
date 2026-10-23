# EasyInstall (이지인스톨) 🚀

> 빠르고 간편한 경량 윈도우 단일 실행 파일(Setup.exe) 인스톨러 생성기  
> **A Lightweight, Modern Win32 Setup.exe Installer & Builder for Windows**

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011-blue.svg)](https://microsoft.com/windows)
[![Architecture](https://img.shields.io/badge/Arch-x64-brightgreen.svg)]()
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Release](https://img.shields.io/badge/Release-v1.0.0-blueviolet.svg)](https://github.com/purestory/easyinstall/releases)

---

## 🌟 주요 특징 (Key Features)

- **단일 실행 파일 (Single Standalone Executable)**
  - 압축 파일(ZIP), 메타데이터, 마법사 UI, 언인스톨러를 단 1개의 `Setup.exe`에 통합 패키징
  - 외부 런타임(`.NET`, `Java`, `VC++ Redistributable`) 종속성 없는 순수 Win32 C++ 구현
- **GUI & CLI 듀얼 모드 지원 (Dual-Mode Architecture)**
  - 마우스 클릭만으로 설치 프로그램을 만드는 직관적인 그래픽 대화상자(GUI Builder)
  - CI/CD 파이프라인 및 자동화 스크립트에서 활용 가능한 강력한 콘솔(CLI) 모드
- **스마트 설치 스코프 및 UAC 관리자 권한 승격**
  - **Per-Machine**: `C:\Program Files` 및 `HKLM` 설치 지원 (필요 시 자동으로 UAC 권한 승격)
  - **Per-User**: 일반 사용자 권한(`%LocalAppData%\Programs`) 설치 지원 (UAC 거부 시 안전 폴백)
- **Windows 10 / 11 완벽 호환**
  - Per-Monitor V2 고DPI 인식 지원 (DWM 가상화 흐림 현상 원천 차단)
  - Windows 시작 메뉴 프로그램 그룹 폴더 축약(Flattening) 방지 알고리즘 적용
  - 제어판(설정) '프로그램 추가/제거' 및 Windows Registry 정식 등록
- **완벽하고 안전한 언인스톨러 (Built-in Clean Uninstaller)**
  - 백그라운드 워커 프로세스를 통한 파일 잠금 방지 및 안전한 재시도 삭제 루틴
  - 설치된 파일, 생성된 바탕화면/시작메뉴 바로가기, 레지스트리 잔여물 완전 삭제

---

## 💻 빠른 시작 및 사용법 (Usage)

### 1. GUI 빌더 모드
`easyinstall.exe`를 더블클릭하여 실행하면 직관적인 그래픽 인터페이스가 나타납니다.
1. **대상 경로**: 패키징할 원본 폴더 또는 실행 파일 선택
2. **프로그램 정보**: 프로그램명, 버전, 게시자, 메인 실행 파일 지정
3. **설치 옵션**: 기본 설치 위치(모든 사용자 / 현재 사용자), 바로가기 생성 여부 지정
4. **설치 프로그램 생성** 버튼을 클릭하면 수 초 내로 완성된 `Setup.exe`가 생성됩니다.

### 2. CLI 명령줄 모드 (CI/CD 자동화)
```powershell
# 기본 사용법
easyinstall.exe -i <대상폴더_또는_파일> -o <출력_Setup.exe경로> [옵션]

# 예시: 특정 폴더를 Setup.exe로 빌드
easyinstall.exe -i "D:\MyAppFolder" -o "D:\Dist\MyApp_Setup.exe" -n "MyApp" -v "1.0.0" -p "MyCompany"

# 옵션 목록
  -i, --input <경로>           대상 원본 파일 또는 디렉터리 경로 (필수)
  -o, --output <경로>          생성될 Setup.exe 파일 경로 (필수/생략 시 자동 생성)
  -n, --name <이름>            프로그램 이름 (생략 시 입력 경로명 자동 사용)
  -v, --version <버전>         프로그램 버전 (기본값: 1.0.0)
  -p, --publisher <게시자>     제작자 또는 회사명
  -m, --main-exe <파일명>      메인 실행 파일명 (생략 시 폴더 내 exe 자동 탐색)
      --scope <machine|user>   기본 설치 위치 (machine: Program Files, user: LocalAppData)
      --desktop-shortcut <1|0> 바탕화면 바로가기 기본 체크 (기본값: 1)
      --start-menu <1|0>       시작 메뉴 바로가기 기본 체크 (기본값: 1)
      --launch <1|0>           설치 완료 후 즉시 실행 체크 (기본값: 1)
  -h, --help                   도움말 출력
```

---

## 🛠️ 빌드 방법 (Build Instructions)

### 요구 사양
- Windows 10 / 11 (x64)
- Visual Studio 2022 (C++ 데스크톱 개발 워크로드)

### MSBuild를 이용한 빌드
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" easyinstall.sln /p:Configuration=Release /p:Platform=x64
```
컴파일이 완료되면 `bin/Release/easyinstall.exe` 바이너리가 생성됩니다.

---

## 📄 라이선스 (License)

본 프로젝트는 [MIT License](LICENSE)에 따라 자유롭게 사용 및 수정, 배포할 수 있습니다.
내장된 압축 모듈은 `miniz` 라이브러리(MIT)를 사용합니다.
