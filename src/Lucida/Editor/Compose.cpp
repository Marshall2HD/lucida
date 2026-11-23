#include "pch.h"
#include "Editor/Compose.h"

#include "Editor/BackdropRender.h"
#include "Editor/Render.h"
#include "Editor/RenderObjects.h"

#include <algorithm>
#include <cstring>

// The final image, drawn with Win2D on the shared device.  A CanvasBitmap has a
// device-dependent maximum dimension, so export is deliberately rendered in 2-D
// tiles rather than assuming a scrolling capture fits in one bitmap.
namespace Lucida::Editor
{
    namespace canvas = winrt::Microsoft::Graphics::Canvas;
    namespace bd = Core::Backdrop;
    using winrt::Windows::Foundation::Rect;

    namespace
    {
        std::shared_ptr<Core::Bitmap const> CropOnly(std::shared_ptr<Core::Bitmap const> const& image, Core::Rect const& crop)
        {
            int const x = std::clamp(static_cast<int>(crop.Left()), 0, image->width);
            int const y = std::clamp(static_cast<int>(crop.Top()), 0, image->height);
            int const w = std::clamp(static_cast<int>(std::abs(crop.width)), 0, image->width - x);
            int const h = std::clamp(static_cast<int>(std::abs(crop.height)), 0, image->height - y);
            if (x == 0 && y == 0 && w == image->width && h == image->height)
                return image;
            auto out = std::make_shared<Core::Bitmap>(Core::Bitmap::Create(w, h, image->dpr));
            if (out->IsEmpty()) return nullptr;
            out->screenRect = image->screenRect;
            for (int row = 0; row < h; ++row)
                std::memcpy(out->pixels.data() + out->Stride() * row,
                            image->pixels.data() + image->Stride() * (y + row) + static_cast<std::size_t>(x) * 4, out->Stride());
            return out;
        }

        // Create a packed bitmap for an arbitrary source rectangle. CanvasBitmap does
        // not accept a stride, therefore rows must be copied into a contiguous buffer.
        canvas::CanvasBitmap TileBitmap(canvas::ICanvasResourceCreator const& device, Core::Bitmap const& image,
                                        int srcX, int srcY, int srcW, int srcH)
        {
            auto const bytes = Core::TryBitmapBytes(srcW, srcH);
            if (!bytes || srcX < 0 || srcY < 0 || srcX + srcW > image.width || srcY + srcH > image.height)
                return nullptr;
            std::vector<std::uint8_t> packed(*bytes);
            for (int row = 0; row < srcH; ++row)
                std::memcpy(packed.data() + static_cast<std::size_t>(row) * srcW * 4,
                            image.pixels.data() + image.Stride() * static_cast<std::size_t>(srcY + row) + static_cast<std::size_t>(srcX) * 4,
                            static_cast<std::size_t>(srcW) * 4);
            return canvas::CanvasBitmap::CreateFromBytes(device, winrt::array_view<std::uint8_t const>(packed), srcW, srcH,
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 96.0f, canvas::CanvasAlphaMode::Premultiplied);
        }

        void LogFailure(wchar_t const* message) { OutputDebugStringW(message); }
    }

    std::shared_ptr<Core::Bitmap const> Compose(Core::Document const& document, ComposeOptions const& options)
    {
        auto const& image = document.ImagePtr();
        if (!image || image->IsEmpty()) return image;
        bool const withBackdrop = document.backdrop.config.isOn == 1 && !options.ignoreBackdrop;
        // CropOnly is intentionally restricted to this lossless fast path.  Returning an
        // unannotated screenshot after a failed render is data loss, not a useful fallback.
        if (document.Objects().empty() && !withBackdrop) return CropOnly(image, document.Crop());

        try
        {
            Core::Rect const crop = document.Crop().Standardized();
            bd::Layout layout;
            if (withBackdrop)
                layout = BackdropRender::DocumentLayout(document);
            else
            {
                bd::Config off = document.backdrop.config;
                off.isOn = 0;
                layout = bd::ComputeLayout(off, image->dpr, crop, crop, false);
                Core::Rect const canvasRect = document.Canvas().Standardized().Union(crop);
                layout.canvasW = canvasRect.width;
                layout.canvasH = canvasRect.height;
                layout.screenshot = { crop.x - canvasRect.x, crop.y - canvasRect.y, crop.width, crop.height };
                layout.content = { 0, 0, canvasRect.width, canvasRect.height };
            }
            int const width = static_cast<int>(std::lround(layout.canvasW));
            int const height = static_cast<int>(std::lround(layout.canvasH));
            if (width <= 0 || height <= 0 || !Core::TryBitmapBytes(width, height))
            {
                LogFailure(L"Lucida: composition has invalid or oversized output dimensions\n");
                return nullptr;
            }

            canvas::CanvasDevice const device = canvas::CanvasDevice::GetSharedDevice();
            int const maxSize = device.MaximumBitmapSizeInPixels();
            if (maxSize <= 0)
            {
                LogFailure(L"Lucida: composition device has no usable bitmap size\n");
                return nullptr;
            }
            // Keep both dimensions legal and each render target below about 16 MiB.
            // Reserve a halo inside the source-bitmap limit as well as the target limit.
            int const halo = std::min(512, (std::max)(0, (maxSize - 1) / 4));
            int const interiorLimit = (std::max)(1, maxSize - halo * 2);
            int const tileWidth = std::max(1, std::min(width, std::min(interiorLimit, 2048)));
            int const tileHeight = std::max(1, std::min(height, std::min(interiorLimit, static_cast<int>((16 * 1024 * 1024) / (tileWidth * 4)))));
            auto out = std::make_shared<Core::Bitmap>(Core::Bitmap::Create(width, height, image->dpr));
            if (out->IsEmpty())
            {
                LogFailure(L"Lucida: unable to allocate composition output\n");
                return nullptr;
            }
            out->screenRect = image->screenRect;
            auto const canvasColor = withBackdrop ? BackdropRender::CanvasColor(document) : std::nullopt;

            struct EnvironmentScope
            {
                Core::ObjectEnvironment& env = Core::Environment();
                Core::ObjectEnvironment saved{ env.dpr, env.zoom, env.image, env.bitmap, env.imageGeneration };
                ~EnvironmentScope() { env.dpr = saved.dpr; env.zoom = saved.zoom; env.image = saved.image; env.bitmap = saved.bitmap; env.imageGeneration = saved.imageGeneration; }
            } const environment;
            environment.env.dpr = image->dpr;
            environment.env.zoom = 1.0;
            environment.env.image = document.Source();
            environment.env.bitmap = image.get();
            environment.env.imageGeneration = document.ImageGeneration();

            // The halo covers blend/filter bleed at tile seams; sourceOrigin keeps sampling exact.
            for (int top = 0; top < height; top += tileHeight)
            for (int left = 0; left < width; left += tileWidth)
            {
                int const rows = std::min(tileHeight, height - top);
                int const cols = std::min(tileWidth, width - left);
                // The image coordinates corresponding to this output tile.  Only the
                // intersecting screenshot region is uploaded; it may begin at any x/y.
                int const wantedX = static_cast<int>(std::floor(crop.x + left - layout.screenshot.x)) - halo;
                int const wantedY = static_cast<int>(std::floor(crop.y + top - layout.screenshot.y)) - halo;
                int const wantedRight = static_cast<int>(std::ceil(crop.x + left + cols - layout.screenshot.x)) + halo;
                int const wantedBottom = static_cast<int>(std::ceil(crop.y + top + rows - layout.screenshot.y)) + halo;
                int const srcX = std::clamp(wantedX, 0, image->width);
                int const srcY = std::clamp(wantedY, 0, image->height);
                int const srcRight = std::clamp(wantedRight, srcX, image->width);
                int const srcBottom = std::clamp(wantedBottom, srcY, image->height);
                auto const source = srcRight > srcX && srcBottom > srcY ? TileBitmap(device, *image, srcX, srcY, srcRight - srcX, srcBottom - srcY) : nullptr;
                canvas::CanvasRenderTarget target(device, static_cast<float>(cols), static_cast<float>(rows), 96.0f);
                {
                    auto session = target.CreateDrawingSession();
                    session.Clear(winrt::Windows::UI::Colors::Transparent());
                    session.Transform(winrt::Windows::Foundation::Numerics::make_float3x2_translation(static_cast<float>(-left), static_cast<float>(-top)));
                    if (withBackdrop) BackdropRender::DrawBackdrop(session, document, layout, true);
                    canvas::CanvasActiveLayer clip{ nullptr };
                    if (auto const geometry = BackdropRender::ContentClip(session, layout)) clip = session.CreateLayer(1.0f, geometry);
                    else clip = session.CreateLayer(1.0f, Rect{ static_cast<float>(layout.content.x), static_cast<float>(layout.content.y), static_cast<float>(layout.content.width), static_cast<float>(layout.content.height) });
                    if (canvasColor)
                    {
                        auto byte = [](double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0), 0L, 255L)); };
                        session.FillRectangle(Rect{ static_cast<float>(layout.content.x), static_cast<float>(layout.content.y), static_cast<float>(layout.content.width), static_cast<float>(layout.content.height) },
                            winrt::Windows::UI::Color{ 255, byte(canvasColor->r), byte(canvasColor->g), byte(canvasColor->b) });
                    }
                    if (source)
                    {
                        Core::Rect const sourceRect{ static_cast<double>(srcX), static_cast<double>(srcY), static_cast<double>(srcRight - srcX), static_cast<double>(srcBottom - srcY) };
                        Core::Rect const shown = sourceRect.Intersection(crop);
                        if (!shown.IsEmpty())
                            session.DrawImage(source, Rect{ static_cast<float>(layout.screenshot.x + shown.x - crop.x), static_cast<float>(layout.screenshot.y + shown.y - crop.y), static_cast<float>(shown.width), static_cast<float>(shown.height) },
                                Rect{ static_cast<float>(shown.x - srcX), static_cast<float>(shown.y - srcY), static_cast<float>(shown.width), static_cast<float>(shown.height) }, 1.0f, canvas::CanvasImageInterpolation::NearestNeighbor);
                    }
                    Render::Context context;
                    context.zoom = 1.0; context.dpr = image->dpr; context.rasterScale = 1.0;
                    context.origin = { layout.screenshot.x - crop.x, layout.screenshot.y - crop.y };
                    context.sourceOrigin = { static_cast<double>(srcX), static_cast<double>(srcY) };
                    context.imageGeneration = document.ImageGeneration();
                    context.image = source;
                    Render::DrawSpotlights(session, document.Objects(), context, document.Source());
                    for (auto const& object : document.Objects()) if (object) Render::Draw(session, *object, context);
                    clip.Close(); session.Close();
                }
                auto const bytes = target.GetPixelBytes();
                std::size_t const expected = static_cast<std::size_t>(cols) * rows * 4;
                if (bytes.size() < expected)
                {
                    LogFailure(L"Lucida: composition tile returned too few pixels\n");
                    return nullptr;
                }
                for (int row = 0; row < rows; ++row)
                    std::memcpy(out->pixels.data() + out->Stride() * static_cast<std::size_t>(top + row) + static_cast<std::size_t>(left) * 4,
                                bytes.data() + static_cast<std::size_t>(row) * cols * 4, static_cast<std::size_t>(cols) * 4);
            }
            return out;
        }
        catch (winrt::hresult_error const& e)
        {
            OutputDebugStringW((L"Lucida: composing the image failed: " + std::wstring{ e.message() } + L"\n").c_str());
        }
        catch (...) { LogFailure(L"Lucida: composing the image failed\n"); }
        return nullptr;
    }
}
