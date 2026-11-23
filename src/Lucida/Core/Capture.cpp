#include "pch.h"
#include "Core/Capture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace Lucida::Core
{
    namespace
    {
        constexpr std::uint8_t kAlphaThreshold = 150;   // 0x96
        constexpr int kMaxScan = 300;

        std::uint8_t AlphaAt(Bitmap const& image, int x, int y) noexcept
        {
            return image.pixels[static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4 + 3];
        }

        // Source-over of a premultiplied BGRA pixel.
        void Blend(std::uint8_t* dst, std::uint8_t const* src) noexcept
        {
            unsigned const inverse = 255u - src[3];
            for (int c = 0; c < 4; ++c)
            {
                dst[c] = static_cast<std::uint8_t>(src[c] + (dst[c] * inverse + 127) / 255);
            }
        }

        // Draws `image` onto `canvas` (same width) moved down by `shift` rows, clipped.
        void DrawShifted(Bitmap& canvas, Bitmap const& image, int shift) noexcept
        {
            for (int y = 0; y < image.height; ++y)
            {
                int const ty = y + shift;
                if (ty < 0 || ty >= canvas.height)
                {
                    continue;
                }
                std::uint8_t const* src = image.pixels.data() + static_cast<std::size_t>(y) * image.Stride();
                std::uint8_t* dst = canvas.pixels.data() + static_cast<std::size_t>(ty) * canvas.Stride();
                int const width = (std::min)(image.width, canvas.width);
                for (int x = 0; x < width; ++x)
                {
                    Blend(dst + x * 4, src + x * 4);
                }
            }
        }

        Bitmap BlankLike(Bitmap const& image)
        {
            Bitmap canvas;
            canvas.width = image.width;
            canvas.height = image.height;
            canvas.dpr = image.dpr;
            canvas.screenRect = image.screenRect;
            canvas.pixels.assign(image.Stride() * static_cast<std::size_t>(image.height), 0);
            return canvas;
        }

        // Truncation toward zero.
        int Trunc(double value) noexcept
        {
            return static_cast<int>(std::trunc(value));
        }

        std::uint8_t RoundedCoverage(int x, int y, int width, int height, int radius) noexcept
        {
            if (radius <= 0)
            {
                return 255;
            }
            int inside = 0;
            constexpr int kSamples = 4;
            for (int sy = 0; sy < kSamples; ++sy)
            {
                for (int sx = 0; sx < kSamples; ++sx)
                {
                    double const px = static_cast<double>(x) + (static_cast<double>(sx) + 0.5) / kSamples;
                    double const py = static_cast<double>(y) + (static_cast<double>(sy) + 0.5) / kSamples;
                    double const dx = (std::max)({ static_cast<double>(radius) - px, 0.0, px - (width - radius) });
                    double const dy = (std::max)({ static_cast<double>(radius) - py, 0.0, py - (height - radius) });
                    if (dx * dx + dy * dy <= static_cast<double>(radius) * radius)
                    {
                        ++inside;
                    }
                }
            }
            return static_cast<std::uint8_t>((inside * 255 + (kSamples * kSamples) / 2) / (kSamples * kSamples));
        }

        void BlurAlpha(std::vector<std::uint8_t>& alpha, int width, int height, int radius)
        {
            if (radius <= 0)
            {
                return;
            }
            std::vector<std::uint8_t> temporary(alpha.size());
            auto blur = [radius](std::vector<std::uint8_t> const& from, std::vector<std::uint8_t>& to, int width, int height, bool horizontal) {
                for (int y = 0; y < height; ++y)
                {
                    for (int x = 0; x < width; ++x)
                    {
                        int sum = 0;
                        int count = 0;
                        for (int d = -radius; d <= radius; ++d)
                        {
                            int const px = horizontal ? x + d : x;
                            int const py = horizontal ? y : y + d;
                            if (px >= 0 && px < width && py >= 0 && py < height)
                            {
                                sum += from[static_cast<std::size_t>(py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(px)];
                                ++count;
                            }
                        }
                        to[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                            static_cast<std::uint8_t>((sum + count / 2) / count);
                    }
                }
            };
            blur(alpha, temporary, width, height, true);
            blur(temporary, alpha, width, height, false);
        }
    }

    std::optional<ShadowMode> ParseShadowMode(std::wstring_view value) noexcept
    {
        if (value == L"transparent") return ShadowMode::Transparent;
        if (value == L"trimmed") return ShadowMode::Trimmed;
        if (value == L"wallpaper") return ShadowMode::Wallpaper;
        if (value == L"solid") return ShadowMode::Solid;
        return std::nullopt;
    }

    Bitmap AddWindowShadow(Bitmap image, WindowShadowStyle const& style)
    {
        if (image.IsEmpty() || (style.left <= 0 && style.top <= 0 && style.right <= 0 && style.bottom <= 0 && style.cornerRadius <= 0))
        {
            return image;
        }
        constexpr int kMaxStyleExtent = 16384;
        int const left = std::clamp(style.left, 0, kMaxStyleExtent);
        int const top = std::clamp(style.top, 0, kMaxStyleExtent);
        int const right = std::clamp(style.right, 0, kMaxStyleExtent);
        int const bottom = std::clamp(style.bottom, 0, kMaxStyleExtent);
        std::int64_t const outputWidth = static_cast<std::int64_t>(image.width) + left + right;
        std::int64_t const outputHeight = static_cast<std::int64_t>(image.height) + top + bottom;
        if (outputWidth > (std::numeric_limits<int>::max)() || outputHeight > (std::numeric_limits<int>::max)())
        {
            return image;
        }
        int const width = static_cast<int>(outputWidth);
        int const height = static_cast<int>(outputHeight);
        auto const byteCount = TryBitmapBytes(width, height);
        if (!byteCount)
        {
            return image;
        }
        int const radius = std::clamp(style.cornerRadius, 0, (std::min)(image.width, image.height) / 2);
        int const blurRadius = std::clamp(style.blurRadius, 0, 128);
        int const offsetY = std::clamp(style.offsetY, 0, kMaxStyleExtent);

        Bitmap canvas;
        canvas.width = width;
        canvas.height = height;
        canvas.dpr = image.dpr;
        canvas.screenRect = image.screenRect.IsEmpty()
            ? Rect{}
            : Rect{ image.screenRect.x - left, image.screenRect.y - top, static_cast<double>(width), static_cast<double>(height) };
        canvas.pixels.assign(*byteCount, 0);

        std::vector<std::uint8_t> coverage(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
        std::vector<std::uint8_t> shadow(canvas.pixels.size() / 4, 0);
        for (int y = 0; y < image.height; ++y)
        {
            for (int x = 0; x < image.width; ++x)
            {
                std::uint8_t const cov = RoundedCoverage(x, y, image.width, image.height, radius);
                coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)] = cov;
                int const shadowY = top + y + offsetY;
                if (shadowY >= 0 && shadowY < height)
                {
                    shadow[static_cast<std::size_t>(shadowY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(left + x)] = cov;
                }
            }
        }
        BlurAlpha(shadow, width, height, blurRadius);
        std::uint8_t peak = 0;
        for (std::uint8_t alpha : shadow)
        {
            peak = (std::max)(peak, alpha);
        }
        if (peak > 0)
        {
            for (std::size_t i = 0; i < shadow.size(); ++i)
            {
                canvas.pixels[i * 4 + 3] = static_cast<std::uint8_t>((static_cast<unsigned>(shadow[i]) * style.opacity + peak / 2) / peak);
            }
        }
        for (int y = 0; y < image.height; ++y)
        {
            for (int x = 0; x < image.width; ++x)
            {
                std::uint8_t const cov = coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)];
                std::uint8_t const* source = image.pixels.data() + static_cast<std::size_t>(y) * image.Stride() + static_cast<std::size_t>(x) * 4;
                std::uint8_t masked[4];
                for (int channel = 0; channel < 4; ++channel)
                {
                    masked[channel] = static_cast<std::uint8_t>((static_cast<unsigned>(source[channel]) * cov + 127) / 255);
                }
                std::uint8_t* destination = canvas.pixels.data() + static_cast<std::size_t>(top + y) * canvas.Stride() + static_cast<std::size_t>(left + x) * 4;
                Blend(destination, masked);
            }
        }
        return canvas;
    }

    ShadowPaddings ComputeShadowPaddings(Bitmap const& image) noexcept
    {
        ShadowPaddings paddings;
        if (image.IsEmpty())
        {
            return paddings;
        }
        auto scan = [&](int startX, int startY, int stepX, int stepY) {
            for (int index = 0; index < kMaxScan; ++index)
            {
                int const x = startX + stepX * index;
                int const y = startY + stepY * index;
                if (x < 0 || x >= image.width || y < 0 || y >= image.height)
                {
                    break;
                }
                if (AlphaAt(image, x, y) > kAlphaThreshold)
                {
                    return (std::max)(index, 1) - 1;
                }
            }
            return 0;
        };

        int const middleRow = image.height / 2;
        paddings.left = scan(0, middleRow, 1, 0);
        paddings.right = scan(image.width - 1, middleRow, -1, 0);
        int const column = std::clamp(paddings.left + (image.width - paddings.left - paddings.right) / 2, 0, image.width - 1);
        paddings.top = scan(column, 0, 0, 1);
        paddings.bottom = scan(column, image.height - 1, 0, -1);
        return paddings;
    }

    Bitmap TrimShadow(Bitmap image)
    {
        auto const p = ComputeShadowPaddings(image);
        if (p.left + p.right < image.width && p.top + p.bottom < image.height)
        {
            return CropBitmap(image, p.left, p.top, image.width - p.left - p.right, image.height - p.top - p.bottom);
        }
        return image;
    }

    bool IsEdgeTransparent(Bitmap const& image) noexcept
    {
        if (image.IsEmpty())
        {
            return false;
        }
        int const x = image.width / 2;
        return AlphaAt(image, x, 0) != 255 || AlphaAt(image, x, image.height - 1) != 255;
    }

    Bitmap AddWallpaper(Bitmap image, Bitmap const& wallpaper)
    {
        if (image.IsEmpty() || !IsEdgeTransparent(image) || wallpaper.IsEmpty())
        {
            return image;
        }
        int const w = image.width;
        int const h = image.height;
        // The centred W×H crop, intersected with the wallpaper and made integral like
        // Cropped, then drawn stretched into (0, 0, W, H).
        Rect const wanted{ (wallpaper.width - w) / 2.0, (wallpaper.height - h) / 2.0, static_cast<double>(w), static_cast<double>(h) };
        Rect crop = wanted.Intersection(Rect{ 0, 0, static_cast<double>(wallpaper.width), static_cast<double>(wallpaper.height) });
        if (crop.IsEmpty())
        {
            return image;
        }
        double const left = std::floor(crop.x);
        double const top = std::floor(crop.y);
        crop = Rect{ left, top, std::ceil(crop.x + crop.width) - left, std::ceil(crop.y + crop.height) - top };

        Bitmap canvas = BlankLike(image);
        for (int y = 0; y < h; ++y)
        {
            int const sy = (std::min)(wallpaper.height - 1, static_cast<int>(crop.y + (y + 0.5) * crop.height / h));
            std::uint8_t const* srcRow = wallpaper.pixels.data() + static_cast<std::size_t>(sy) * wallpaper.Stride();
            std::uint8_t* dstRow = canvas.pixels.data() + static_cast<std::size_t>(y) * canvas.Stride();
            for (int x = 0; x < w; ++x)
            {
                int const sx = (std::min)(wallpaper.width - 1, static_cast<int>(crop.x + (x + 0.5) * crop.width / w));
                std::memcpy(dstRow + x * 4, srcRow + sx * 4, 4);
            }
        }
        auto const p = ComputeShadowPaddings(image);
        DrawShifted(canvas, image, Trunc((p.bottom - p.top) / 2.0));
        return canvas;
    }

    Bitmap AddColor(Bitmap const& image, Color color)
    {
        if (image.IsEmpty())
        {
            return image;
        }
        Bitmap canvas = BlankLike(image);
        // windowSolidColor has alpha 1.0; premultiplied BGRA.
        std::uint8_t const pixel[4] = {
            static_cast<std::uint8_t>((color.b * color.a + 127) / 255),
            static_cast<std::uint8_t>((color.g * color.a + 127) / 255),
            static_cast<std::uint8_t>((color.r * color.a + 127) / 255),
            color.a,
        };
        for (std::size_t i = 0; i < canvas.pixels.size(); i += 4)
        {
            std::memcpy(canvas.pixels.data() + i, pixel, 4);
        }
        auto const p = ComputeShadowPaddings(image);
        DrawShifted(canvas, image, Trunc((p.bottom - p.top) / 2.0));
        return canvas;
    }

    Bitmap CropBitmap(Bitmap const& image, int x, int y, int width, int height)
    {
        int const left = std::clamp(x, 0, image.width);
        int const top = std::clamp(y, 0, image.height);
        int const right = std::clamp(x + width, left, image.width);
        int const bottom = std::clamp(y + height, top, image.height);
        Bitmap out;
        out.width = right - left;
        out.height = bottom - top;
        out.dpr = image.dpr;
        out.screenRect = image.screenRect.IsEmpty()
            ? Rect{}
            : Rect{ image.screenRect.x + left, image.screenRect.y + top, static_cast<double>(out.width), static_cast<double>(out.height) };
        out.pixels.resize(out.Stride() * static_cast<std::size_t>(out.height));
        for (int row = 0; row < out.height; ++row)
        {
            std::memcpy(out.pixels.data() + static_cast<std::size_t>(row) * out.Stride(),
                        image.pixels.data() + static_cast<std::size_t>(top + row) * image.Stride() + static_cast<std::size_t>(left) * 4,
                        out.Stride());
        }
        return out;
    }

    Rect SelectorCutout(Point start, Point current, Size screen) noexcept
    {
        double x1 = current.x;
        double y1 = current.y;
        if (x1 > start.x && x1 == screen.width - 1)
        {
            x1 += 1;
        }
        if (y1 > start.y && y1 == screen.height - 1)
        {
            y1 += 1;
        }
        Rect const rect{ (std::min)(start.x, x1), (std::min)(start.y, y1), std::abs(x1 - start.x), std::abs(y1 - start.y) };
        return rect.Intersection(Rect{ 0, 0, screen.width, screen.height });
    }

    bool IsSelectionTooSmall(Rect pixels, double dpr) noexcept
    {
        double const scale = dpr > 0 ? dpr : 1.0;
        return pixels.Standardized().width / scale < 5 || pixels.Standardized().height / scale < 5;
    }

    StatusLabelLayout LayoutStatusLabel(Rect cutout, Size text, Size screen, double dpr) noexcept
    {
        double const gap = 10 * dpr;
        double const margin = 6 * dpr;
        double const x = std::trunc(cutout.x + cutout.width / 2 - text.width / 2);
        // Below the cutout, or above it when that is < 6 from the bottom.
        double y = cutout.Bottom() + gap;
        if (screen.height - (y + text.height) < margin)
        {
            y = cutout.Top() - gap - text.height;
        }
        y = std::trunc(y);
        StatusLabelLayout layout;
        layout.text = Rect{ x, y, text.width, text.height };
        layout.box = Rect{ x - 8 * dpr, y - 6 * dpr, text.width + 16 * dpr, text.height + 12 * dpr };
        return layout;
    }

    bool NeedsAreaCaptureQuestion(CaptureActions const& actions, std::wstring_view mode, bool afterGrabShow,
                                  std::wstring_view areaCaptureMode) noexcept
    {
        return actions.useSettings && mode == L"interactive" && afterGrabShow && areaCaptureMode == L"none";
    }

    ResolvedActions ResolveCaptureActions(CaptureActions const& actions, std::wstring_view mode, bool afterGrabShow,
                                          bool afterGrabCopy, bool afterGrabSave, std::wstring_view areaCaptureMode) noexcept
    {
        ResolvedActions resolved;
        if (!actions.useSettings)
        {
            resolved.copy = actions.copy;
            resolved.save = actions.save;
            resolved.edit = actions.edit;
            resolved.pin = actions.pin;
            resolved.thumbnail = actions.thumbnail;
            return resolved;
        }
        resolved.copy = afterGrabCopy;
        resolved.save = afterGrabSave;
        resolved.thumbnail = mode == L"interactive" && afterGrabShow && areaCaptureMode == L"preview";
        resolved.edit = afterGrabShow && !resolved.thumbnail;
        resolved.pin = false;
        return resolved;
    }

    Point AddCapturePosition(Rect captureRect, double canvasMaxX, Size object, double dpr) noexcept
    {
        double const maxX = (std::max)(captureRect.x, captureRect.x + captureRect.width);
        double const maxY = (std::max)(captureRect.y, captureRect.y + captureRect.height);
        double const x = (std::min)(maxX + 20 * dpr, canvasMaxX + 1920 * dpr - object.width - 20 * dpr);
        return { x, maxY - object.height };
    }
}
