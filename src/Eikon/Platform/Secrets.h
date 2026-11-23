#pragma once

// Secrets are a small string dictionary that must not sit in plain settings. They are stored as JSON encrypted with DPAPI (CryptProtectData, current user) in
// LocalState\secrets.dat and loaded lazily.  Plaintext values remain in SecureString storage only.
// UI thread only.

#include "Core/Event.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Eikon
{
    class SecureBuffer
    {
    public:
        SecureBuffer() = default;
        explicit SecureBuffer(std::string value) : m_value(value.begin(), value.end())
        {
            if (!value.empty()) SecureZeroMemory(value.data(), value.size());
        }
        SecureBuffer(void const* data, std::size_t size)
        {
            if (data && size != 0)
            {
                auto const* first = static_cast<std::uint8_t const*>(data);
                m_value.assign(first, first + size);
            }
        }
        ~SecureBuffer() { Wipe(); }
        SecureBuffer(SecureBuffer const&) = delete;
        SecureBuffer& operator=(SecureBuffer const&) = delete;
        SecureBuffer(SecureBuffer&& other) noexcept : m_value(std::move(other.m_value)) { other.Wipe(); }
        SecureBuffer& operator=(SecureBuffer&& other) noexcept
        {
            if (this != &other)
            {
                Wipe();
                m_value = std::move(other.m_value);
                other.Wipe();
            }
            return *this;
        }

        char const* data() const noexcept { return reinterpret_cast<char const*>(m_value.data()); }
        char* data() noexcept { return reinterpret_cast<char*>(m_value.data()); }
        std::size_t size() const noexcept { return m_value.size(); }
        void Wipe() noexcept
        {
            if (m_value.capacity() != 0) SecureZeroMemory(m_value.data(), m_value.capacity());
            m_value.clear();
        }

    private:
        std::vector<std::uint8_t> m_value;
    };

    class SecureString
    {
    public:
        SecureString() = default;
        explicit SecureString(std::wstring value) : m_value(std::move(value)) {}
        ~SecureString() { Wipe(); }
        SecureString(SecureString const&) = delete;
        SecureString& operator=(SecureString const&) = delete;
        SecureString(SecureString&& other) noexcept : m_value(std::move(other.m_value)) { other.Wipe(); }
        SecureString& operator=(SecureString&& other) noexcept
        {
            if (this != &other)
            {
                Wipe();
                m_value = std::move(other.m_value);
                other.Wipe();
            }
            return *this;
        }

        std::wstring_view View() const noexcept { return m_value; }
        bool empty() const noexcept { return m_value.empty(); }
        void Wipe() noexcept
        {
            if (m_value.capacity() != 0) SecureZeroMemory(m_value.data(), m_value.capacity() * sizeof(wchar_t));
            m_value.clear();
        }

    private:
        std::wstring m_value;
    };

    class Secrets
    {
    public:
        static Secrets& Current();

        static constexpr std::wstring_view kAllowDeeplinks = L"allowDeeplinks";
        static constexpr std::wstring_view kS3Access = L"s3access";
        static constexpr std::wstring_view kS3Secret = L"s3secret";

        // Legacy persisted deeplink approval is intentionally ignored. Every URL invocation asks.
        bool AllowDeeplinks();
        void AllowDeeplinks(bool value);
        std::wstring S3Access();
        void S3Access(std::wstring const& value);
        std::wstring S3Secret();
        void S3Secret(std::wstring const& value);

        // "" when missing.
        std::wstring Get(std::wstring_view key);
        // Writes the file right away; an empty value removes the key.
        void Set(std::wstring_view key, std::wstring const& value);

        // Raised after Set changed a key.
        Event<std::wstring_view> Changed;

    private:
        Secrets() = default;
        void EnsureLoaded();
        void Save() const;

        bool m_loaded = false;
        std::map<std::wstring, SecureString, std::less<>> m_values;
    };
}
