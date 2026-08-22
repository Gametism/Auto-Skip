//NO-INTRO BY GAMETISM

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "GameConfig.generated.h"
#if NOINTRO_REPLACE_FEATURE
#include "EmbeddedBlankFiles.generated.h"
#endif
#if AUTOSKIP_FEATURE
#include "AutoSkipIntegrated.h"
#endif

namespace fs = std::filesystem;

namespace
{
#if NOINTRO_FEATURE
    struct ManagedFile
    {
        fs::path original;
        fs::path backup;
#if NOINTRO_REPLACE_FEATURE
        const EmbeddedBlank::BlankFile* blank;
#endif
    };

    std::mutex g_logMutex;

    fs::path GetExePath()
    {
        std::vector<wchar_t> buffer(32768);
        const DWORD len = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

        if (len == 0 || len >= buffer.size())
            return {};

        return fs::path(std::wstring(buffer.data(), len));
    }

    void Log(const std::wstring& message)
    {
        if constexpr (!GameConfig::EnableLog)
            return;

        const fs::path exe = GetExePath();
        const fs::path logPath =
            exe.empty() ? fs::path(L"NoIntro.log")
                        : exe.parent_path() / L"NoIntro.log";

        std::lock_guard<std::mutex> lock(g_logMutex);
        std::wofstream stream(logPath, std::ios::app);
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
            << L"] "
            << message << L"\n";
    }

    bool Exists(const fs::path& path)
    {
        const DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES &&
               (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool EqualsInsensitive(const std::wstring& a, const std::wstring& b)
    {
        return _wcsicmp(a.c_str(), b.c_str()) == 0;
    }

    bool IsConfiguredExecutable(const fs::path& exePath)
    {
        const std::wstring current = exePath.filename().wstring();

        for (const wchar_t* configured : GameConfig::Executables)
        {
            if (configured != nullptr &&
                *configured != L'\0' &&
                EqualsInsensitive(current, configured))
            {
                Log(L"EXE match: " + current);
                return true;
            }
        }

        Log(L"EXE mismatch: " + current);
        return false;
    }

    fs::path ResolveTarget(
        const fs::path& exeDirectory,
        const wchar_t* configuredPath)
    {
        fs::path path(configuredPath);

        if (!path.is_absolute())
            path = exeDirectory / path;

        return path.lexically_normal();
    }

#if NOINTRO_REPLACE_FEATURE
    const EmbeddedBlank::BlankFile* FindBlankForTarget(const fs::path& target)
    {
        const std::wstring ext = target.extension().wstring();

        for (std::size_t i = 0; i < EmbeddedBlank::FileCount; ++i)
        {
            const auto& blank = EmbeddedBlank::Files[i];

            if (blank.extension != nullptr &&
                EqualsInsensitive(ext, blank.extension))
            {
                return &blank;
            }
        }

        return nullptr;
    }
#endif

    bool MoveFileSafe(const fs::path& from, const fs::path& to)
    {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE)
        {
            Log(L"Move OK: " + from.wstring() + L" -> " + to.wstring());
            return true;
        }

        Log(L"Move FAILED: " + from.wstring() + L" -> " + to.wstring() +
            L" | Win32=" + std::to_wstring(GetLastError()));
        return false;
    }

    bool DeleteFileSafe(const fs::path& path)
    {
        if (!Exists(path))
            return true;

        if (DeleteFileW(path.c_str()) != FALSE)
        {
            Log(L"Delete OK: " + path.wstring());
            return true;
        }

        Log(L"Delete FAILED: " + path.wstring() +
            L" | Win32=" + std::to_wstring(GetLastError()));
        return false;
    }

#if NOINTRO_REPLACE_FEATURE
    bool WriteBlank(
        const fs::path& path,
        const EmbeddedBlank::BlankFile& blank)
    {
        HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
        {
            Log(L"Create blank FAILED: " + path.wstring() +
                L" | Win32=" + std::to_wstring(GetLastError()));
            return false;
        }

        std::size_t total = 0;
        bool success = true;

        while (total < blank.size)
        {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(
                (blank.size - total) > 0xFFFFFFFFull
                    ? 0xFFFFFFFFull
                    : (blank.size - total));

            if (WriteFile(
                file,
                blank.data + total,
                chunk,
                &written,
                nullptr) == FALSE || written == 0)
            {
                success = false;
                Log(L"Write blank FAILED: " + path.wstring() +
                    L" | Win32=" + std::to_wstring(GetLastError()));
                break;
            }

            total += written;
        }

        FlushFileBuffers(file);
        CloseHandle(file);

        if (!success)
        {
            DeleteFileSafe(path);
            return false;
        }

        Log(L"Embedded blank written: " + path.wstring() +
            L" | format=" + std::wstring(blank.extension) +
            L" | bytes=" + std::to_wstring(blank.size));
        return true;
    }

#endif

#if NOINTRO_REPLACE_FEATURE
    bool FileMatchesBlank(
        const fs::path& path,
        const EmbeddedBlank::BlankFile& blank)
    {
        HANDLE file = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return false;

        LARGE_INTEGER size{};
        if (GetFileSizeEx(file, &size) == FALSE ||
            size.QuadPart < 0 ||
            static_cast<unsigned long long>(size.QuadPart) !=
                static_cast<unsigned long long>(blank.size))
        {
            CloseHandle(file);
            return false;
        }

        std::vector<std::uint8_t> data(blank.size);
        std::size_t total = 0;
        bool success = true;

        while (total < data.size())
        {
            DWORD read = 0;
            const DWORD chunk = static_cast<DWORD>(
                (data.size() - total) > 0xFFFFFFFFull
                    ? 0xFFFFFFFFull
                    : (data.size() - total));

            if (ReadFile(
                file,
                data.data() + total,
                chunk,
                &read,
                nullptr) == FALSE || read == 0)
            {
                success = false;
                break;
            }

            total += read;
        }

        CloseHandle(file);

        if (!success)
            return false;

        for (std::size_t i = 0; i < blank.size; ++i)
        {
            if (data[i] != blank.data[i])
                return false;
        }

        return true;
    }

#endif

#if NOINTRO_REPLACE_FEATURE
    void RecoverReplace(
        const fs::path& original,
        const fs::path& backup,
        const EmbeddedBlank::BlankFile& blank)
    {
        if (!Exists(backup))
            return;

        if (!Exists(original))
        {
            Log(L"Recovery: target missing; restoring backup.");
            MoveFileSafe(backup, original);
            return;
        }

        if (FileMatchesBlank(original, blank))
        {
            Log(L"Recovery: our embedded blank is still installed; restoring backup.");
            if (DeleteFileSafe(original))
                MoveFileSafe(backup, original);
            return;
        }

        Log(L"Recovery: target was externally restored/updated; preserving it.");
        DeleteFileSafe(backup);
    }

#endif

    void RecoverRename(
        const fs::path& original,
        const fs::path& backup)
    {
        if (!Exists(backup))
            return;

        if (!Exists(original))
        {
            MoveFileSafe(backup, original);
            return;
        }

        DeleteFileSafe(backup);
    }

#if NOINTRO_REPLACE_FEATURE
    bool ApplyReplace(
        const fs::path& original,
        const fs::path& backup,
        const EmbeddedBlank::BlankFile& blank)
    {
        RecoverReplace(original, backup, blank);

        if (!Exists(original))
        {
            Log(L"TARGET NOT FOUND: " + original.wstring());
            return false;
        }

        if (Exists(backup))
        {
            Log(L"Backup still exists; refusing to overwrite: " + backup.wstring());
            return false;
        }

        if (!MoveFileSafe(original, backup))
            return false;

        if (!WriteBlank(original, blank))
        {
            Log(L"Blank write failed; restoring original immediately.");
            MoveFileSafe(backup, original);
            return false;
        }

        return true;
    }

#endif

    bool ApplyRename(
        const fs::path& original,
        const fs::path& backup)
    {
        RecoverRename(original, backup);

        if (!Exists(original))
        {
            Log(L"TARGET NOT FOUND: " + original.wstring());
            return false;
        }

        if (Exists(backup))
        {
            Log(L"Backup still exists; refusing to overwrite: " + backup.wstring());
            return false;
        }

        return MoveFileSafe(original, backup);
    }

#if NOINTRO_REPLACE_FEATURE
    void RestoreReplace(const ManagedFile& item)
    {
        if (!Exists(item.backup))
            return;

        if (!Exists(item.original))
        {
            MoveFileSafe(item.backup, item.original);
            return;
        }

        if (item.blank != nullptr &&
            FileMatchesBlank(item.original, *item.blank))
        {
            if (DeleteFileSafe(item.original))
                MoveFileSafe(item.backup, item.original);
            return;
        }

        Log(L"Restore: target changed externally; preserving current file.");
        DeleteFileSafe(item.backup);
    }

#endif

    void RestoreRename(const ManagedFile& item)
    {
        if (!Exists(item.backup))
            return;

        if (Exists(item.original))
        {
            DeleteFileSafe(item.backup);
            return;
        }

        MoveFileSafe(item.backup, item.original);
    }

    DWORD WINAPI WorkerThread(LPVOID)
    {
        if constexpr (!GameConfig::NoIntroEnabled)
        {
            return 0;
        }
        else
        {
            Log(L"========== NoIntro startup ==========");

            const fs::path exePath = GetExePath();

            if (exePath.empty() || !IsConfiguredExecutable(exePath))
                return 0;

        if constexpr (GameConfig::StartDelayMilliseconds > 0)
            Sleep(GameConfig::StartDelayMilliseconds);

        const fs::path exeDirectory = exePath.parent_path();
        std::vector<ManagedFile> managed;
        managed.reserve(std::size(GameConfig::Files));

        if constexpr (GameConfig::ReplaceMode)
            Log(L"Mode: Replace");
        else
            Log(L"Mode: Rename");

        for (const wchar_t* configuredFile : GameConfig::Files)
        {
            if (configuredFile == nullptr || *configuredFile == L'\0')
                continue;

            const fs::path original =
                ResolveTarget(exeDirectory, configuredFile);

            fs::path backup = original;
            backup += GameConfig::BackupSuffix;

#if NOINTRO_REPLACE_FEATURE
            const auto* blank = FindBlankForTarget(original);

            if (blank == nullptr)
            {
                Log(L"NO EMBEDDED BLANK FOR EXTENSION: " +
                    original.extension().wstring() +
                    L" | target skipped: " + original.wstring());
                continue;
            }

            if (ApplyReplace(original, backup, *blank))
                managed.push_back({ original, backup, blank });
#else
            if (ApplyRename(original, backup))
                managed.push_back({ original, backup });
#endif
        }

        Log(L"Managed target count: " + std::to_wstring(managed.size()));

        if (managed.empty())
            return 0;

        if constexpr (GameConfig::RestoreDelaySeconds > 0)
        {
            const auto endTime =
                std::chrono::steady_clock::now() +
                std::chrono::seconds(GameConfig::RestoreDelaySeconds);

            while (std::chrono::steady_clock::now() < endTime)
            {
                const auto remaining =
                    endTime - std::chrono::steady_clock::now();

                const auto maxSleep =
                    std::chrono::steady_clock::duration(
                        std::chrono::milliseconds(250));

                std::this_thread::sleep_for(
                    (remaining < maxSleep) ? remaining : maxSleep);
            }

            for (auto it = managed.rbegin(); it != managed.rend(); ++it)
            {
#if NOINTRO_REPLACE_FEATURE
                RestoreReplace(*it);
#else
                RestoreRename(*it);
#endif
            }

            Log(L"Timed restoration complete.");
        }

            return 0;
        }
    }
#endif // NOINTRO_FEATURE
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);

#if NOINTRO_FEATURE
        HANDLE thread = CreateThread(
            nullptr, 0, WorkerThread, nullptr, 0, nullptr);

        if (thread != nullptr)
            CloseHandle(thread);
#endif

#if AUTOSKIP_FEATURE
        AutoSkipIntegrated::Start();
#endif
    }

    return TRUE;
}
