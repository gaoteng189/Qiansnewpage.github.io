// ============================================================
//  留言板服务控制台 (C++ TUI)
//  启动/停止后端 + Cloudflare 隧道、设置端口、自动更新留言页地址
//  编译：g++ -std=c++17 -O2 -static main.cpp -o start-server.exe -lshell32
// ============================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>

static PROCESS_INFORMATION g_node = {};
static PROCESS_INFORMATION g_tunnel = {};
static std::string g_root;
static std::string g_tunnelUrl;
static int g_port = 50304;

// ---------- 编码转换 ----------
static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// ---------- 权限 ----------
static bool IsElevated() {
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION e{};
        DWORD sz = sizeof(e);
        if (GetTokenInformation(token, TokenElevation, &e, sz, &sz))
            elevated = e.TokenIsElevated;
        CloseHandle(token);
    }
    return elevated != FALSE;
}

static std::string GetRoot() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path = buf;
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) path = path.substr(0, pos);
    return WideToUtf8(path);
}

// ---------- 文件 ----------
static std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (f) f.write(content.data(), (std::streamsize)content.size());
}

// ---------- 进程 ----------
static bool IsRunning(PROCESS_INFORMATION& pi) {
    if (pi.hProcess == nullptr) return false;
    DWORD code = 0;
    if (GetExitCodeProcess(pi.hProcess, &code) && code == STILL_ACTIVE) return true;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    pi.hProcess = nullptr;
    pi.hThread = nullptr;
    return false;
}

static bool LaunchProcess(const std::string& cmdline, PROCESS_INFORMATION& pi, const std::string& logFile = "") {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outHandle = nullptr;
    if (!logFile.empty()) {
        HANDLE f = CreateFileW(Utf8ToWide(logFile).c_str(), GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        outHandle = f;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = f;
        si.hStdError = f;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }

    std::wstring cmd = Utf8ToWide(cmdline);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');

    std::wstring workDir = Utf8ToWide(g_root);
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi);
    if (outHandle) CloseHandle(outHandle);
    return ok != FALSE;
}

// ---------- WS_URL ----------
static void SetWsUrl(const std::string& url) {
    std::string path = g_root + "\\message\\index.html";
    std::string content = ReadFile(path);
    std::regex re("var WS_URL = 'wss://[^']+';");
    std::string replacement = "var WS_URL = '" + url + "';";
    content = std::regex_replace(content, re, replacement);
    WriteFile(path, content);
}

static void ResetWsUrl() {
    SetWsUrl("wss://YOUR-TUNNEL.trycloudflare.com");
}

// ---------- 端口 ----------
static void LoadPort() {
    std::string path = g_root + "\\.server-port";
    std::string content = ReadFile(path);
    if (!content.empty()) {
        try { g_port = std::stoi(content); } catch (...) {}
    }
    if (g_port < 1 || g_port > 65535) g_port = 50304;
}

static void SavePort(int p) {
    WriteFile(g_root + "\\.server-port", std::to_string(p));
}

// ---------- 服务 ----------
static void StopServices() {
    if (IsRunning(g_tunnel)) {
        TerminateProcess(g_tunnel.hProcess, 0);
        CloseHandle(g_tunnel.hProcess);
        CloseHandle(g_tunnel.hThread);
        g_tunnel = {};
    }
    if (IsRunning(g_node)) {
        TerminateProcess(g_node.hProcess, 0);
        CloseHandle(g_node.hProcess);
        CloseHandle(g_node.hThread);
        g_node = {};
    }
    g_tunnelUrl.clear();
    ResetWsUrl();
    std::cout << "  Services stopped. Message page URL reset to placeholder." << std::endl;
}

static void StartServices() {
    if (IsRunning(g_node)) {
        std::cout << "  Services are already running." << std::endl;
        return;
    }

    std::cout << "  Starting backend server.js (port " << g_port << ")..." << std::endl;
    if (!LaunchProcess("node server.js " + std::to_string(g_port), g_node, g_root + "\\.node.log")) {
        std::cout << "  Failed to start node." << std::endl;
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    std::cout << "  Starting Cloudflare Tunnel..." << std::endl;
    std::string logFile = g_root + "\\.cloudflared.log";
    if (!LaunchProcess("cloudflared tunnel --url http://localhost:" + std::to_string(g_port),
                       g_tunnel, logFile)) {
        std::cout << "  Failed to start cloudflared." << std::endl;
        StopServices();
        return;
    }

    std::cout << "  Waiting for tunnel URL..." << std::endl;
    std::regex re("https://([a-z0-9-]+\\.trycloudflare\\.com)");
    std::string url;
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(90)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::string log = ReadFile(logFile);
        std::smatch m;
        if (std::regex_search(log, m, re)) {
            url = m[1].str();
            break;
        }
        if (!IsRunning(g_tunnel)) break;
    }

    if (url.empty()) {
        std::cout << "  Failed to obtain tunnel URL. Check cloudflared." << std::endl;
        StopServices();
        return;
    }

    g_tunnelUrl = "https://" + url;
    SetWsUrl("wss://" + url);
    std::cout << std::endl;
    std::cout << "  Tunnel URL : " << g_tunnelUrl << std::endl;
    std::cout << "  Message page updated to: wss://" << url << std::endl;
}

static void SetPortInteractive() {
    if (IsRunning(g_node)) {
        std::cout << "  Please stop services before changing the port." << std::endl;
        return;
    }
    std::cout << "  Current port is " << g_port << ". Enter new port (1-65535): ";
    std::string line;
    std::getline(std::cin, line);
    int newPort = 0;
    try { newPort = std::stoi(line); } catch (...) { newPort = 0; }
    if (newPort >= 1 && newPort <= 65535) {
        g_port = newPort;
        SavePort(g_port);
        std::cout << "  Port set to " << g_port << std::endl;
    } else {
        std::cout << "  Invalid port." << std::endl;
    }
}

// ---------- 菜单 ----------
static void ShowMenu() {
    system("cls");
    const char* nodeState = IsRunning(g_node) ? "Running" : "Stopped";
    const char* tunnelState = IsRunning(g_tunnel) ? "Running" : "Stopped";

    std::cout << "========================================" << std::endl;
    std::cout << "       Message Board Console" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "  Port           : " << g_port << std::endl;
    std::cout << "  Backend (node) : " << nodeState << std::endl;
    std::cout << "  Tunnel         : " << tunnelState << std::endl;
    if (!g_tunnelUrl.empty()) {
        std::cout << "  Tunnel URL     : " << g_tunnelUrl << std::endl;
    }
    std::cout << std::endl;
    std::cout << "  [1] Start services" << std::endl;
    std::cout << "  [2] Stop services" << std::endl;
    std::cout << "  [3] Set port" << std::endl;
    std::cout << "  [4] Exit" << std::endl;
    std::cout << std::endl;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    g_root = GetRoot();
    LoadPort();

    if (!IsElevated()) {
        wchar_t buf[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        ShellExecuteW(nullptr, L"runas", buf, nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    bool running = true;
    while (running) {
        ShowMenu();
        std::cout << "  Select (1-4): ";
        std::string line;
        std::getline(std::cin, line);
        if (line == "1") StartServices();
        else if (line == "2") StopServices();
        else if (line == "3") SetPortInteractive();
        else if (line == "4") running = false;
        else std::cout << "  Invalid choice." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    StopServices();
    std::cout << "  Bye." << std::endl;
    return 0;
}
