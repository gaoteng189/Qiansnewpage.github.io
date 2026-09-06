// stub.cpp —— 单 exe 启动器（纯 Win32，不链接 Qt）
//
// 把真正的 Qt 程序（start-server-core.exe）以及全部运行时 DLL、平台插件
// 打包成 payload.bin（自定义归档），用 objcopy 作为二进制数据嵌入本 exe。
// 启动时：
//   1. 解析归档，把所有文件释放到 %TEMP%\start-server-app\；
//   2. 通过环境变量 START_SERVER_ROOT 告诉 core 项目根目录的位置；
//   3. 运行 core，等待其退出。
//
// 这样最终只需分发这一个 exe，无需携带任何 DLL。
#include <windows.h>
#include <string>
#include <cstdint>

extern "C" {
    extern unsigned char _binary_payload_bin_start[];
    extern unsigned char _binary_payload_bin_end[];
}

static std::wstring g_dir;

static uint32_t read32(const unsigned char*& p) {
    uint32_t v = uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                 (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    p += 4;
    return v;
}

static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static bool writeFile(const std::wstring& rel, const unsigned char* data, uint32_t size) {
    std::wstring outPath = g_dir + L"\\" + rel;

    size_t lastSep = outPath.find_last_of(L"/\\");
    if (lastSep != std::wstring::npos) {
        std::wstring parent = outPath.substr(0, lastSep);
        std::wstring cur;
        for (wchar_t c : parent) {
            if (c == L'/' || c == L'\\') {
                if (!cur.empty()) CreateDirectoryW(cur.c_str(), nullptr);
                cur += L'\\';
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) CreateDirectoryW(cur.c_str(), nullptr);
    }

    HANDLE h = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(h, data, size, &written, nullptr);
    CloseHandle(h);
    return true;
}

static void extractAll() {
    const unsigned char* p = _binary_payload_bin_start;
    const unsigned char* end = _binary_payload_bin_end;
    if (p >= end) return;
    uint32_t count = read32(p);
    for (uint32_t i = 0; i < count && p < end; i++) {
        uint32_t nameLen = read32(p);
        std::string name((const char*)p, nameLen);
        p += nameLen;
        uint32_t dataLen = read32(p);
        writeFile(utf8ToWide(name), p, dataLen);
        p += dataLen;
    }
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // 1. 释放到临时目录
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    g_dir = std::wstring(temp) + L"start-server-app";
    CreateDirectoryW(g_dir.c_str(), nullptr);
    extractAll();

    // 2. 定位项目根目录（优先 stub exe 同目录，其次其上级）
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(hInst, self, MAX_PATH);
    std::wstring selfDir = self;
    size_t p = selfDir.find_last_of(L"/\\");
    if (p != std::wstring::npos) selfDir = selfDir.substr(0, p);

    std::wstring root = selfDir;
    if (GetFileAttributesW((root + L"\\server.js").c_str()) == INVALID_FILE_ATTRIBUTES) {
        size_t q = root.find_last_of(L"/\\");
        if (q != std::wstring::npos) root = root.substr(0, q);
    }
    SetEnvironmentVariableW(L"START_SERVER_ROOT", root.c_str());

    // 把根目录写入 root.txt（提权重启后环境变量可能丢失，文件更可靠）
    {
        std::string rootUtf8;
        for (wchar_t c : root) rootUtf8 += (char)c;
        HANDLE h = CreateFileW((g_dir + L"\\root.txt").c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, rootUtf8.c_str(), (DWORD)rootUtf8.size(), &written, nullptr);
            CloseHandle(h);
        }
    }

    // 3. 运行 core
    std::wstring core = g_dir + L"\\start-server-core.exe";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(core.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr,
                       g_dir.c_str(), &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return 0;
}
