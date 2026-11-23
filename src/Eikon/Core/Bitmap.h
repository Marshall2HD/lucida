#pragma once

// A captured or loaded image (ARCHITECTURE.md, "Images"). Platform-neutral: Win2D wraps the pixels
// with CanvasBitmap::CreateFromBytes when the editor shows them.

#include "Core/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace Eikon::Core
{
    inline constexpr std::size_t kMaxImageBytes = 128ull * 1024 * 1024;
    inline constexpr std::size_t kMaxInputBytes = 128ull * 1024 * 1024;

    // BGRA8 byte count, bounded before every multiplication.
    inline std::optional<std::size_t> TryBitmapBytes(int width, int height) noexcept
    {
        if (width <= 0 || height <= 0)
        {
            return std::nullopt;
        }
        std::uint64_t const w = static_cast<std::uint64_t>(width);
        std::uint64_t const h = static_cast<std::uint64_t>(height);
        std::uint64_t constexpr maxBytes = static_cast<std::uint64_t>(kMaxImageBytes);
        if (w > maxBytes / 4 || h > maxBytes / (w * 4))
        {
            return std::nullopt;
        }
        std::uint64_t const bytes = w * h * 4;
        if (bytes > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()))
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(bytes);
    }

    inline std::optional<std::uint32_t> TryPixelByteCount32(int width, int height) noexcept
    {
        auto const bytes = TryBitmapBytes(width, height);
        if (!bytes || *bytes > static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()))
        {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*bytes);
    }

    struct Bitmap
    {
        // Pixel size. Rows are top-down and tightly packed: stride = width * 4.
        std::int32_t width = 0;
        std::int32_t height = 0;
        // Scale of the screen it came from.
        double dpr = 1.0;
        // Where it was on the virtual desktop, in physical pixels (empty for clipboard/file images).
        Rect screenRect;
        // BGRA8, premultiplied alpha (DirectXPixelFormat::B8G8R8A8UIntNormalized, CanvasAlphaMode::Premultiplied).
        std::vector<std::uint8_t> pixels;

        std::size_t Stride() const noexcept { return static_cast<std::size_t>(width) * 4; }
        bool IsEmpty() const noexcept
        {
            auto const bytes = TryBitmapBytes(width, height);
            return !bytes || pixels.size() < *bytes;
        }
        // Size in points (image pixels / dpr): the image size shown in the editor.
        Size PointSize() const noexcept { return { width / dpr, height / dpr }; }

        // An opaque black image of the given size.
        static Bitmap Create(std::int32_t width, std::int32_t height, double dpr = 1.0)
        {
            Bitmap bitmap;
            auto const bytes = TryBitmapBytes(width, height);
            if (!bytes)
            {
                return bitmap;
            }
            bitmap.width = width;
            bitmap.height = height;
            bitmap.dpr = dpr;
            bitmap.pixels.assign(*bytes, 0);
            for (std::size_t i = 3; i < bitmap.pixels.size(); i += 4)
            {
                bitmap.pixels[i] = 0xFF;
            }
            return bitmap;
        }
    };
}
