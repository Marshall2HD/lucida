#pragma once

#include "AboutSettingsPage.g.h"

namespace winrt::Lucida::implementation
{
    struct AboutSettingsPage : AboutSettingsPageT<AboutSettingsPage>
    {
        AboutSettingsPage() = default;

        void Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);
        void NoticesButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&);

    private:
        winrt::fire_and_forget LoadNotices();
    };
}

namespace winrt::Lucida::factory_implementation
{
    struct AboutSettingsPage : AboutSettingsPageT<AboutSettingsPage, implementation::AboutSettingsPage>
    {
    };
}
