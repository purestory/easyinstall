#include "Builder.h"
#include "miniz.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <iostream>
#include <fstream>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

static std::string toUtf8(const std::wstring& str) {
    if (str.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string res(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &res[0], len - 1, nullptr, nullptr);
    return res;
}

struct ScannedFile {
    std::wstring fullPath;
    std::wstring relPath;
    uint64_t size;
};

static void scanDirectoryRecursive(const std::wstring& baseDir, const std::wstring& currentSubDir, std::vector<ScannedFile>& files) {
    std::wstring searchPattern = baseDir;
    if (!currentSubDir.empty()) {
        searchPattern += L"\\" + currentSubDir;
    }
    searchPattern += L"\\*";

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
            continue;
        }

        std::wstring itemRelPath = currentSubDir.empty() ? fd.cFileName : (currentSubDir + L"\\" + fd.cFileName);
        std::wstring itemFullPath = baseDir + L"\\" + itemRelPath;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scanDirectoryRecursive(baseDir, itemRelPath, files);
        } else {
            ULARGE_INTEGER uli;
            uli.LowPart = fd.nFileSizeLow;
            uli.HighPart = fd.nFileSizeHigh;

            ScannedFile sf;
            sf.fullPath = itemFullPath;
            sf.relPath = itemRelPath;
            sf.size = uli.QuadPart;
            files.push_back(sf);
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

std::vector<std::wstring> Builder::findExecutableFiles(const std::wstring& dirPath) {
    std::vector<std::wstring> exeList;
    if (dirPath.empty() || !PathFileExistsW(dirPath.c_str())) return exeList;

    DWORD attr = GetFileAttributesW(dirPath.c_str());
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wstring lower = dirPath;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".exe") {
            const wchar_t* pName = PathFindFileNameW(dirPath.c_str());
            exeList.push_back(pName);
        }
        return exeList;
    }

    std::vector<ScannedFile> files;
    scanDirectoryRecursive(dirPath, L"", files);

    for (const auto& f : files) {
        std::wstring lower = f.relPath;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
        if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".exe") {
            exeList.push_back(f.relPath);
        }
    }
    return exeList;
}

BuildResult Builder::build(const BuilderConfig& config, ProgressCallback callback) {
    BuildResult result;

    if (config.inputPath.empty()) {
        result.errorMessage = L"대상 프로그램 경로가 지정되지 않았습니다.";
        return result;
    }

    wchar_t absInput[MAX_PATH];
    if (GetFullPathNameW(config.inputPath.c_str(), MAX_PATH, absInput, nullptr) == 0) {
        wcscpy_s(absInput, config.inputPath.c_str());
    }

    if (!PathFileExistsW(absInput)) {
        result.errorMessage = L"지정한 대상 경로가 존재하지 않습니다: " + config.inputPath;
        return result;
    }
    if (config.outputPath.empty()) {
        result.errorMessage = L"출력 파일 경로가 지정되지 않았습니다.";
        return result;
    }

    wchar_t absOutput[MAX_PATH];
    if (GetFullPathNameW(config.outputPath.c_str(), MAX_PATH, absOutput, nullptr) == 0) {
        wcscpy_s(absOutput, config.outputPath.c_str());
    }

    if (callback) callback(5, L"대상 파일 목록 수집 중...");

    DWORD attr = GetFileAttributesW(absInput);
    bool isDirectory = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;

    std::vector<ScannedFile> files;
    std::wstring detectedMainExe = config.mainExeName;

    if (isDirectory) {
        scanDirectoryRecursive(absInput, L"", files);
        if (files.empty()) {
            result.errorMessage = L"지정한 폴더가 비어 있습니다.";
            return result;
        }
        // 메인 실행 파일 자동 감지
        if (detectedMainExe.empty()) {
            for (const auto& f : files) {
                std::wstring lower = f.relPath;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
                if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".exe") {
                    detectedMainExe = f.relPath;
                    break;
                }
            }
        }
    } else {
        // 단일 파일
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(absInput, GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER uli;
            uli.LowPart = fad.nFileSizeLow;
            uli.HighPart = fad.nFileSizeHigh;

            ScannedFile sf;
            sf.fullPath = absInput;
            sf.relPath = PathFindFileNameW(absInput);
            sf.size = uli.QuadPart;
            files.push_back(sf);

            if (detectedMainExe.empty()) {
                detectedMainExe = sf.relPath;
            }
        } else {
            result.errorMessage = L"파일 정보를 읽을 수 없습니다: " + std::wstring(absInput);
            return result;
        }
    }

    if (detectedMainExe.empty()) {
        result.errorMessage = L"메인 실행 파일(.exe)을 찾을 수 없습니다. 직접 메인 실행 파일명을 지정하십시오.";
        return result;
    }

    // 총 크기 계산
    uint64_t totalUncompressed = 0;
    for (const auto& f : files) {
        totalUncompressed += f.size;
    }
    result.totalFiles = static_cast<uint32_t>(files.size());
    result.uncompressedBytes = totalUncompressed;

    // 메타데이터 준비
    PackageMeta meta;
    meta.appName = config.appName.empty() ? PathFindFileNameW(config.inputPath.c_str()) : config.appName;
    meta.appVersion = config.appVersion.empty() ? L"1.0.0" : config.appVersion;
    meta.publisher = config.publisher.empty() ? L"Unknown" : config.publisher;
    meta.mainExeName = detectedMainExe;
    meta.defaultInstallDir = meta.appName;
    meta.defaultScope = config.defaultScope;
    meta.createDesktopShortcut = config.createDesktopShortcut;
    meta.createStartMenuShortcut = config.createStartMenuShortcut;
    meta.launchAfterInstall = config.launchAfterInstall;
    meta.uncompressedSize = totalUncompressed;

    std::vector<uint8_t> serializedMeta = meta.serialize();

    // 임시 ZIP 파일 생성
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t tempZipFile[MAX_PATH];
    GetTempFileNameW(tempDir, L"EZI", 0, tempZipFile);

    if (callback) callback(15, L"패키지 압축 아카이브 생성 중...");

    FILE* pZipFile = _wfopen(tempZipFile, L"wb+");
    if (!pZipFile) {
        result.errorMessage = L"임시 압축 파일을 생성할 수 없습니다.";
        return result;
    }

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_writer_init_cfile(&zip, pZipFile, 0)) {
        fclose(pZipFile);
        DeleteFileW(tempZipFile);
        result.errorMessage = L"압축 라이브러리 초기화 실패.";
        return result;
    }

    // 파일 하나씩 압축
    std::vector<uint8_t> fileBuf;
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& sf = files[i];
        int percent = 15 + static_cast<int>((i * 60) / files.size());
        if (callback) {
            callback(percent, L"압축 중 (" + std::to_wstring(i + 1) + L"/" + std::to_wstring(files.size()) + L"): " + sf.relPath);
        }

        std::string utf8RelName = toUtf8(sf.relPath);
        for (char& c : utf8RelName) {
            if (c == '\\') c = '/';
        }

        HANDLE hFile = CreateFileW(sf.fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            mz_zip_writer_end(&zip);
            fclose(pZipFile);
            DeleteFileW(tempZipFile);
            result.errorMessage = L"파일을 읽을 수 없습니다: " + sf.fullPath;
            return result;
        }

        if (sf.size > 0) {
            fileBuf.resize(static_cast<size_t>(sf.size));
            DWORD bytesRead = 0;
            if (!ReadFile(hFile, fileBuf.data(), static_cast<DWORD>(sf.size), &bytesRead, nullptr) || bytesRead != sf.size) {
                CloseHandle(hFile);
                mz_zip_writer_end(&zip);
                fclose(pZipFile);
                DeleteFileW(tempZipFile);
                result.errorMessage = L"파일 읽기 오류: " + sf.fullPath;
                return result;
            }
            CloseHandle(hFile);

            if (!mz_zip_writer_add_mem(&zip, utf8RelName.c_str(), fileBuf.data(), fileBuf.size(), MZ_DEFAULT_COMPRESSION)) {
                mz_zip_writer_end(&zip);
                fclose(pZipFile);
                DeleteFileW(tempZipFile);
                result.errorMessage = L"ZIP 항목 압축 실패: " + sf.relPath;
                return result;
            }
        } else {
            CloseHandle(hFile);
            mz_zip_writer_add_mem(&zip, utf8RelName.c_str(), nullptr, 0, MZ_DEFAULT_COMPRESSION);
        }
    }

    if (!mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        fclose(pZipFile);
        DeleteFileW(tempZipFile);
        result.errorMessage = L"ZIP 아카이브 종료 실패.";
        return result;
    }
    mz_zip_writer_end(&zip);
    fclose(pZipFile);

    if (callback) callback(80, L"실행 파일 바이너리 스텁 및 페이로드 결합 중...");

    // 자기 자신(easyinstall.exe)의 스텁 바이너리 읽기
    wchar_t selfExePath[MAX_PATH];
    GetModuleFileNameW(nullptr, selfExePath, MAX_PATH);

    HANDLE hSelf = CreateFileW(selfExePath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hSelf == INVALID_HANDLE_VALUE) {
        DeleteFileW(tempZipFile);
        result.errorMessage = L"빌더 자체 바이너리를 열 수 없습니다.";
        return result;
    }

    LARGE_INTEGER selfSize;
    GetFileSizeEx(hSelf, &selfSize);
    uint64_t stubSize = selfSize.QuadPart;

    // 만약 이미 푸터가 있는 바이너리라면(기존 setup.exe를 재패키징하는 경우 등 방어)
    if (selfSize.QuadPart > sizeof(EasyInstallFooter)) {
        SetFilePointer(hSelf, -static_cast<LONG>(sizeof(EasyInstallFooter)), nullptr, FILE_END);
        EasyInstallFooter existingFooter;
        DWORD readBytes = 0;
        if (ReadFile(hSelf, &existingFooter, sizeof(existingFooter), &readBytes, nullptr) && readBytes == sizeof(existingFooter)) {
            if (existingFooter.magic == EASYINSTALL_MAGIC && existingFooter.zipOffset < static_cast<unsigned long long>(selfSize.QuadPart)) {
                stubSize = existingFooter.zipOffset;
            }
        }
    }

    SetFilePointer(hSelf, 0, nullptr, FILE_BEGIN);
    std::vector<uint8_t> stubBytes(static_cast<size_t>(stubSize));
    DWORD readStub = 0;
    if (!ReadFile(hSelf, stubBytes.data(), static_cast<DWORD>(stubSize), &readStub, nullptr) || readStub != stubSize) {
        CloseHandle(hSelf);
        DeleteFileW(tempZipFile);
        result.errorMessage = L"스텁 바이너리 복사 실패.";
        return result;
    }
    CloseHandle(hSelf);

    // 출력 디렉터리 생성
    wchar_t outDir[MAX_PATH];
    wcscpy_s(outDir, absOutput);
    PathRemoveFileSpecW(outDir);
    if (wcslen(outDir) > 0 && !PathFileExistsW(outDir)) {
        SHCreateDirectoryExW(nullptr, outDir, nullptr);
    }

    // 최종 Setup.exe 생성
    HANDLE hOut = CreateFileW(absOutput, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) {
        DeleteFileW(tempZipFile);
        result.errorMessage = L"출력 파일 생성 실패: " + std::wstring(absOutput);
        return result;
    }

    DWORD written = 0;
    // 1. 스텁 쓰기
    if (!WriteFile(hOut, stubBytes.data(), static_cast<DWORD>(stubBytes.size()), &written, nullptr) || written != stubBytes.size()) {
        CloseHandle(hOut);
        DeleteFileW(absOutput);
        DeleteFileW(tempZipFile);
        result.errorMessage = L"출력 파일에 스텁을 쓰는 중 오류가 발생했습니다.";
        return result;
    }

    uint64_t zipOffset = stubBytes.size();

    // 2. ZIP 쓰기
    HANDLE hZip = CreateFileW(tempZipFile, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hZip == INVALID_HANDLE_VALUE) {
        CloseHandle(hOut);
        DeleteFileW(config.outputPath.c_str());
        DeleteFileW(tempZipFile);
        result.errorMessage = L"임시 ZIP 파일을 읽을 수 없습니다.";
        return result;
    }

    LARGE_INTEGER zipFileSize;
    GetFileSizeEx(hZip, &zipFileSize);
    uint64_t zipSize = zipFileSize.QuadPart;

    std::vector<uint8_t> ioChunk(64 * 1024);
    uint64_t zipRemaining = zipSize;
    while (zipRemaining > 0) {
        DWORD toRead = static_cast<DWORD>(std::min<uint64_t>(zipRemaining, ioChunk.size()));
        DWORD got = 0;
        if (!ReadFile(hZip, ioChunk.data(), toRead, &got, nullptr) || got != toRead) {
            CloseHandle(hZip);
            CloseHandle(hOut);
            DeleteFileW(config.outputPath.c_str());
            DeleteFileW(tempZipFile);
            result.errorMessage = L"ZIP 스트림 복사 오류.";
            return result;
        }
        DWORD wrote = 0;
        if (!WriteFile(hOut, ioChunk.data(), got, &wrote, nullptr) || wrote != got) {
            CloseHandle(hZip);
            CloseHandle(hOut);
            DeleteFileW(config.outputPath.c_str());
            DeleteFileW(tempZipFile);
            result.errorMessage = L"출력 파일에 ZIP을 쓰는 중 오류.";
            return result;
        }
        zipRemaining -= got;
    }
    CloseHandle(hZip);
    DeleteFileW(tempZipFile);

    // 3. 메타데이터 쓰기
    uint64_t metaOffset = zipOffset + zipSize;
    uint32_t metaSize = static_cast<uint32_t>(serializedMeta.size());
    if (!WriteFile(hOut, serializedMeta.data(), metaSize, &written, nullptr) || written != metaSize) {
        CloseHandle(hOut);
        DeleteFileW(config.outputPath.c_str());
        result.errorMessage = L"메타데이터 쓰기 오류.";
        return result;
    }

    // 4. 푸터 쓰기
    EasyInstallFooter footer;
    footer.magic = EASYINSTALL_MAGIC;
    footer.zipOffset = zipOffset;
    footer.zipSize = zipSize;
    footer.metaOffset = metaOffset;
    footer.metaSize = metaSize;
    footer.reserved = 0;

    if (!WriteFile(hOut, &footer, sizeof(footer), &written, nullptr) || written != sizeof(footer)) {
        CloseHandle(hOut);
        DeleteFileW(config.outputPath.c_str());
        result.errorMessage = L"푸터 쓰기 오류.";
        return result;
    }

    LARGE_INTEGER finalOutPos;
    finalOutPos.QuadPart = 0;
    GetFileSizeEx(hOut, &finalOutPos);
    result.outputBytes = finalOutPos.QuadPart;
    CloseHandle(hOut);

    if (callback) callback(100, L"설치 파일(Setup.exe) 생성 완료!");
    result.success = true;
    return result;
}
