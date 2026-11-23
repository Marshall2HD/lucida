#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

#include <string>

namespace Eikon::SystemIntegration
{
    // ---- Launch at login (MSIX StartupTask "EikonStartup") ----
    winrt::Windows::Foundation::IAsyncOperation<bool> IsLaunchAtLoginEnabled();
    // Returns the resulting state (may be false if the user disabled it in Task Manager/Settings).
    winrt::Windows::Foundation::IAsyncOperation<bool> SetLaunchAtLoginEnabled(bool enable);

    // Opens Settings > System > Clipboard (ms-settings:clipboard).
    void OpenWindowsClipboardSettings();
    // Opens Settings > System > Notifications (ms-settings:notifications).
    void OpenWindowsNotificationSettings();
    // False when Windows blocks Lucida's notifications (turned off for Lucida or for all apps, or by
    // policy): AppNotificationManager::Setting() != Enabled.
    bool AreAppNotificationsEnabled();

    // ---- Misc ----
    winrt::fire_and_forget OpenUri(wchar_t const* uri);
    // Opens a local file through the packaged Windows Launcher.
    winrt::fire_and_forget OpenFile(std::wstring const& path);
    // Package version "1.2.3" (from Package::Current().Id().Version()).
    winrt::hstring AppVersion();
    // Plays Assets\Sounds\<name>.wav asynchronously (PlaySoundW, SND_ASYNC|SND_FILENAME).
    void PlaySoundNamed(wchar_t const* name);
}
