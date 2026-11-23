#include "pch.h"
#include "Platform/SystemIntegration.h"

#include <mmsystem.h>

#include <winrt/Microsoft.Windows.AppNotifications.h>

#pragma comment(lib, "Winmm.lib")

namespace Eikon::SystemIntegration
{
    namespace
    {
        using winrt::Windows::ApplicationModel::StartupTask;
        using winrt::Windows::ApplicationModel::StartupTaskState;

        constexpr wchar_t StartupTaskId[] = L"EikonStartup";

        bool IsEnabledState(StartupTaskState state) noexcept
        {
            return state == StartupTaskState::Enabled || state == StartupTaskState::EnabledByPolicy;
        }

        std::wstring ExeDirectory()
        {
            std::wstring path(MAX_PATH, L'\0');
            for (;;)
            {
                const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if (length == 0)
                {
                    return {};
                }
                if (length < path.size())
                {
                    path.resize(length);
                    break;
                }
                path.resize(path.size() * 2);
            }
            const auto separator = path.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring{} : path.substr(0, separator);
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> IsLaunchAtLoginEnabled()
    {
        bool enabled = false;
        try
        {
            const StartupTask task = co_await StartupTask::GetAsync(StartupTaskId);
            enabled = IsEnabledState(task.State());
        }
        catch (...)
        {
            enabled = false; // not packaged / task missing
        }
        co_return enabled;
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> SetLaunchAtLoginEnabled(bool enable)
    {
        bool enabled = false;
        try
        {
            const StartupTask task = co_await StartupTask::GetAsync(StartupTaskId);
            if (enable)
            {
                const StartupTaskState state = co_await task.RequestEnableAsync();
                enabled = IsEnabledState(state);
            }
            else
            {
                task.Disable();
                enabled = IsEnabledState(task.State());
            }
        }
        catch (...)
        {
            enabled = false;
        }
        co_return enabled;
    }

    void OpenWindowsClipboardSettings()
    {
        OpenUri(L"ms-settings:clipboard");
    }

    void OpenWindowsNotificationSettings()
    {
        OpenUri(L"ms-settings:notifications");
    }

    bool AreAppNotificationsEnabled()
    {
        try
        {
            return winrt::Microsoft::Windows::AppNotifications::AppNotificationManager::Default().Setting() ==
                winrt::Microsoft::Windows::AppNotifications::AppNotificationSetting::Enabled;
        }
        catch (...)
        {
            return false;
        }
    }

    winrt::fire_and_forget OpenUri(wchar_t const* uri)
    {
        if (!uri || !*uri)
        {
            co_return;
        }
        try
        {
            bool const launched = co_await winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri{ uri });
            if (!launched)
            {
                OutputDebugStringW(L"Eikon: Windows Launcher declined URI\n");
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: Windows Launcher failed to open URI\n");
        }
    }

    winrt::fire_and_forget OpenFile(std::wstring const& path)
    {
        try
        {
            auto const file = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(path);
            if (!co_await winrt::Windows::System::Launcher::LaunchFileAsync(file))
            {
                OutputDebugStringW(L"Eikon: Windows Launcher declined file\n");
            }
        }
        catch (...)
        {
            OutputDebugStringW(L"Eikon: Windows Launcher failed to open file\n");
        }
    }

    winrt::hstring AppVersion()
    {
        try
        {
            const auto version = winrt::Windows::ApplicationModel::Package::Current().Id().Version();
            return winrt::hstring{ std::to_wstring(version.Major) + L"." + std::to_wstring(version.Minor) + L"." + std::to_wstring(version.Build) };
        }
        catch (...)
        {
            return L"0.0.0";
        }
    }

    void PlaySoundNamed(wchar_t const* name)
    {
        try
        {
            if (!name || !*name)
            {
                return;
            }
            const std::wstring path = ExeDirectory() + L"\\Assets\\Sounds\\" + name + L".wav";
            PlaySoundW(path.c_str(), nullptr, SND_ASYNC | SND_FILENAME | SND_NODEFAULT);
        }
        catch (...)
        {
        }
    }
}
