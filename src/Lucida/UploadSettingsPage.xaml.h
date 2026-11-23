#pragma once

#include "UploadSettingsPage.g.h"

#include <cstdint>
#include <string_view>
namespace winrt::Lucida::implementation
{
    // Settings > Uploading.
    struct UploadSettingsPage : UploadSettingsPageT<UploadSettingsPage>
    {
        UploadSettingsPage() = default;
        ~UploadSettingsPage();

        void Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Page_Unloaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void UploadModeCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void Field_LostFocus(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void Field_KeyDown(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
        void Service_Checked(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void TestButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void SharingCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
        void ExpirationSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&);

    private:
        void LoadValues();
        void UpdateVisibility();
        void Commit(winrt::Windows::Foundation::IInspectable const& field);
        winrt::fire_and_forget RunTest();
        void OnSettingChanged(std::wstring_view key);

        bool m_loading = false;
        bool m_testing = false;
        std::uint64_t m_settingsToken = 0;
    };
}

namespace winrt::Lucida::factory_implementation
{
    struct UploadSettingsPage : UploadSettingsPageT<UploadSettingsPage, implementation::UploadSettingsPage>
    {
    };
}
