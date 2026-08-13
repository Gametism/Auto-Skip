// Auto-Skip.cpp //
// Auto-Skip by Gametism //
// Version 0.2 //

#include <windows.h>
#include <atomic>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cwctype>

#define AUTOSKIP_NAME    "Auto-Skip"
#define AUTOSKIP_VERSION "0.2"
#define AUTOSKIP_AUTHOR  "Gametism"

static std::atomic<bool> g_running = true;

static DWORD g_startDelayMs = 0;
static DWORD g_totalRuntimeMs = 6000;
static DWORD g_pressIntervalMs = 10;
static DWORD g_keyHoldMs = 0;

static DWORD g_foregroundStableMs = 50;
static DWORD g_waitForForegroundMs = 15000;

static DWORD g_minWindowWidth = 640;
static DWORD g_minWindowHeight = 360;

static DWORD g_enterAfterMs = 0;
static DWORD g_escapeAfterMs = 0;

static DWORD g_maxKeyPresses = 0;

static bool g_onlyWhenGameForeground = true;

static std::vector<WORD> g_keyboardKeys;

static std::wstring g_modulePath;
static std::wstring g_iniPath;
static std::wstring g_logPath;
static std::wstring g_exePath;
static std::wstring g_exeName;

static DWORD g_keyPressesSent = 0;
static DWORD g_inputsSent = 0;

static HANDLE g_processGuard = nullptr;

static std::atomic<WORD> g_heldKey = 0;

static std::string WStringToString(const std::wstring& ws)
{
    if (ws.empty())
        return {};

    int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        ws.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (size <= 0)
        return {};

    std::string result(static_cast<size_t>(size), '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        ws.c_str(),
        -1,
        result.data(),
        size,
        nullptr,
        nullptr
    );

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

static std::wstring ReplaceExtension(
    const std::wstring& path,
    const std::wstring& newExt
)
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
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](wchar_t c)
        {
            return static_cast<wchar_t>(std::towupper(c));
        }
    );

    return text;
}

static std::wstring ReadIniString(
    const wchar_t* key,
    const wchar_t* defaultValue
)
{
    wchar_t buffer[512]{};

    GetPrivateProfileStringW(
        L"AutoSkip",
        key,
        defaultValue,
        buffer,
        static_cast<DWORD>(std::size(buffer)),
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
            comma == std::wstring::npos
            ? std::wstring::npos
            : comma - start
        );

        item.erase(
            std::remove_if(
                item.begin(),
                item.end(),
                [](wchar_t c)
                {
                    return std::iswspace(c) != 0;
                }
            ),
            item.end()
        );

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
    if (key == L"SPACE" || key == L"SPACEBAR")
        return VK_SPACE;

    if (key == L"ENTER" || key == L"RETURN")
        return VK_RETURN;

    if (key == L"ESC" || key == L"ESCAPE")
        return VK_ESCAPE;

    if (key == L"TAB")
        return VK_TAB;

    if (key == L"BACKSPACE")
        return VK_BACK;

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

static std::string KeyName(WORD vk)
{
    switch (vk)
    {
    case VK_SPACE:
        return "SPACE";

    case VK_RETURN:
        return "ENTER";

    case VK_ESCAPE:
        return "ESCAPE";

    case VK_TAB:
        return "TAB";

    case VK_BACK:
        return "BACKSPACE";
    }

    if (vk >= 'A' && vk <= 'Z')
        return std::string(1, static_cast<char>(vk));

    if (vk >= '0' && vk <= '9')
        return std::string(1, static_cast<char>(vk));

    if (vk >= VK_F1 && vk <= VK_F24)
        return "F" + std::to_string(vk - VK_F1 + 1);

    return "VK_" + std::to_string(vk);
}

static void RemoveDuplicateKeys()
{
    std::vector<WORD> unique;

    for (WORD key : g_keyboardKeys)
    {
        if (
            std::find(
                unique.begin(),
                unique.end(),
                key
            ) == unique.end()
            )
        {
            unique.push_back(key);
        }
    }

    g_keyboardKeys.swap(unique);
}

static bool GetGameForegroundWindow(
    HWND& hwndOut,
    DWORD& widthOut,
    DWORD& heightOut
)
{
    HWND hwnd = GetForegroundWindow();

    if (!hwnd)
        return false;

    if (!IsWindow(hwnd))
        return false;

    if (!IsWindowVisible(hwnd))
        return false;

    if (IsIconic(hwnd))
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid != GetCurrentProcessId())
        return false;

    RECT rect{};

    if (!GetClientRect(hwnd, &rect))
        return false;

    DWORD width = static_cast<DWORD>(
        std::max<LONG>(0, rect.right - rect.left)
        );

    DWORD height = static_cast<DWORD>(
        std::max<LONG>(0, rect.bottom - rect.top)
        );

    if (
        width < g_minWindowWidth ||
        height < g_minWindowHeight
        )
    {
        return false;
    }

    hwndOut = hwnd;
    widthOut = width;
    heightOut = height;

    return true;
}

static void ReleaseHeldKey()
{
    WORD vk = g_heldKey.exchange(0);

    if (!vk)
        return;

    INPUT up{};
    up.type = INPUT_KEYBOARD;
    up.ki.wVk = vk;
    up.ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(
        1,
        &up,
        sizeof(INPUT)
    );
}

static bool SendSingleKeyboardPress(WORD vk)
{
    ReleaseHeldKey();

    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = vk;

    UINT sentDown = SendInput(
        1,
        &down,
        sizeof(INPUT)
    );

    if (sentDown != 1)
        return false;

    g_heldKey = vk;

    if (g_keyHoldMs > 0)
        Sleep(g_keyHoldMs);

    INPUT up{};
    up.type = INPUT_KEYBOARD;
    up.ki.wVk = vk;
    up.ki.dwFlags = KEYEVENTF_KEYUP;

    UINT sentUp = SendInput(
        1,
        &up,
        sizeof(INPUT)
    );

    g_heldKey = 0;

    g_inputsSent += sentDown + sentUp;

    if (sentUp == 1)
    {
        g_keyPressesSent++;
        return true;
    }

    return false;
}

static bool ShouldRunForThisExe()
{
    std::wstring targetExe = ReadIniString(
        L"TargetExe",
        L""
    );

    if (targetExe.empty())
        return true;

    return ToUpper(targetExe) == ToUpper(g_exeName);
}

static void LoadConfig()
{
    g_startDelayMs = GetPrivateProfileIntW(
        L"AutoSkip",
        L"StartDelayMs",
        0,
        g_iniPath.c_str()
    );

    g_totalRuntimeMs = GetPrivateProfileIntW(
        L"AutoSkip",
        L"TotalRuntimeMs",
        6000,
        g_iniPath.c_str()
    );

    g_pressIntervalMs = GetPrivateProfileIntW(
        L"AutoSkip",
        L"PressIntervalMs",
        10,
        g_iniPath.c_str()
    );

    g_keyHoldMs = GetPrivateProfileIntW(
        L"AutoSkip",
        L"KeyHoldMs",
        0,
        g_iniPath.c_str()
    );

    g_foregroundStableMs = GetPrivateProfileIntW(
        L"AutoSkip",
        L"ForegroundStableMs",
        50,
        g_iniPath.c_str()
    );

    g_waitForForegroundMs = GetPrivateProfileIntW(
        L"AutoSkip",
        L"WaitForForegroundMs",
        15000,
        g_iniPath.c_str()
    );

    g_minWindowWidth = GetPrivateProfileIntW(
        L"AutoSkip",
        L"MinWindowWidth",
        640,
        g_iniPath.c_str()
    );

    g_minWindowHeight = GetPrivateProfileIntW(
        L"AutoSkip",
        L"MinWindowHeight",
        360,
        g_iniPath.c_str()
    );

    g_enterAfterMs = GetPrivateProfileIntW(
        L"AutoSkip",
        L"EnterAfterMs",
        0,
        g_iniPath.c_str()
    );

    g_escapeAfterMs = GetPrivateProfileIntW(
        L"AutoSkip",
        L"EscapeAfterMs",
        0,
        g_iniPath.c_str()
    );

    g_maxKeyPresses = GetPrivateProfileIntW(
        L"AutoSkip",
        L"MaxKeyPresses",
        0,
        g_iniPath.c_str()
    );

    g_onlyWhenGameForeground = GetPrivateProfileIntW(
        L"AutoSkip",
        L"OnlyWhenGameForeground",
        1,
        g_iniPath.c_str()
    ) != 0;

    g_keyboardKeys.clear();

    for (
        const auto& key :
        SplitList(
            ReadIniString(
                L"KeyboardKeys",
                L"SPACE,ENTER,ESCAPE"
            )
        )
        )
    {
        WORD parsed = ParseKeyboardKey(key);

        if (parsed)
            g_keyboardKeys.push_back(parsed);
    }

    RemoveDuplicateKeys();

    if (g_pressIntervalMs < 10)
        g_pressIntervalMs = 10;

    if (g_keyHoldMs > 250)
        g_keyHoldMs = 250;

    if (g_totalRuntimeMs < 1)
        g_totalRuntimeMs = 1;
}

static HANDLE CreateGlobalGuard()
{
    std::wstring guardKey = ToUpper(g_exePath);

    for (wchar_t& c : guardKey)
    {
        if (
            c == L'\\' ||
            c == L'/' ||
            c == L':' ||
            c == L' ' ||
            c == L'.'
            )
        {
            c = L'_';
        }
    }

    std::wstring mutexName =
        L"Local\\Gametism_AutoSkip_" +
        guardKey;

    HANDLE mutex = CreateMutexW(
        nullptr,
        TRUE,
        mutexName.c_str()
    );

    if (!mutex)
        return nullptr;

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        return nullptr;
    }

    return mutex;
}

static bool IsKeyAllowedAtElapsed(
    WORD vk,
    ULONGLONG elapsed
)
{
    if (
        vk == VK_RETURN &&
        elapsed < g_enterAfterMs
        )
    {
        return false;
    }

    if (
        vk == VK_ESCAPE &&
        elapsed < g_escapeAfterMs
        )
    {
        return false;
    }

    return true;
}

static WORD ChooseNextAllowedKey(
    size_t& keyIndex,
    ULONGLONG elapsed
)
{
    if (g_keyboardKeys.empty())
        return 0;

    size_t attempts = 0;

    while (attempts < g_keyboardKeys.size())
    {
        WORD candidate =
            g_keyboardKeys[keyIndex];

        keyIndex++;

        if (keyIndex >= g_keyboardKeys.size())
            keyIndex = 0;

        attempts++;

        if (IsKeyAllowedAtElapsed(candidate, elapsed))
            return candidate;
    }

    return 0;
}

static bool WaitForStableForeground(
    HWND& stableWindowOut
)
{
    HWND currentForeground = nullptr;
    HWND lastForeground = nullptr;

    DWORD width = 0;
    DWORD height = 0;

    DWORD lastWidth = 0;
    DWORD lastHeight = 0;

    ULONGLONG waitStart = GetTickCount64();
    ULONGLONG stableStart = 0;

    while (g_running)
    {
        bool allowed = true;

        if (g_onlyWhenGameForeground)
        {
            allowed = GetGameForegroundWindow(
                currentForeground,
                width,
                height
            );
        }

        if (allowed)
        {
            if (!g_onlyWhenGameForeground)
            {
                stableWindowOut = nullptr;
                return true;
            }

            if (
                currentForeground != lastForeground ||
                width != lastWidth ||
                height != lastHeight
                )
            {
                lastForeground = currentForeground;
                lastWidth = width;
                lastHeight = height;
                stableStart = GetTickCount64();
            }
            else if (
                stableStart != 0 &&
                GetTickCount64() - stableStart >=
                g_foregroundStableMs
                )
            {
                stableWindowOut = currentForeground;
                return true;
            }
        }
        else
        {
            lastForeground = nullptr;
            lastWidth = 0;
            lastHeight = 0;
            stableStart = 0;
        }

        if (
            g_waitForForegroundMs > 0 &&
            GetTickCount64() - waitStart >=
            g_waitForForegroundMs
            )
        {
            return false;
        }

        Sleep(25);
    }

    return false;
}

static DWORD WINAPI MainThread(LPVOID)
{
    g_modulePath = GetThisModulePath();
    g_iniPath = ReplaceExtension(
        g_modulePath,
        L".ini"
    );
    g_logPath = ReplaceExtension(
        g_modulePath,
        L".log"
    );

    g_exePath = GetExePath();
    g_exeName = GetFileNameOnly(g_exePath);

    {
        std::ofstream clear(
            WStringToString(g_logPath),
            std::ios::trunc
        );
    }

    LoadConfig();

    Log("------------------------------------------------");
    Log(
        std::string(AUTOSKIP_NAME) +
        " by " +
        AUTOSKIP_AUTHOR
    );
    Log(
        "Version " +
        std::string(AUTOSKIP_VERSION)
    );
    Log("------------------------------------------------");
    Log(
        "EXE: " +
        WStringToString(g_exeName)
    );
    Log(
        "INI: " +
        WStringToString(g_iniPath)
    );

    if (!ShouldRunForThisExe())
    {
        Log(
            "TargetExe does not match. "
            "Auto-Skip disabled for this process."
        );
        return 0;
    }

    g_processGuard = CreateGlobalGuard();

    if (!g_processGuard)
    {
        Log(
            "Another Auto-Skip instance is already active "
            "for this game executable."
        );
        return 0;
    }

    if (g_keyboardKeys.empty())
    {
        Log(
            "No valid KeyboardKeys configured. "
            "Auto-Skip disabled."
        );

        ReleaseMutex(g_processGuard);
        CloseHandle(g_processGuard);
        g_processGuard = nullptr;

        return 0;
    }

    Log("Auto-Skip active.");
    Log("Input mode: priority/fallback cycling.");
    Log(
        "Press interval: " +
        std::to_string(g_pressIntervalMs) +
        " ms"
    );
    Log(
        "Key hold: " +
        std::to_string(g_keyHoldMs) +
        " ms"
    );
    Log(
        "Enter enabled after: " +
        std::to_string(g_enterAfterMs) +
        " ms"
    );
    Log(
        "Escape enabled after: " +
        std::to_string(g_escapeAfterMs) +
        " ms"
    );
    Log(
        "Minimum window size: " +
        std::to_string(g_minWindowWidth) +
        "x" +
        std::to_string(g_minWindowHeight)
    );

    if (g_startDelayMs > 0)
        Sleep(g_startDelayMs);

    HWND stableWindow = nullptr;

    if (!WaitForStableForeground(stableWindow))
    {
        Log(
            "Timed out waiting for a stable "
            "foreground game window."
        );

        ReleaseMutex(g_processGuard);
        CloseHandle(g_processGuard);
        g_processGuard = nullptr;

        return 0;
    }

    Log("Stable foreground game window detected.");

    ULONGLONG startTime = GetTickCount64();
    HWND lastWindow = stableWindow;
    bool focusWasLost = false;

    while (g_running)
    {
        ULONGLONG elapsed =
            GetTickCount64() - startTime;

        if (elapsed >= g_totalRuntimeMs)
            break;

        if (
            g_maxKeyPresses > 0 &&
            g_keyPressesSent >= g_maxKeyPresses
            )
        {
            Log("MaxKeyPresses reached.");
            break;
        }

        bool allowed = true;

        HWND foreground = nullptr;
        DWORD width = 0;
        DWORD height = 0;

        if (g_onlyWhenGameForeground)
        {
            allowed = GetGameForegroundWindow(
                foreground,
                width,
                height
            );
        }

        if (!allowed)
        {
            if (!focusWasLost)
            {
                Log(
                    "Game focus/window lost. "
                    "Input paused."
                );
                focusWasLost = true;
            }

            ReleaseHeldKey();

            HWND reacquired = nullptr;

            if (!WaitForStableForeground(reacquired))
            {
                Log(
                    "Timed out waiting for the game "
                    "window to become stable again."
                );
                break;
            }

            foreground = reacquired;
            lastWindow = foreground;
            focusWasLost = false;

            Log(
                "Game foreground/window reacquired. "
                "Input resumed."
            );

            continue;
        }

        if (
            g_onlyWhenGameForeground &&
            foreground != lastWindow
            )
        {
            Log(
                "Foreground game window changed. "
                "Re-validating."
            );

            ReleaseHeldKey();

            HWND reacquired = nullptr;

            if (!WaitForStableForeground(reacquired))
            {
                Log(
                    "Timed out waiting for the new "
                    "game window to stabilize."
                );
                break;
            }

            lastWindow = reacquired;

            Log("New game window stabilized.");

            continue;
        }

        // Fast sequential burst:
        // send every currently-allowed key as its own complete press.
        // Unlike v0.1, keys are never held down together.
        for (WORD vk : g_keyboardKeys)
        {
            if (!IsKeyAllowedAtElapsed(vk, elapsed))
                continue;

            if (
                g_maxKeyPresses > 0 &&
                g_keyPressesSent >= g_maxKeyPresses
                )
            {
                break;
            }

            if (SendSingleKeyboardPress(vk))
            {
                if (g_keyPressesSent <= 12)
                {
                    Log(
                        "Sent: " +
                        KeyName(vk) +
                        " at " +
                        std::to_string(elapsed) +
                        " ms"
                    );
                }
            }
        }

        Sleep(g_pressIntervalMs);
    }

    ReleaseHeldKey();

    Log("Finished.");
    Log(
        "Key presses sent: " +
        std::to_string(g_keyPressesSent)
    );
    Log(
        "Input events sent: " +
        std::to_string(g_inputsSent)
    );

    if (g_processGuard)
    {
        ReleaseMutex(g_processGuard);
        CloseHandle(g_processGuard);
        g_processGuard = nullptr;
    }

    return 0;
}

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID
)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        HANDLE thread = CreateThread(
            nullptr,
            0,
            MainThread,
            nullptr,
            0,
            nullptr
        );

        if (thread)
            CloseHandle(thread);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        g_running = false;

        ReleaseHeldKey();
    }

    return TRUE;
}
