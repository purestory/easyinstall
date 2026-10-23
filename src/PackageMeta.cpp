#include "PackageMeta.h"
#include <cstring>

static void writeString(std::vector<unsigned char>& buf, const std::wstring& str) {
    if (str.empty()) {
        unsigned int len = 0;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&len);
        buf.insert(buf.end(), p, p + sizeof(len));
        return;
    }
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 1) {
        unsigned int len = 0;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(&len);
        buf.insert(buf.end(), p, p + sizeof(len));
        return;
    }
    std::string utf8(utf8Len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &utf8[0], utf8Len - 1, nullptr, nullptr);

    unsigned int len = static_cast<unsigned int>(utf8.size());
    const unsigned char* pLen = reinterpret_cast<const unsigned char*>(&len);
    buf.insert(buf.end(), pLen, pLen + sizeof(len));
    buf.insert(buf.end(), utf8.begin(), utf8.end());
}

static bool readString(const unsigned char* data, size_t size, size_t& offset, std::wstring& outStr) {
    if (offset + sizeof(unsigned int) > size) return false;
    unsigned int len = 0;
    std::memcpy(&len, data + offset, sizeof(len));
    offset += sizeof(len);

    if (len == 0) {
        outStr.clear();
        return true;
    }

    if (offset + len > size) return false;
    std::string utf8(reinterpret_cast<const char*>(data + offset), len);
    offset += len;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (wlen <= 0) {
        outStr.clear();
        return true;
    }
    outStr.resize(wlen);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &outStr[0], wlen);
    return true;
}

template<typename T>
static void writeVal(std::vector<unsigned char>& buf, const T& val) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&val);
    buf.insert(buf.end(), p, p + sizeof(T));
}

template<typename T>
static bool readVal(const unsigned char* data, size_t size, size_t& offset, T& outVal) {
    if (offset + sizeof(T) > size) return false;
    std::memcpy(&outVal, data + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

std::vector<unsigned char> PackageMeta::serialize() const {
    std::vector<unsigned char> buf;
    buf.reserve(1024);

    writeString(buf, appName);
    writeString(buf, appVersion);
    writeString(buf, publisher);
    writeString(buf, mainExeName);
    writeString(buf, defaultInstallDir);

    unsigned int scope = static_cast<unsigned int>(defaultScope);
    writeVal(buf, scope);

    unsigned char bDesk = createDesktopShortcut ? 1 : 0;
    unsigned char bStart = createStartMenuShortcut ? 1 : 0;
    unsigned char bLaunch = launchAfterInstall ? 1 : 0;

    writeVal(buf, bDesk);
    writeVal(buf, bStart);
    writeVal(buf, bLaunch);
    writeVal(buf, uncompressedSize);

    return buf;
}

bool PackageMeta::deserialize(const unsigned char* data, size_t size, PackageMeta& outMeta) {
    if (!data || size == 0) return false;
    size_t offset = 0;

    if (!readString(data, size, offset, outMeta.appName)) return false;
    if (!readString(data, size, offset, outMeta.appVersion)) return false;
    if (!readString(data, size, offset, outMeta.publisher)) return false;
    if (!readString(data, size, offset, outMeta.mainExeName)) return false;
    if (!readString(data, size, offset, outMeta.defaultInstallDir)) return false;

    unsigned int scope = 0;
    if (!readVal(data, size, offset, scope)) return false;
    outMeta.defaultScope = static_cast<EasyInstallScope>(scope);

    unsigned char bDesk = 0, bStart = 0, bLaunch = 0;
    if (!readVal(data, size, offset, bDesk)) return false;
    if (!readVal(data, size, offset, bStart)) return false;
    if (!readVal(data, size, offset, bLaunch)) return false;

    outMeta.createDesktopShortcut = (bDesk != 0);
    outMeta.createStartMenuShortcut = (bStart != 0);
    outMeta.launchAfterInstall = (bLaunch != 0);

    if (!readVal(data, size, offset, outMeta.uncompressedSize)) return false;

    return true;
}
