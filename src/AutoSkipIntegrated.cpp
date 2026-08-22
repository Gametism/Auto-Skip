#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <fstream>
#include <string>
#include <vector>

#include "AutoSkipIntegrated.h"
#include "GameConfig.generated.h"

namespace AutoSkipIntegrated
{
    namespace
    {
        std::atomic<bool> g_running{ true };
        std::atomic<WORD> g_heldKey{ 0 };
        std::vector<WORD> g_keyboardKeys;
        DWORD g_keyPressesSent = 0;
        DWORD g_inputsSent = 0;

        std::wstring GetExePath()
        {
            std::vector<wchar_t> buffer(32768);
            const DWORD len = GetModuleFileNameW(
                nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

            if (len == 0 || len >= buffer.size())
                return {};

            return std::wstring(buffer.data(), len);
        }

        std::wstring GetExeName()
        {
            std::wstring path = GetExePath();
            const std::size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? path : path.substr(slash + 1);
        }

        std::wstring GetExeDirectory()
        {
            std::wstring path = GetExePath();
            const std::size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? L"." : path.substr(0, slash);
        }

        void Log(const std::wstring& message)
        {
            if constexpr (!GameConfig::EnableLog)
                return;

            const std::wstring path = GetExeDirectory() + L"\\NoIntro.log";
            std::wofstream stream(path, std::ios::app);

            if (!stream)
                return;

            SYSTEMTIME st{};
            GetLocalTime(&st);

            stream
                << L"["
                << st.wYear << L"-"
                << (st.wMonth < 10 ? L"0" : L"") << st.wMonth << L"-"
                << (st.wDay < 10 ? L"0" : L"") << st.wDay << L" "
                << (st.wHour < 10 ? L"0" : L"") << st.wHour << L":"
                << (st.wMinute < 10 ? L"0" : L"") << st.wMinute << L":"
                << (st.wSecond < 10 ? L"0" : L"") << st.wSecond << L"."
                << st.wMilliseconds
                << L"] [Auto-Skip] "
                << message << L"\n";
        }

        std::wstring ToUpper(std::wstring text)
        {
            std::transform(
                text.begin(),
                text.end(),
                text.begin(),
                [](wchar_t c)
                {
                    return static_cast<wchar_t>(std::towupper(c));
                });

            return text;
        }

        std::vector<std::wstring> SplitList(std::wstring text)
        {
            std::vector<std::wstring> result;
            std::size_t start = 0;

            while (start < text.size())
            {
                const std::size_t comma = text.find(L',', start);
                std::wstring item = text.substr(
                    start,
                    comma == std::wstring::npos
                        ? std::wstring::npos
                        : comma - start);

                item.erase(
                    std::remove_if(
                        item.begin(),
                        item.end(),
                        [](wchar_t c)
                        {
                            return std::iswspace(c) != 0;
                        }),
                    item.end());

                if (!item.empty())
                    result.push_back(ToUpper(item));

                if (comma == std::wstring::npos)
                    break;

                start = comma + 1;
            }

            return result;
        }

        WORD ParseKeyboardKey(const std::wstring& key)
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
                const wchar_t c = key[0];

                if ((c >= L'A' && c <= L'Z') ||
                    (c >= L'0' && c <= L'9'))
                {
                    return static_cast<WORD>(c);
                }
            }

            if (key.length() >= 2 && key[0] == L'F')
            {
                const int number = _wtoi(key.c_str() + 1);

                if (number >= 1 && number <= 24)
                    return static_cast<WORD>(VK_F1 + number - 1);
            }

            return 0;
        }

        const wchar_t* KeyName(WORD vk)
        {
            switch (vk)
            {
            case VK_SPACE:  return L"SPACE";
            case VK_RETURN: return L"ENTER";
            case VK_ESCAPE: return L"ESCAPE";
            case VK_TAB:    return L"TAB";
            case VK_BACK:   return L"BACKSPACE";
            default:        return L"KEY";
            }
        }

        bool IsConfiguredExecutable()
        {
            const std::wstring current = GetExeName();

            for (const wchar_t* configured : GameConfig::Executables)
            {
                if (configured != nullptr &&
                    *configured != L'\0' &&
                    _wcsicmp(current.c_str(), configured) == 0)
                {
                    return true;
                }
            }

            return false;
        }

        bool GetGameForegroundWindow(
            HWND& hwndOut,
            DWORD& widthOut,
            DWORD& heightOut)
        {
            HWND hwnd = GetForegroundWindow();

            if (!hwnd ||
                !IsWindow(hwnd) ||
                !IsWindowVisible(hwnd) ||
                IsIconic(hwnd))
            {
                return false;
            }

            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);

            if (pid != GetCurrentProcessId())
                return false;

            RECT rect{};
            if (!GetClientRect(hwnd, &rect))
                return false;

            const DWORD width = static_cast<DWORD>(
                std::max<LONG>(0, rect.right - rect.left));

            const DWORD height = static_cast<DWORD>(
                std::max<LONG>(0, rect.bottom - rect.top));

            if (width < GameConfig::AutoSkip::MinWindowWidth ||
                height < GameConfig::AutoSkip::MinWindowHeight)
            {
                return false;
            }

            hwndOut = hwnd;
            widthOut = width;
            heightOut = height;
            return true;
        }

        void ReleaseHeldKey()
        {
            const WORD vk = g_heldKey.exchange(0);

            if (!vk)
                return;

            INPUT up{};
            up.type = INPUT_KEYBOARD;
            up.ki.wVk = vk;
            up.ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(1, &up, sizeof(INPUT));
        }

        bool SendSingleKeyboardPress(WORD vk)
        {
            ReleaseHeldKey();

            INPUT down{};
            down.type = INPUT_KEYBOARD;
            down.ki.wVk = vk;

            const UINT sentDown = SendInput(1, &down, sizeof(INPUT));

            if (sentDown != 1)
                return false;

            g_heldKey = vk;

            if constexpr (GameConfig::AutoSkip::KeyHoldMs > 0)
                Sleep(GameConfig::AutoSkip::KeyHoldMs);

            INPUT up{};
            up.type = INPUT_KEYBOARD;
            up.ki.wVk = vk;
            up.ki.dwFlags = KEYEVENTF_KEYUP;

            const UINT sentUp = SendInput(1, &up, sizeof(INPUT));

            g_heldKey = 0;
            g_inputsSent += sentDown + sentUp;

            if (sentUp == 1)
            {
                ++g_keyPressesSent;
                return true;
            }

            return false;
        }

        bool IsKeyAllowedAtElapsed(WORD vk, ULONGLONG elapsed)
        {
            if (vk == VK_RETURN &&
                elapsed < GameConfig::AutoSkip::EnterAfterMs)
            {
                return false;
            }

            if (vk == VK_ESCAPE &&
                elapsed < GameConfig::AutoSkip::EscapeAfterMs)
            {
                return false;
            }

            return true;
        }

        bool WaitForStableForeground(HWND& stableWindowOut)
        {
            HWND currentForeground = nullptr;
            HWND lastForeground = nullptr;

            DWORD width = 0;
            DWORD height = 0;
            DWORD lastWidth = 0;
            DWORD lastHeight = 0;

            const ULONGLONG waitStart = GetTickCount64();
            ULONGLONG stableStart = 0;

            while (g_running)
            {
                bool allowed = true;

                if constexpr (GameConfig::AutoSkip::OnlyWhenGameForeground)
                {
                    allowed = GetGameForegroundWindow(
                        currentForeground,
                        width,
                        height);
                }

                if (allowed)
                {
                    if constexpr (!GameConfig::AutoSkip::OnlyWhenGameForeground)
                    {
                        stableWindowOut = nullptr;
                        return true;
                    }

                    if (currentForeground != lastForeground ||
                        width != lastWidth ||
                        height != lastHeight)
                    {
                        lastForeground = currentForeground;
                        lastWidth = width;
                        lastHeight = height;
                        stableStart = GetTickCount64();
                    }
                    else if (
                        stableStart != 0 &&
                        GetTickCount64() - stableStart >=
                            GameConfig::AutoSkip::ForegroundStableMs)
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

                if constexpr (GameConfig::AutoSkip::WaitForForegroundMs > 0)
                {
                    if (GetTickCount64() - waitStart >=
                        GameConfig::AutoSkip::WaitForForegroundMs)
                    {
                        return false;
                    }
                }

                Sleep(25);
            }

            return false;
        }

        DWORD WINAPI ThreadProc(LPVOID)
        {
            if constexpr (!GameConfig::AutoSkip::Enabled)
                return 0;

            if (!IsConfiguredExecutable())
                return 0;

            g_keyboardKeys.clear();

            for (const auto& key :
                SplitList(GameConfig::AutoSkip::KeyboardKeys))
            {
                const WORD parsed = ParseKeyboardKey(key);

                if (parsed != 0 &&
                    std::find(
                        g_keyboardKeys.begin(),
                        g_keyboardKeys.end(),
                        parsed) == g_keyboardKeys.end())
                {
                    g_keyboardKeys.push_back(parsed);
                }
            }

            if (g_keyboardKeys.empty())
            {
                Log(L"No valid KeyboardKeys configured. Disabled.");
                return 0;
            }

            Log(L"Auto-Skip v0.5 integrated and active.");

            if constexpr (GameConfig::AutoSkip::StartDelayMs > 0)
                Sleep(GameConfig::AutoSkip::StartDelayMs);

            HWND stableWindow = nullptr;

            if (!WaitForStableForeground(stableWindow))
            {
                Log(L"Timed out waiting for a stable foreground game window.");
                return 0;
            }

            const ULONGLONG startTime = GetTickCount64();
            HWND lastWindow = stableWindow;
            bool focusWasLost = false;

            while (g_running)
            {
                const ULONGLONG elapsed =
                    GetTickCount64() - startTime;

                if (elapsed >= GameConfig::AutoSkip::TotalRuntimeMs)
                    break;

                if constexpr (GameConfig::AutoSkip::MaxKeyPresses > 0)
                {
                    if (g_keyPressesSent >=
                        GameConfig::AutoSkip::MaxKeyPresses)
                    {
                        break;
                    }
                }

                bool allowed = true;
                HWND foreground = nullptr;
                DWORD width = 0;
                DWORD height = 0;

                if constexpr (GameConfig::AutoSkip::OnlyWhenGameForeground)
                {
                    allowed = GetGameForegroundWindow(
                        foreground,
                        width,
                        height);
                }

                if (!allowed)
                {
                    if (!focusWasLost)
                    {
                        Log(L"Game focus/window lost. Input paused.");
                        focusWasLost = true;
                    }

                    ReleaseHeldKey();

                    HWND reacquired = nullptr;

                    if (!WaitForStableForeground(reacquired))
                        break;

                    foreground = reacquired;
                    lastWindow = foreground;
                    focusWasLost = false;
                    Log(L"Game foreground/window reacquired.");
                    continue;
                }

                if constexpr (GameConfig::AutoSkip::OnlyWhenGameForeground)
                {
                    if (foreground != lastWindow)
                    {
                        ReleaseHeldKey();

                        HWND reacquired = nullptr;

                        if (!WaitForStableForeground(reacquired))
                            break;

                        lastWindow = reacquired;
                        continue;
                    }
                }

                for (WORD vk : g_keyboardKeys)
                {
                    if (!IsKeyAllowedAtElapsed(vk, elapsed))
                        continue;

                    if constexpr (GameConfig::AutoSkip::MaxKeyPresses > 0)
                    {
                        if (g_keyPressesSent >=
                            GameConfig::AutoSkip::MaxKeyPresses)
                        {
                            break;
                        }
                    }

                    SendSingleKeyboardPress(vk);
                }

                Sleep(GameConfig::AutoSkip::PressIntervalMs);
            }

            ReleaseHeldKey();

            Log(
                L"Finished. Key presses=" +
                std::to_wstring(g_keyPressesSent) +
                L", input events=" +
                std::to_wstring(g_inputsSent));

            return 0;
        }
    }

    void Start()
    {
        if constexpr (GameConfig::AutoSkip::Enabled)
        {
            HANDLE thread = CreateThread(
                nullptr,
                0,
                ThreadProc,
                nullptr,
                0,
                nullptr);

            if (thread != nullptr)
                CloseHandle(thread);
        }
    }
}
