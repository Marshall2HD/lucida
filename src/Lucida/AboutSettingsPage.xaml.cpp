#include "pch.h"
#include "AboutSettingsPage.xaml.h"
#if __has_include("AboutSettingsPage.g.cpp")
#include "AboutSettingsPage.g.cpp"
#endif

#include "Platform/SystemIntegration.h"
#include "Strings.h"

#include <winrt/Windows.Storage.h>

namespace winrt::Lucida::implementation
{
    void AboutSettingsPage::Page_Loaded(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        VersionText().Text(winrt::hstring{ ::Lucida::Strings::Format(L"Prefs_AboutVersionValue", std::wstring{ ::Lucida::SystemIntegration::AppVersion() }) });
    }

    void AboutSettingsPage::NoticesButton_Click(winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
    {
        LoadNotices();
    }

    winrt::fire_and_forget AboutSettingsPage::LoadNotices()
    {
        auto weak = get_weak();
        try
        {
            auto const file = co_await winrt::Windows::Storage::StorageFile::GetFileFromApplicationUriAsync(
                winrt::Windows::Foundation::Uri{ L"ms-appx:///Assets/THIRD_PARTY_NOTICES.md" });
            auto const text = co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
            if (auto self = weak.get())
            {
                self->NoticesText().Text(text);
                self->NoticesText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
                self->NoticesButton().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
        }
        catch (...)
        {
            if (auto self = weak.get())
            {
                self->NoticesText().Text(winrt::hstring{ ::Lucida::Strings::Get(L"Prefs_AboutNoticesUnavailable") });
                self->NoticesText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
        }
    }
}
