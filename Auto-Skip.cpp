// Auto-Skip.cpp //
// Auto-Skip by Gametism //
// Version 0.1 //

#include <windows.h>
#include <atomic>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>

#define AUTOSKIP_NAME    "Auto-Skip"
#define AUTOSKIP_VERSION "0.1"
#define AUTOSKIP_AUTHOR  "Gametism"

static std::atomic<bool> g_running = true;

static DWORD g_startDelayMs = 0;
static DWORD g_totalRuntimeMs = 5000;
static DWORD g_pressIntervalMs = 25;
static DWORD g_keyHoldMs = 10;

static bool g_onlyWhenGameForeground = true;

static std::vector<WORD> g_keyboardKeys;

static std::wstring g_modulePath;
static std::wstring g_iniPath;
static std::wstring g_logPath;
static std::wstring g_exePath;
static std::wstring g_exeName;

static DWORD g_burstsSent = 0;
static DWORD g_inputsSent = 0;

static std::string WStringToString(const std::wstring& ws)
{
    if (ws.empty())
        return {};

    int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);

    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, result.data(), size, nullptr, nullptr);

    if (!result.empty() && result.back() == '\0')
        result.pop_back();

    return result;
}

static void Log(const std::string& text)
{
    std::ofstream log(WStringToString(g_logPath), std::ios::app);
    if (log)
        log << text << std::endl;
}

static std::wstring GetThisModulePath()
{
    HMODULE module = nullptr;

    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&GetThisModulePath),
        &module
    );

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);

    return path;
}

static std::wstring GetExePath()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

static std::wstring GetFileNameOnly(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return path;

    return path.substr(slash + 1);
}

static std::wstring ReplaceExtension(const std::wstring& path, const std::wstring& newExt)
{
    std::wstring result = path;
    size_t dot = result.find_last_of(L'.');

    if (dot != std::wstring::npos)
        result = result.substr(0, dot);

    result += newExt;
    return result;
}

static std::wstring ToUpper(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), ::towupper);
    return text;
}

static std::wstring ReadIniString(const wchar_t* key, const wchar_t* defaultValue)
{
    wchar_t buffer[512]{};

    GetPrivateProfileStringW(
        L"AutoSkip",
        key,
        defaultValue,
        buffer,
        512,
        g_iniPath.c_str()
    );

    return buffer;
}

static std::vector<std::wstring> SplitList(std::wstring text)
{
    std::vector<std::wstring> result;
    size_t start = 0;

    while (start < text.size())
    {
        size_t comma = text.find(L',', start);

        std::wstring item = text.substr(
            start,
            comma == std::wstring::npos ? std::wstring::npos : comma - start
        );

        item.erase(std::remove_if(item.begin(), item.end(), iswspace), item.end());

        if (!item.empty())
            result.push_back(ToUpper(item));

        if (comma == std::wstring::npos)
            break;

        start = comma + 1;
    }

    return result;
}

static WORD ParseKeyboardKey(const std::wstring& key)
{
    if (key == L"SPACE" || key == L"SPACEBAR") return VK_SPACE;
    if (key == L"ENTER" || key == L"RETURN") return VK_RETURN;
    if (key == L"ESC" || key == L"ESCAPE") return VK_ESCAPE;
    if (key == L"TAB") return VK_TAB;
    if (key == L"BACKSPACE") return VK_BACK;

    if (key.length() == 1)
    {
        wchar_t c = key[0];

        if (c >= L'A' && c <= L'Z')
            return static_cast<WORD>(c);

        if (c >= L'0' && c <= L'9')
            return static_cast<WORD>(c);
    }

    if (key.length() >= 2 && key[0] == L'F')
    {
        int number = _wtoi(key.c_str() + 1);
        if (number >= 1 && number <= 24)
            return static_cast<WORD>(VK_F1 + number - 1);
    }

    return 0;
}

static bool IsGameForeground()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    return pid == GetCurrentProcessId();
}

static void SendKeyboardBurst()
{
    if (g_keyboardKeys.empty())
        return;

    std::vector<INPUT> inputs;
    inputs.reserve(g_keyboardKeys.size() * 2);

    for (WORD vk : g_keyboardKeys)
    {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = vk;
        inputs.push_back(down);
    }

    if (g_keyHoldMs > 0)
        Sleep(g_keyHoldMs);

    for (WORD vk : g_keyboardKeys)
    {
        INPUT up{};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = vk;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }

    UINT sent = SendInput(
        static_cast<UINT>(inputs.size()),
        inputs.data(),
        sizeof(INPUT)
    );

    g_burstsSent++;
    g_inputsSent += sent;
}

static bool ShouldRunForThisExe()
{
    std::wstring targetExe = ReadIniString(L"TargetExe", L"");

    if (targetExe.empty())
        return true;

    return ToUpper(targetExe) == ToUpper(g_exeName);
}

static void LoadConfig()
{
    g_startDelayMs = GetPrivateProfileIntW(L"AutoSkip", L"StartDelayMs", 0, g_iniPath.c_str());
    g_totalRuntimeMs = GetPrivateProfileIntW(L"AutoSkip", L"TotalRuntimeMs", 5000, g_iniPath.c_str());
    g_pressIntervalMs = GetPrivateProfileIntW(L"AutoSkip", L"PressIntervalMs", 25, g_iniPath.c_str());
    g_keyHoldMs = GetPrivateProfileIntW(L"AutoSkip", L"KeyHoldMs", 10, g_iniPath.c_str());
    g_onlyWhenGameForeground = GetPrivateProfileIntW(L"AutoSkip", L"OnlyWhenGameForeground", 1, g_iniPath.c_str()) != 0;

    g_keyboardKeys.clear();

    for (const auto& key : SplitList(ReadIniString(L"KeyboardKeys", L"SPACE,ENTER,ESCAPE")))
    {
        WORD parsed = ParseKeyboardKey(key);
        if (parsed)
            g_keyboardKeys.push_back(parsed);
    }

    if (g_pressIntervalMs < 1)
        g_pressIntervalMs = 1;

    if (g_totalRuntimeMs < 1)
        g_totalRuntimeMs = 1;
}

static DWORD WINAPI MainThread(LPVOID)
{
    g_modulePath = GetThisModulePath();
    g_iniPath = ReplaceExtension(g_modulePath, L".ini");
    g_logPath = ReplaceExtension(g_modulePath, L".log");
    g_exePath = GetExePath();
    g_exeName = GetFileNameOnly(g_exePath);

    {
        std::ofstream clear(WStringToString(g_logPath), std::ios::trunc);
    }

    LoadConfig();

    Log("------------------------------------------------");
    Log(std::string(AUTOSKIP_NAME) + " by " + AUTOSKIP_AUTHOR);
    Log("Version " + std::string(AUTOSKIP_VERSION));
    Log("------------------------------------------------");
    Log("EXE: " + WStringToString(g_exeName));
    Log("INI: " + WStringToString(g_iniPath));

    if (!ShouldRunForThisExe())
    {
        Log("TargetExe does not match. Auto-Skip disabled for this process.");
        return 0;
    }

    Log("Auto-Skip active.");

    if (g_startDelayMs > 0)
        Sleep(g_startDelayMs);

    DWORD startTime = GetTickCount();

    while (g_running)
    {
        DWORD elapsed = GetTickCount() - startTime;

        if (elapsed >= g_totalRuntimeMs)
            break;

        bool allowed = !g_onlyWhenGameForeground || IsGameForeground();

        if (allowed)
            SendKeyboardBurst();

        Sleep(g_pressIntervalMs);
    }

    Log("Finished.");
    Log("Bursts sent: " + std::to_string(g_burstsSent));
    Log("Inputs sent: " + std::to_string(g_inputsSent));

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        HANDLE thread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        g_running = false;
    }

    return TRUE;
}
