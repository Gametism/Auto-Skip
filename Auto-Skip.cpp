// Auto-Skip.cpp //
// Auto-Skip by Gametism //
// Version 0.3 //

#include <windows.h>
#include <atomic>
#include <string>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <cstdint>

#define AUTOSKIP_NAME    "Auto-Skip"
#define AUTOSKIP_VERSION "0.3"
#define AUTOSKIP_AUTHOR  "Gametism"

static std::atomic<bool> g_running = true;

static DWORD g_startDelayMs = 0;
static DWORD g_totalRuntimeMs = 4000;
static DWORD g_fastBurstDurationMs = 1000;
static DWORD g_fastIntervalMs = 5;
static DWORD g_fallbackIntervalMs = 20;
static DWORD g_keyHoldMs = 0;

static DWORD g_foregroundStableMs = 25;
static DWORD g_waitForForegroundMs = 15000;

static DWORD g_minWindowWidth = 640;
static DWORD g_minWindowHeight = 360;

static DWORD g_enterAfterMs = 0;
static DWORD g_escapeAfterMs = 0;
static DWORD g_mouseAfterMs = 0;

static DWORD g_maxInputBursts = 0;
static bool g_onlyWhenGameForeground = true;

static std::vector<WORD> g_keyboardKeys;

enum class MouseButton
{
    Left,
    Right,
    Middle,
    X1,
    X2
};

static std::vector<MouseButton> g_mouseButtons;

static std::wstring g_modulePath;
static std::wstring g_iniPath;
static std::wstring g_logPath;
static std::wstring g_exePath;
static std::wstring g_exeName;

static DWORD g_burstsSent = 0;
static DWORD g_keyboardPressesSent = 0;
static DWORD g_mouseClicksSent = 0;
static DWORD g_inputEventsSent = 0;
static DWORD g_sendInputFailures = 0;

static HANDLE g_processGuard = nullptr;

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
    if (key == L"SPACE" || key == L"SPACEBAR") return VK_SPACE;
    if (key == L"ENTER" || key == L"RETURN") return VK_RETURN;
    if (key == L"ESC" || key == L"ESCAPE") return VK_ESCAPE;
    if (key == L"TAB") return VK_TAB;
    if (key == L"BACKSPACE") return VK_BACK;
    if (key == L"SHIFT") return VK_SHIFT;
    if (key == L"CTRL" || key == L"CONTROL") return VK_CONTROL;
    if (key == L"ALT") return VK_MENU;
    if (key == L"UP") return VK_UP;
    if (key == L"DOWN") return VK_DOWN;
    if (key == L"LEFT") return VK_LEFT;
    if (key == L"RIGHT") return VK_RIGHT;

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

static bool ParseMouseButton(
    const std::wstring& text,
    MouseButton& buttonOut
)
{
    if (text == L"LEFT" || text == L"LMB")
    {
        buttonOut = MouseButton::Left;
        return true;
    }

    if (text == L"RIGHT" || text == L"RMB")
    {
        buttonOut = MouseButton::Right;
        return true;
    }

    if (text == L"MIDDLE" || text == L"MMB")
    {
        buttonOut = MouseButton::Middle;
        return true;
    }

    if (text == L"X1" || text == L"MOUSE4")
    {
        buttonOut = MouseButton::X1;
        return true;
    }

    if (text == L"X2" || text == L"MOUSE5")
    {
        buttonOut = MouseButton::X2;
        return true;
    }

    return false;
}

static std::string KeyName(WORD vk)
{
    switch (vk)
    {
    case VK_SPACE: return "SPACE";
    case VK_RETURN: return "ENTER";
    case VK_ESCAPE: return "ESCAPE";
    case VK_TAB: return "TAB";
    case VK_BACK: return "BACKSPACE";
    case VK_SHIFT: return "SHIFT";
    case VK_CONTROL: return "CTRL";
    case VK_MENU: return "ALT";
    case VK_UP: return "UP";
    case VK_DOWN: return "DOWN";
    case VK_LEFT: return "LEFT";
    case VK_RIGHT: return "RIGHT";
    }

    if (vk >= 'A' && vk <= 'Z')
        return std::string(1, static_cast<char>(vk));

    if (vk >= '0' && vk <= '9')
        return std::string(1, static_cast<char>(vk));

    if (vk >= VK_F1 && vk <= VK_F24)
        return "F" + std::to_string(vk - VK_F1 + 1);

    return "VK_" + std::to_string(vk);
}

static std::string MouseButtonName(MouseButton button)
{
    switch (button)
    {
    case MouseButton::Left: return "LEFT";
    case MouseButton::Right: return "RIGHT";
    case MouseButton::Middle: return "MIDDLE";
    case MouseButton::X1: return "X1";
    case MouseButton::X2: return "X2";
    }

    return "UNKNOWN";
}

static void RemoveDuplicateKeys()
{
    std::vector<WORD> unique;

    for (WORD key : g_keyboardKeys)
    {
        if (std::find(unique.begin(), unique.end(), key) == unique.end())
            unique.push_back(key);
    }

    g_keyboardKeys.swap(unique);
}

static void RemoveDuplicateMouseButtons()
{
    std::vector<MouseButton> unique;

    for (MouseButton button : g_mouseButtons)
    {
        if (std::find(unique.begin(), unique.end(), button) == unique.end())
            unique.push_back(button);
    }

    g_mouseButtons.swap(unique);
}

static bool GetGameForegroundWindow(
    HWND& hwndOut,
    DWORD& widthOut,
    DWORD& heightOut
)
{
    HWND hwnd = GetForegroundWindow();

    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid != GetCurrentProcessId())
        return false;

    RECT rect{};

    if (!GetClientRect(hwnd, &rect))
        return false;

    DWORD width = static_cast<DWORD>(std::max<LONG>(0, rect.right - rect.left));
    DWORD height = static_cast<DWORD>(std::max<LONG>(0, rect.bottom - rect.top));

    if (width < g_minWindowWidth || height < g_minWindowHeight)
        return false;

    hwndOut = hwnd;
    widthOut = width;
    heightOut = height;
    return true;
}

static bool IsKeyAllowedAtElapsed(WORD vk, ULONGLONG elapsed)
{
    if (vk == VK_RETURN && elapsed < g_enterAfterMs)
        return false;

    if (vk == VK_ESCAPE && elapsed < g_escapeAfterMs)
        return false;

    return true;
}

static void AppendKeyboardPress(std::vector<INPUT>& inputs, WORD vk)
{
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = vk;
    inputs.push_back(down);

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_KEYUP;
    inputs.push_back(up);
}

static void AppendMouseClick(std::vector<INPUT>& inputs, MouseButton button)
{
    INPUT down{};
    INPUT up{};
    down.type = INPUT_MOUSE;
    up.type = INPUT_MOUSE;

    switch (button)
    {
    case MouseButton::Left:
        down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        break;

    case MouseButton::Right:
        down.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        up.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        break;

    case MouseButton::Middle:
        down.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
        up.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
        break;

    case MouseButton::X1:
        down.mi.dwFlags = MOUSEEVENTF_XDOWN;
        up.mi.dwFlags = MOUSEEVENTF_XUP;
        down.mi.mouseData = XBUTTON1;
        up.mi.mouseData = XBUTTON1;
        break;

    case MouseButton::X2:
        down.mi.dwFlags = MOUSEEVENTF_XDOWN;
        up.mi.dwFlags = MOUSEEVENTF_XUP;
        down.mi.mouseData = XBUTTON2;
        up.mi.mouseData = XBUTTON2;
        break;
    }

    inputs.push_back(down);
    inputs.push_back(up);
}

static bool SendFastInputBurst(ULONGLONG elapsed)
{
    std::vector<INPUT> inputs;
    DWORD keyboardPresses = 0;
    DWORD mouseClicks = 0;

    inputs.reserve((g_keyboardKeys.size() + g_mouseButtons.size()) * 2);

    for (WORD vk : g_keyboardKeys)
    {
        if (!IsKeyAllowedAtElapsed(vk, elapsed))
            continue;

        AppendKeyboardPress(inputs, vk);
        keyboardPresses++;
    }

    if (elapsed >= g_mouseAfterMs)
    {
        for (MouseButton button : g_mouseButtons)
        {
            AppendMouseClick(inputs, button);
            mouseClicks++;
        }
    }

    if (inputs.empty())
        return false;

    // Keep each burst bounded even if a user manually configures a huge list.
    constexpr size_t MAX_EVENTS_PER_BURST = 32;
    if (inputs.size() > MAX_EVENTS_PER_BURST)
        inputs.resize(MAX_EVENTS_PER_BURST);

    SetLastError(ERROR_SUCCESS);

    UINT sent = SendInput(
        static_cast<UINT>(inputs.size()),
        inputs.data(),
        sizeof(INPUT)
    );

    g_inputEventsSent += sent;

    if (sent != inputs.size())
    {
        g_sendInputFailures++;

        if (g_sendInputFailures <= 5)
        {
            Log(
                "SendInput partial/failed burst. Sent " +
                std::to_string(sent) + "/" +
                std::to_string(inputs.size()) +
                ", GetLastError=" +
                std::to_string(GetLastError())
            );
        }

        return false;
    }

    g_burstsSent++;
    g_keyboardPressesSent += keyboardPresses;
    g_mouseClicksSent += mouseClicks;
    return true;
}

static bool SendHeldKeyboardSequence(ULONGLONG elapsed)
{
    bool sentAny = false;

    for (WORD vk : g_keyboardKeys)
    {
        if (!IsKeyAllowedAtElapsed(vk, elapsed))
            continue;

        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = vk;

        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_KEYUP;

        if (SendInput(1, &down, sizeof(INPUT)) != 1)
        {
            g_sendInputFailures++;
            continue;
        }

        g_inputEventsSent++;
        Sleep(g_keyHoldMs);

        if (SendInput(1, &up, sizeof(INPUT)) == 1)
        {
            g_inputEventsSent++;
            g_keyboardPressesSent++;
            sentAny = true;
        }
    }

    if (elapsed >= g_mouseAfterMs && !g_mouseButtons.empty())
    {
        std::vector<INPUT> mouseInputs;
        mouseInputs.reserve(g_mouseButtons.size() * 2);

        for (MouseButton button : g_mouseButtons)
            AppendMouseClick(mouseInputs, button);

        if (!mouseInputs.empty())
        {
            UINT sent = SendInput(
                static_cast<UINT>(mouseInputs.size()),
                mouseInputs.data(),
                sizeof(INPUT)
            );

            g_inputEventsSent += sent;

            if (sent == mouseInputs.size())
            {
                g_mouseClicksSent += static_cast<DWORD>(g_mouseButtons.size());
                sentAny = true;
            }
            else
            {
                g_sendInputFailures++;
            }
        }
    }

    if (sentAny)
        g_burstsSent++;

    return sentAny;
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
    g_totalRuntimeMs = GetPrivateProfileIntW(L"AutoSkip", L"TotalRuntimeMs", 4000, g_iniPath.c_str());
    g_fastBurstDurationMs = GetPrivateProfileIntW(L"AutoSkip", L"FastBurstDurationMs", 1000, g_iniPath.c_str());
    g_fastIntervalMs = GetPrivateProfileIntW(L"AutoSkip", L"FastIntervalMs", 5, g_iniPath.c_str());
    g_fallbackIntervalMs = GetPrivateProfileIntW(L"AutoSkip", L"FallbackIntervalMs", 20, g_iniPath.c_str());
    g_keyHoldMs = GetPrivateProfileIntW(L"AutoSkip", L"KeyHoldMs", 0, g_iniPath.c_str());

    // Backward compatibility with v0.2: if PressIntervalMs exists and the new
    // interval settings are absent, use it for the fallback phase.
    wchar_t legacyBuffer[32]{};
    GetPrivateProfileStringW(
        L"AutoSkip",
        L"PressIntervalMs",
        L"",
        legacyBuffer,
        static_cast<DWORD>(std::size(legacyBuffer)),
        g_iniPath.c_str()
    );

    if (legacyBuffer[0] != L'\0')
    {
        wchar_t fastBuffer[32]{};
        wchar_t fallbackBuffer[32]{};

        GetPrivateProfileStringW(L"AutoSkip", L"FastIntervalMs", L"", fastBuffer, static_cast<DWORD>(std::size(fastBuffer)), g_iniPath.c_str());
        GetPrivateProfileStringW(L"AutoSkip", L"FallbackIntervalMs", L"", fallbackBuffer, static_cast<DWORD>(std::size(fallbackBuffer)), g_iniPath.c_str());

        DWORD legacy = static_cast<DWORD>(_wtoi(legacyBuffer));

        if (fastBuffer[0] == L'\0')
            g_fastIntervalMs = legacy;

        if (fallbackBuffer[0] == L'\0')
            g_fallbackIntervalMs = legacy;
    }

    g_foregroundStableMs = GetPrivateProfileIntW(L"AutoSkip", L"ForegroundStableMs", 25, g_iniPath.c_str());
    g_waitForForegroundMs = GetPrivateProfileIntW(L"AutoSkip", L"WaitForForegroundMs", 15000, g_iniPath.c_str());
    g_minWindowWidth = GetPrivateProfileIntW(L"AutoSkip", L"MinWindowWidth", 640, g_iniPath.c_str());
    g_minWindowHeight = GetPrivateProfileIntW(L"AutoSkip", L"MinWindowHeight", 360, g_iniPath.c_str());
    g_enterAfterMs = GetPrivateProfileIntW(L"AutoSkip", L"EnterAfterMs", 0, g_iniPath.c_str());
    g_escapeAfterMs = GetPrivateProfileIntW(L"AutoSkip", L"EscapeAfterMs", 0, g_iniPath.c_str());
    g_mouseAfterMs = GetPrivateProfileIntW(L"AutoSkip", L"MouseAfterMs", 0, g_iniPath.c_str());
    g_maxInputBursts = GetPrivateProfileIntW(L"AutoSkip", L"MaxInputBursts", 0, g_iniPath.c_str());
    g_onlyWhenGameForeground = GetPrivateProfileIntW(L"AutoSkip", L"OnlyWhenGameForeground", 1, g_iniPath.c_str()) != 0;

    g_keyboardKeys.clear();

    for (const auto& key : SplitList(ReadIniString(L"KeyboardKeys", L"SPACE,ENTER,ESCAPE")))
    {
        WORD parsed = ParseKeyboardKey(key);
        if (parsed)
            g_keyboardKeys.push_back(parsed);
    }

    RemoveDuplicateKeys();

    g_mouseButtons.clear();

    for (const auto& buttonName : SplitList(ReadIniString(L"MouseButtons", L"LEFT")))
    {
        MouseButton button{};
        if (ParseMouseButton(buttonName, button))
            g_mouseButtons.push_back(button);
    }

    RemoveDuplicateMouseButtons();

    // Stability clamps. Auto-Skip is designed to be fast, not an unbounded input flood.
    if (g_fastIntervalMs < 5)
        g_fastIntervalMs = 5;

    if (g_fallbackIntervalMs < 10)
        g_fallbackIntervalMs = 10;

    if (g_keyHoldMs > 250)
        g_keyHoldMs = 250;

    if (g_totalRuntimeMs < 1)
        g_totalRuntimeMs = 1;

    if (g_fastBurstDurationMs > g_totalRuntimeMs)
        g_fastBurstDurationMs = g_totalRuntimeMs;

    if (g_foregroundStableMs > 5000)
        g_foregroundStableMs = 5000;
}

static HANDLE CreateGlobalGuard()
{
    std::wstring guardKey = ToUpper(g_exePath);

    for (wchar_t& c : guardKey)
    {
        if (c == L'\\' || c == L'/' || c == L':' || c == L' ' || c == L'.')
            c = L'_';
    }

    std::wstring mutexName = L"Local\\Gametism_AutoSkip_" + guardKey;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());

    if (!mutex)
        return nullptr;

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);
        return nullptr;
    }

    return mutex;
}

static bool WaitForStableForeground(HWND& stableWindowOut)
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
            allowed = GetGameForegroundWindow(currentForeground, width, height);

        if (allowed)
        {
            if (!g_onlyWhenGameForeground)
            {
                stableWindowOut = nullptr;
                return true;
            }

            if (currentForeground != lastForeground || width != lastWidth || height != lastHeight)
            {
                lastForeground = currentForeground;
                lastWidth = width;
                lastHeight = height;
                stableStart = GetTickCount64();
            }
            else if (stableStart != 0 && GetTickCount64() - stableStart >= g_foregroundStableMs)
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

        if (g_waitForForegroundMs > 0 && GetTickCount64() - waitStart >= g_waitForForegroundMs)
            return false;

        Sleep(10);
    }

    return false;
}

static void PreciseWaitMs(DWORD milliseconds)
{
    if (milliseconds == 0)
        return;

    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};

    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&start))
    {
        Sleep(milliseconds);
        return;
    }

    const double targetSeconds = static_cast<double>(milliseconds) / 1000.0;

    while (g_running)
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);

        double elapsedSeconds =
            static_cast<double>(now.QuadPart - start.QuadPart) /
            static_cast<double>(frequency.QuadPart);

        double remainingMs = (targetSeconds - elapsedSeconds) * 1000.0;

        if (remainingMs <= 0.0)
            break;

        if (remainingMs > 2.0)
            Sleep(1);
        else
            SwitchToThread();
    }
}

static void CleanupGuard()
{
    if (!g_processGuard)
        return;

    ReleaseMutex(g_processGuard);
    CloseHandle(g_processGuard);
    g_processGuard = nullptr;
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

    g_processGuard = CreateGlobalGuard();

    if (!g_processGuard)
    {
        Log("Another Auto-Skip instance is already active for this game executable.");
        return 0;
    }

    if (g_keyboardKeys.empty() && g_mouseButtons.empty())
    {
        Log("No valid KeyboardKeys or MouseButtons configured. Auto-Skip disabled.");
        CleanupGuard();
        return 0;
    }

    Log("Auto-Skip active.");
    Log("Mode: low-latency bounded input burst.");
    Log("Fast burst duration: " + std::to_string(g_fastBurstDurationMs) + " ms");
    Log("Fast interval: " + std::to_string(g_fastIntervalMs) + " ms");
    Log("Fallback interval: " + std::to_string(g_fallbackIntervalMs) + " ms");
    Log("Total runtime: " + std::to_string(g_totalRuntimeMs) + " ms");
    Log("Minimum window size: " + std::to_string(g_minWindowWidth) + "x" + std::to_string(g_minWindowHeight));

    std::string keys = "Keyboard: ";
    if (g_keyboardKeys.empty())
    {
        keys += "disabled";
    }
    else
    {
        for (size_t i = 0; i < g_keyboardKeys.size(); ++i)
        {
            if (i > 0) keys += ",";
            keys += KeyName(g_keyboardKeys[i]);
        }
    }
    Log(keys);

    std::string mouse = "Mouse: ";
    if (g_mouseButtons.empty())
    {
        mouse += "disabled";
    }
    else
    {
        for (size_t i = 0; i < g_mouseButtons.size(); ++i)
        {
            if (i > 0) mouse += ",";
            mouse += MouseButtonName(g_mouseButtons[i]);
        }
    }
    Log(mouse);

    if (g_startDelayMs > 0)
        Sleep(g_startDelayMs);

    HWND stableWindow = nullptr;

    if (!WaitForStableForeground(stableWindow))
    {
        Log("Timed out waiting for a stable foreground game window.");
        CleanupGuard();
        return 0;
    }

    Log("Stable foreground game window detected.");

    ULONGLONG startTime = GetTickCount64();
    HWND lastWindow = stableWindow;
    bool focusWasLost = false;
    bool loggedFallback = false;

    while (g_running)
    {
        ULONGLONG elapsed = GetTickCount64() - startTime;

        if (elapsed >= g_totalRuntimeMs)
            break;

        if (g_maxInputBursts > 0 && g_burstsSent >= g_maxInputBursts)
        {
            Log("MaxInputBursts reached.");
            break;
        }

        bool allowed = true;
        HWND foreground = nullptr;
        DWORD width = 0;
        DWORD height = 0;

        if (g_onlyWhenGameForeground)
            allowed = GetGameForegroundWindow(foreground, width, height);

        if (!allowed)
        {
            if (!focusWasLost)
            {
                Log("Game focus/window lost. Input paused.");
                focusWasLost = true;
            }

            HWND reacquired = nullptr;

            if (!WaitForStableForeground(reacquired))
            {
                Log("Timed out waiting for the game window to become stable again.");
                break;
            }

            lastWindow = reacquired;
            focusWasLost = false;
            Log("Game foreground/window reacquired. Input resumed.");
            continue;
        }

        if (g_onlyWhenGameForeground && foreground != lastWindow)
        {
            Log("Foreground game window changed. Re-validating.");

            HWND reacquired = nullptr;

            if (!WaitForStableForeground(reacquired))
            {
                Log("Timed out waiting for the new game window to stabilize.");
                break;
            }

            lastWindow = reacquired;
            Log("New game window stabilized.");
            continue;
        }

        bool sent = false;

        if (g_keyHoldMs == 0)
            sent = SendFastInputBurst(elapsed);
        else
            sent = SendHeldKeyboardSequence(elapsed);

        if (sent && g_burstsSent <= 8)
        {
            Log(
                "Burst #" +
                std::to_string(g_burstsSent) +
                " at " +
                std::to_string(elapsed) +
                " ms"
            );
        }

        DWORD interval = g_fastIntervalMs;

        if (elapsed >= g_fastBurstDurationMs)
        {
            interval = g_fallbackIntervalMs;

            if (!loggedFallback)
            {
                Log("Fast burst complete. Entering fallback phase.");
                loggedFallback = true;
            }
        }

        PreciseWaitMs(interval);
    }

    Log("Finished.");
    Log("Bursts sent: " + std::to_string(g_burstsSent));
    Log("Keyboard presses sent: " + std::to_string(g_keyboardPressesSent));
    Log("Mouse clicks sent: " + std::to_string(g_mouseClicksSent));
    Log("Input events sent: " + std::to_string(g_inputEventsSent));
    Log("SendInput failures: " + std::to_string(g_sendInputFailures));

    CleanupGuard();
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
        // Keep DLL detach minimal. No SendInput or other loader-sensitive work here.
        g_running = false;
    }

    return TRUE;
}
