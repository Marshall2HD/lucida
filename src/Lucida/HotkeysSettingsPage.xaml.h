#pragma once

#include "HotkeysSettingsPage.g.h"

#include "Core/Settings.h"

#include <utility>
#include <vector>

namespace winrt::Lucida::implementation
{
    // Settings > Hotkeys.
    struct HotkeysSettingsPage : HotkeysSettingsPageT<HotkeysSettingsPage>
    {
        HotkeysSettingsPage() = default;
        ~HotkeysSettingsPage();

        void InitializeComponent();

        void Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void OpenWindowsSettingsButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        void LoadShortcuts();
        void OnShortcutChanged(winrt::Lucida::ShortcutRecorder const& sender);
        void OnSettingChanged(std::wstring_view key);

        std::vector<std::pair<::Lucida::Core::HotKey, winrt::Lucida::ShortcutRecorder>> m_recorders;
        std::uint64_t m_settingsToken = 0;
        bool m_loading = false;
    };
}

namespace winrt::Lucida::factory_implementation
{
    struct HotkeysSettingsPage : HotkeysSettingsPageT<HotkeysSettingsPage, implementation::HotkeysSettingsPage>
    {
    };
}
