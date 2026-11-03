#include "Pch.h"
#include "Graphics\Validation\PostProcessEffectsValidation.h"
#include "Graphics\Camera\CameraUtils.h"
#include "Graphics\DirectX12\Renderer.h"
#include "Graphics\DirectX12\FrameCapture.h"
#include "Graphics\Texture\TextureManager.h"
#include <DirectXPackedVector.h>
#include <cmath>
#include <cstring>

namespace Engine
{
    namespace
    {
        using Rgb = std::array<float, 3>;
        struct CpuImage
        {
            std::uint32_t width = 0, height = 0;
            std::vector<Rgb> pixels;
            const Rgb& at(const std::uint32_t x, const std::uint32_t y) const { return pixels[y * width + x]; }
        };
        struct Bounds
        {
            float x, y, width, height;
        };
        struct EffectCase
        {
            const char* name;
            PostProcessCameraSnapshot camera;
            std::uint32_t width = 320, height = 180;
            CameraViewport viewport{};
            bool exact = false;
            PostProcessValidationFixture fixture = PostProcessValidationFixture::Panels;
            bool customScales = false;
        };
        constexpr unsigned int MAX_ERROR = 2;
        Bounds pixelBounds(const EffectCase& test)
        {
            const auto viewport = CameraUtils::toPixelViewport(test.viewport,
                Vector2(static_cast<float>(test.width), static_cast<float>(test.height)));
            return { viewport.x, viewport.y, viewport.width, viewport.height };
        }

        float half(const float value)
        {
            return DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(value));
        }
        Rgb mix(const Rgb& a, const Rgb& b, const float weight)
        {
            Rgb result;
            for (std::size_t channel = 0; channel < result.size(); ++channel)
                result[channel] = std::lerp(a[channel], b[channel], weight);
            return result;
        }
        float luma(const Rgb& color)
        {
            return color[0] * 0.2126f + color[1] * 0.7152f + color[2] * 0.0722f;
        }
        float perceptual(const Rgb& color) { return std::round(std::sqrt(std::max(luma(color), 0.0f)) * 4096) / 4096; }
        float smooth(const float value)
        {
            const float t = std::clamp(value, 0.0f, 1.0f);
            return t * t * (3 - 2 * t);
        }
        float smoothstep(const float minimum, const float maximum, const float value)
        {
            return smooth((value - minimum) / (maximum - minimum));
        }
        void applyColorGradeRange(Rgb& color, const float saturation, const float contrast,
            const float gamma, const float gain, const Vector3& offset,
            const float settingsWeight, const float weight)
        {
            const float rangeSaturation = std::lerp(1.0f, std::lerp(1.0f, saturation, settingsWeight), weight);
            const float rangeContrast = std::lerp(1.0f, std::lerp(1.0f, contrast, settingsWeight), weight);
            const float rangeGamma = std::lerp(1.0f, std::lerp(1.0f, gamma, settingsWeight), weight);
            const float rangeGain = std::lerp(1.0f, std::lerp(1.0f, gain, settingsWeight), weight);
            const std::array offsets{ offset.x, offset.y, offset.z };
            const float luminance = luma(color);
            for (std::size_t channel = 0; channel < color.size(); ++channel)
            {
                color[channel] = std::max(std::lerp(luminance, color[channel], rangeSaturation), 0.0f);
                color[channel] = std::max((color[channel] - 0.18f) * rangeContrast + 0.18f
                    + offsets[channel] * settingsWeight * weight, 0.0f);
                color[channel] = std::pow(color[channel], 1.0f / rangeGamma) * rangeGain;
            }
        }
        float grainNoise(const std::uint32_t x, const std::uint32_t y, const std::uint32_t frame)
        {
            std::uint32_t value = x * 1973u + y * 9277u + frame * 26699u + 0x68bc21ebu;
            value = (value << 13u) ^ value;
            value = value * (value * value * 15731u + 789221u) + 1376312589u;
            return static_cast<float>(value & 0x7fffffffu) / 1073741824.0f - 1.0f;
        }
        Rgb sample(const CpuImage& image, const Bounds& bounds, float x, float y)
        {
            const float firstX = std::clamp(std::ceil(bounds.x - 0.5f), 0.0f, static_cast<float>(image.width - 1));
            const float firstY = std::clamp(std::ceil(bounds.y - 0.5f), 0.0f, static_cast<float>(image.height - 1));
            const float lastX = std::min(static_cast<float>(image.width - 1),
                std::max(firstX, std::floor(bounds.x + bounds.width) - 1));
            const float lastY = std::min(static_cast<float>(image.height - 1),
                std::max(firstY, std::floor(bounds.y + bounds.height) - 1));
            x = std::clamp(x, firstX, lastX);
            y = std::clamp(y, firstY, lastY);
            const auto left = static_cast<std::uint32_t>(x);
            const auto top = static_cast<std::uint32_t>(y);
            const auto right = std::min(left + 1, image.width - 1);
            const auto bottom = std::min(top + 1, image.height - 1);
            return mix(mix(image.at(left, top), image.at(right, top), x - left),
                mix(image.at(left, bottom), image.at(right, bottom), x - left), y - top);
        }
        Rgb resample(const CpuImage& image, const Bounds& source, const Bounds& target,
            const std::uint32_t x, const std::uint32_t y, const float dx = 0, const float dy = 0)
        {
            return sample(image, source, source.x + (x + 0.5f - target.x) * source.width / target.width - 0.5f + dx,
                source.y + (y + 0.5f - target.y) * source.height / target.height - 0.5f + dy);
        }
        bool inside(const Bounds& bounds, const std::uint32_t x, const std::uint32_t y)
        {
            return x + 0.5f >= bounds.x && y + 0.5f >= bounds.y
                && x < std::floor(bounds.x + bounds.width) && y < std::floor(bounds.y + bounds.height);
        }
        Bounds reducedBounds(const Bounds& full, const CpuImage& target, const CpuImage& original)
        {
            const float sx = static_cast<float>(target.width) / original.width;
            const float sy = static_cast<float>(target.height) / original.height;
            const float x = std::floor(full.x * sx), y = std::floor(full.y * sy);
            return { x, y, std::min(static_cast<float>(target.width), std::ceil((full.x + full.width) * sx)) - x,
                std::min(static_cast<float>(target.height), std::ceil((full.y + full.height) * sy)) - y };
        }
        CpuImage readHdr(const DirectX::ScratchImage& hdr)
        {
            const auto* const source = hdr.GetImage(0, 0, 0);
            CpuImage result{ static_cast<std::uint32_t>(source->width), static_cast<std::uint32_t>(source->height), {} };
            result.pixels.resize(result.width * result.height);
            for (std::uint32_t y = 0; y < result.height; ++y)
                for (std::uint32_t x = 0; x < result.width; ++x)
                {
                    std::array<DirectX::PackedVector::HALF, 4> pixel{};
                    std::memcpy(pixel.data(), source->pixels + y * source->rowPitch + x * 8, 8);
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        result.pixels[y * result.width + x][channel] = DirectX::PackedVector::XMConvertHalfToFloat(pixel[channel]);
                }
            return result;
        }
        bool identical(const DirectX::ScratchImage& left, const DirectX::ScratchImage& right)
        {
            const auto* const a = left.GetImage(0, 0, 0);
            const auto* const b = right.GetImage(0, 0, 0);
            if (!a || !b || a->width != b->width || a->height != b->height || a->format != b->format)
                return false;
            const auto bytes = a->width * DirectX::BitsPerPixel(a->format) / 8;
            for (std::size_t y = 0; y < a->height; ++y)
                if (std::memcmp(a->pixels + y * a->rowPitch, b->pixels + y * b->rowPitch, bytes) != 0)
                    return false;
            return true;
        }

        std::uint32_t bloom(CpuImage& image, const Bounds& full, const PostProcessSettings& settings, const float weight)
        {
            std::array<CpuImage, 5> levels;
            std::array<Bounds, 5> bounds{};
            std::size_t count = 0;
            const CpuImage* source = &image;
            Bounds sourceBounds = full;
            auto width = (image.width + 1) / 2, height = (image.height + 1) / 2;
            for (; count < levels.size(); ++count)
            {
                auto& target = levels[count];
                target = { width, height, std::vector<Rgb>(width * height) };
                bounds[count] = reducedBounds(full, target, image);
                for (std::uint32_t y = 0; y < height; ++y)
                    for (std::uint32_t x = 0; x < width; ++x)
                    {
                        if (!inside(bounds[count], x, y))
                            continue;
                        Rgb rgb{};
                        float weightSum = 0;
                        for (const float dy : { -0.5f, 0.5f })
                            for (const float dx : { -0.5f, 0.5f })
                            {
                                const auto value = resample(*source, sourceBounds, bounds[count], x, y, dx, dy);
                                const float brightness = std::max({ value[0], value[1], value[2] });
                                const float karisWeight = count == 0
                                    ? 1.0f / (1.0f + std::max(brightness, 0.0f)) : 0.25f;
                                for (std::size_t channel = 0; channel < 3; ++channel)
                                    rgb[channel] += value[channel] * karisWeight;
                                weightSum += karisWeight;
                            }
                        for (float& channel : rgb)
                            channel /= weightSum;
                        if (count == 0)
                        {
                            const float brightness = std::max({ rgb[0], rgb[1], rgb[2] });
                            const float threshold = settings.bloomThreshold.value;
                            const float contribution = threshold < 0
                                ? 1.0f : std::clamp(brightness - threshold, 0.0f, 1.0f);
                            for (float& channel : rgb)
                                channel *= contribution;
                        }
                        for (float& channel : rgb)
                            channel = half(std::min(channel, 65504.0f));
                        target.pixels[y * width + x] = rgb;
                    }
                source = &target;
                sourceBounds = bounds[count];
                if (width == 1 && height == 1)
                {
                    ++count;
                    break;
                }
                width = (width + 1) / 2;
                height = (height + 1) / 2;
            }
            CpuImage low = std::move(levels[count - 1]);
            Bounds lowBounds = bounds[count - 1];
            for (std::size_t index = count - 1; index > 0; --index)
            {
                auto& high = levels[index - 1];
                const auto highBounds = bounds[index - 1];
                for (std::uint32_t y = 0; y < high.height; ++y)
                    for (std::uint32_t x = 0; x < high.width; ++x)
                    {
                        if (!inside(highBounds, x, y))
                            continue;
                        Rgb filtered{};
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                const float tapWeight = static_cast<float>((dx == 0 ? 2 : 1) * (dy == 0 ? 2 : 1)) / 16;
                                const auto value = resample(low, lowBounds, highBounds, x, y,
                                    static_cast<float>(dx) * settings.bloomSizeScale.value,
                                    static_cast<float>(dy) * settings.bloomSizeScale.value);
                                for (std::size_t channel = 0; channel < 3; ++channel)
                                    filtered[channel] += value[channel] * tapWeight;
                            }
                        auto rgb = mix(high.at(x, y), filtered, settings.bloomScatter.value);
                        for (float& channel : rgb) channel = half(channel);
                        high.pixels[y * high.width + x] = rgb;
                    }
                low = std::move(high);
                lowBounds = highBounds;
            }
            for (std::uint32_t y = 0; y < image.height; ++y)
                for (std::uint32_t x = 0; x < image.width; ++x)
                {
                    if (!inside(full, x, y)) continue;
                    const auto glow = resample(low, lowBounds, full, x, y);
                    auto& rgb = image.pixels[y * image.width + x];
                    for (std::size_t channel = 0; channel < 3; ++channel)
                        rgb[channel] = half(std::min(rgb[channel] + glow[channel] * settings.bloomIntensity.value * weight, 65504.0f));
                }
                return static_cast<std::uint32_t>(count);
        }

        Rgb fxaa(const CpuImage& image, const Bounds& bounds, const std::uint32_t x, const std::uint32_t y,
            const PostProcessSettings& settings, const float weight)
        {
            const auto original = image.at(x, y);
            const auto value = [&image, &bounds, x, y](const float dx, const float dy)
            {
                return perceptual(sample(image, bounds, static_cast<float>(x) + dx, static_cast<float>(y) + dy));
            };
            const float center = perceptual(original);
            const float n = value(0, -1), s = value(0, 1), w = value(-1, 0), e = value(1, 0);
            const float minimum = std::min({ center, n, s, w, e }), maximum = std::max({ center, n, s, w, e });
            const float range = maximum - minimum;
            if (range <= std::max(settings.fxaaEdgeThresholdMin.value, maximum * settings.fxaaEdgeThreshold.value))
                return original;
            const float nw = value(-1, -1), ne = value(1, -1), sw = value(-1, 1), se = value(1, 1);
            const float horizontal = std::abs(nw - 2 * w + sw) + 2 * std::abs(n - 2 * center + s) + std::abs(ne - 2 * e + se);
            const float vertical = std::abs(nw - 2 * n + ne) + 2 * std::abs(w - 2 * center + e) + std::abs(sw - 2 * s + se);
            const bool h = horizontal >= vertical;
            const float neg = h ? n : w, pos = h ? s : e;
            const bool negative = std::abs(neg - center) >= std::abs(pos - center);
            const float neighbor = negative ? neg : pos;
            const float gradient = std::abs(neighbor - center);
            const float edge = (neighbor + center) * 0.5f;
            const float sign = negative ? -1.0f : 1.0f;
            const auto along = [&value, h, sign](const float distance)
            {
                return h ? value(distance, sign * 0.5f) : value(sign * 0.5f, distance);
            };
            float negDistance = 1, posDistance = 1;
            float negDelta = along(-1) - edge, posDelta = along(1) - edge;
            bool negDone = std::abs(negDelta) >= gradient * 0.25f, posDone = std::abs(posDelta) >= gradient * 0.25f;
            for (int step = 0; step < 8; ++step)
            {
                const float stride = step < 3 ? 1.0f : (step < 6 ? 2.0f : 4.0f);
                if (!negDone) { negDistance += stride; negDelta = along(-negDistance) - edge; negDone = std::abs(negDelta) >= gradient * 0.25f; }
                if (!posDone) { posDistance += stride; posDelta = along(posDistance) - edge; posDone = std::abs(posDelta) >= gradient * 0.25f; }
            }
            const float endpoint = negDistance <= posDistance ? negDelta : posDelta;
            const float edgeBlend = (endpoint < 0) != (center - edge < 0)
                ? 0.5f - std::min(negDistance, posDistance) / (negDistance + posDistance) : 0;
            const float average = (2 * (n + s + w + e) + nw + ne + sw + se) / 12;
            const float sub = smooth(std::abs(average - center) / range);
            const float blend = std::max(edgeBlend, sub * sub * settings.fxaaSubpixel.value);
            return mix(original, sample(image, bounds, x + (h ? 0 : sign * blend), y + (h ? sign * blend : 0)), weight);
        }

        struct Reference
        {
            CpuImage image;
            std::uint32_t bloomLevels = 0;
            std::uint32_t draws = 1;
            std::uint32_t effects = 0;
        };
        Reference reference(const DirectX::ScratchImage& hdr, const EffectCase& test,
            const std::uint32_t filmGrainFrameIndex)
        {
            Reference result{ readHdr(hdr), 0, 1 };
            auto& image = result.image;
            const Bounds bounds = pixelBounds(test);
            if (std::ceil(bounds.x - 0.5f) >= std::floor(bounds.x + bounds.width)
                || std::ceil(bounds.y - 0.5f) >= std::floor(bounds.y + bounds.height))
                return result;
            const auto& settings = test.camera.settings;
            const float weight = test.camera.enabled ? test.camera.weight : 0;
            const float bloomWeight = weight * test.camera.effects.bloom;
            const float exposureWeight = weight * test.camera.effects.exposure;
            const float colorWeight = weight * test.camera.effects.colorAdjustments;
            const float vignetteWeight = weight * test.camera.effects.vignette;
            const float chromaticAberrationWeight = weight * test.camera.effects.chromaticAberration;
            const float filmGrainWeight = weight * test.camera.effects.filmGrain;
            const float fxaaWeight = weight * test.camera.effects.fxaa;
            if (test.customScales)
            {
                for (auto& rgb : image.pixels)
                    for (float& channel : rgb) channel = half(channel * 0.5f);
                result.draws += 2;
            }
            if (bloomWeight > 0 && settings.bloomEnabled.value && settings.bloomIntensity.value > 0)
            {
                result.bloomLevels = bloom(image, bounds, settings, bloomWeight);
                result.draws += result.bloomLevels * 2;
            }
            const bool vignette = vignetteWeight > 0 && settings.vignetteEnabled.value && settings.vignetteIntensity.value > 0;
            const bool chromaticAberration = chromaticAberrationWeight > 0
                && settings.chromaticAberrationEnabled.value && settings.chromaticAberrationIntensity.value > 0;
            const bool filmGrain = filmGrainWeight > 0
                && settings.filmGrainEnabled.value && settings.filmGrainIntensity.value > 0;
            const bool aa = fxaaWeight > 0 && settings.fxaaEnabled.value;
            const float exposure = exposureWeight > 0 && settings.exposureEnabled.value ? std::exp2(settings.exposureEV.value * exposureWeight) : 1;
            const auto filter = Vector3::Lerp(Vector3::One, settings.colorFilter.value, colorWeight);
            const float temperature = std::lerp(6500.0f, settings.colorTemperature.value, colorWeight);
            const float tint = settings.colorTint.value * colorWeight;
            const float temperatureOffset = (6500.0f - temperature) / 6500.0f;
            const Rgb whiteBalance{
                std::exp2(temperatureOffset * 0.65f + tint * 0.3f),
                std::exp2(-std::abs(temperatureOffset) * 0.08f - tint * 0.35f),
                std::exp2(-temperatureOffset * 0.65f + tint * 0.3f),
            };
            const std::array globalOffset{
                settings.colorOffset.value.x, settings.colorOffset.value.y, settings.colorOffset.value.z,
            };
            const bool hasLdrPasses = vignette || chromaticAberration || filmGrain || aa || test.customScales;
            if (hasLdrPasses)
                ++result.draws;
            for (std::uint32_t y = 0; y < image.height; ++y)
                for (std::uint32_t x = 0; x < image.width; ++x)
                {
                    if (!inside(bounds, x, y)) continue;
                    auto& rgb = image.pixels[y * image.width + x];
                    if (colorWeight > 0 && settings.colorAdjustmentsEnabled.value)
                    {
                        const std::array factors{ filter.x, filter.y, filter.z };
                        for (std::size_t channel = 0; channel < 3; ++channel)
                            rgb[channel] = std::max(rgb[channel] * factors[channel] * whiteBalance[channel]
                                + globalOffset[channel] * colorWeight, 0.0f);
                        const float contrast = std::lerp(1.0f, 1.0f + settings.contrast.value * 0.01f, colorWeight);
                        for (float& channel : rgb)
                            channel = std::max((channel - 0.18f) * contrast + 0.18f, 0.0f);
                        const float gray = luma(rgb);
                        for (float& channel : rgb)
                            channel = std::max(std::lerp(gray, channel, 1 + settings.saturation.value * colorWeight * 0.01f), 0.0f);
                        const float gamma = std::lerp(1.0f, settings.colorGamma.value, colorWeight);
                        for (float& channel : rgb)
                            channel = std::pow(channel, 1.0f / gamma);
                        const float rangeLuminance = luma(rgb);
                        const float gradingLuminance = std::clamp(
                            std::log2(1.0f + std::max(rangeLuminance, 0.0f)) / 6.0f, 0.0f, 1.0f);
                        const float shadowWeight = 1.0f - smoothstep(0.0f, 0.5f, gradingLuminance);
                        const float highlightWeight = smoothstep(0.5f, 1.0f, gradingLuminance);
                        const float midtoneWeight = std::clamp(1.0f - shadowWeight - highlightWeight, 0.0f, 1.0f);
                        applyColorGradeRange(rgb, settings.shadowSaturation.value, settings.shadowContrast.value,
                            settings.shadowGamma.value, settings.shadowGain.value, settings.shadowColor.value,
                            colorWeight, shadowWeight);
                        applyColorGradeRange(rgb, settings.midtoneSaturation.value, settings.midtoneContrast.value,
                            settings.midtoneGamma.value, settings.midtoneGain.value, settings.midtoneColor.value,
                            colorWeight, midtoneWeight);
                        applyColorGradeRange(rgb, settings.highlightSaturation.value, settings.highlightContrast.value,
                            settings.highlightGamma.value, settings.highlightGain.value, settings.highlightColor.value,
                            colorWeight, highlightWeight);
                    }
                    for (float& channel : rgb)
                    {
                        const double value = std::min(static_cast<double>(channel) * exposure, 65504.0);
                        channel = static_cast<float>(std::clamp(value * (2.51 * value + 0.03)
                            / (value * (2.43 * value + 0.59) + 0.14), 0.0, 1.0));
                        if (hasLdrPasses) channel = half(channel);
                        if (test.customScales) channel = half(channel * 0.75f);
                    }
                    if (vignette)
                    {
                        const float dx = ((x + 0.5f - bounds.x) / bounds.width - 0.5f) * 2
                            * std::lerp(1.0f, bounds.width / bounds.height, settings.vignetteRoundness.value);
                        const float dy = ((y + 0.5f - bounds.y) / bounds.height - 0.5f) * 2;
                        const float radius = std::sqrt(dx * dx + dy * dy);
                        const float strength = smooth((radius - (1 - settings.vignetteSmoothness.value))
                            / settings.vignetteSmoothness.value) * settings.vignetteIntensity.value * vignetteWeight;
                        const auto& color = settings.vignetteColor.value;
                        rgb = mix(rgb, { color.x, color.y, color.z }, strength);
                        for (float& channel : rgb) channel = half(channel);
                    }
                }
            if (vignette) ++result.draws;
            if (chromaticAberration)
            {
                CpuImage source = image;
                for (std::uint32_t y = 0; y < image.height; ++y)
                    for (std::uint32_t x = 0; x < image.width; ++x)
                    {
                        if (!inside(bounds, x, y)) continue;
                        const float u = (x + 0.5f - bounds.x) / bounds.width;
                        const float v = (y + 0.5f - bounds.y) / bounds.height;
                        const float radialX = (u - 0.5f) * 2.0f;
                        const float radialY = (v - 0.5f) * 2.0f;
                        const float radius = std::sqrt(radialX * radialX + radialY * radialY);
                        const float onset = settings.chromaticAberrationStartOffset.value >= 1.0f
                            ? (radius >= 1.0f ? 1.0f : 0.0f)
                            : smoothstep(settings.chromaticAberrationStartOffset.value, 1.0f, radius);
                        const float offsetX = radialX * (settings.chromaticAberrationIntensity.value
                            * chromaticAberrationWeight * 0.75f * onset);
                        const float offsetY = radialY * (settings.chromaticAberrationIntensity.value
                            * chromaticAberrationWeight * 0.75f * onset);
                        const auto red = sample(source, bounds, x + offsetX, y + offsetY)[0];
                        const auto blue = sample(source, bounds, x - offsetX, y - offsetY)[2];
                        auto& target = image.pixels[y * image.width + x];
                        target = { half(red), half(source.at(x, y)[1]), half(blue) };
                    }
                ++result.draws;
            }
            if (aa)
            {
                CpuImage filtered = image;
                for (std::uint32_t y = 0; y < image.height; ++y)
                    for (std::uint32_t x = 0; x < image.width; ++x)
                    {
                        if (!inside(bounds, x, y)) continue;
                        auto rgb = fxaa(image, bounds, x, y, settings, fxaaWeight);
                        for (float& channel : rgb) channel = half(channel);
                        filtered.pixels[y * image.width + x] = rgb;
                    }
                image = std::move(filtered);
                ++result.draws;
            }
            if (filmGrain)
            {
                for (std::uint32_t y = 0; y < image.height; ++y)
                    for (std::uint32_t x = 0; x < image.width; ++x)
                    {
                        if (!inside(bounds, x, y)) continue;
                        auto& rgb = image.pixels[y * image.width + x];
                        const float response = std::lerp(1.0f, 1.0f - std::clamp(luma(rgb), 0.0f, 1.0f),
                            settings.filmGrainResponse.value);
                        const float noise = grainNoise(x, y, filmGrainFrameIndex);
                        for (float& channel : rgb)
                            channel = half(std::max(channel + noise * settings.filmGrainIntensity.value
                                * filmGrainWeight * response * 0.08f, 0.0f));
                    }
                ++result.draws;
            }
            result.effects = result.draws - 1 - (hasLdrPasses ? 1 : 0);
            return result;
        }

        unsigned int displayByte(const float linear)
        {
            const double value = std::clamp(static_cast<double>(linear), 0.0, 1.0);
            const double srgb = value <= 0.0031308 ? value * 12.92 : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
            return static_cast<unsigned int>(std::lround(srgb * 255));
        }
        double edgeEnergy(const DirectX::ScratchImage& image)
        {
            const auto& display = *image.GetImage(0, 0, 0);
            double energy = 0;
            for (std::size_t y = 1; y < display.height; ++y)
                for (std::size_t x = 1; x < display.width; ++x)
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const auto value = display.pixels[y * display.rowPitch + x * 4 + channel];
                        const double dx = value - display.pixels[y * display.rowPitch + (x - 1) * 4 + channel];
                        const double dy = value - display.pixels[(y - 1) * display.rowPitch + x * 4 + channel];
                        energy += dx * dx + dy * dy;
                    }
            return energy;
        }
    }

    bool runPostProcessEffectsValidation(DX12Renderer& renderer, const std::filesystem::path& directory,
        const PostProcessValidationRender& render, const PostProcessValidationVerifySaved& verifySaved)
    {
        std::vector<EffectCase> cases;
        PostProcessCameraSnapshot enabled;
        enabled.enabled = true;
        const auto add = [&cases](const char* name, const PostProcessCameraSnapshot& camera,
            const std::uint32_t width = 320, const std::uint32_t height = 180, const CameraViewport& viewport = {},
            const bool diagonal = false, const bool exact = false)
        { cases.push_back({ name, camera, width, height, viewport, exact,
            diagonal ? PostProcessValidationFixture::Diagonal : PostProcessValidationFixture::Panels, false }); };
        add("effects-neutral", enabled, 320, 180, {}, false, true);
        auto exposure = enabled;
        exposure.settings.exposureEnabled.value = true;
        for (const auto [name, ev, weight] : {
            std::tuple{ "exposure-positive", 2.0f, 1.0f }, std::tuple{ "exposure-negative", -1.0f, 1.0f },
            std::tuple{ "exposure-weighted", 2.0f, 0.5f }, std::tuple{ "exposure-minimum", -16.0f, 1.0f },
            std::tuple{ "exposure-maximum", 16.0f, 1.0f } })
        {
            exposure.settings.exposureEV.value = ev; exposure.weight = weight; add(name, exposure);
        }
        auto color = enabled;
        color.settings.colorAdjustmentsEnabled.value = true;
        color.settings.contrast.value = 30;
        color.settings.saturation.value = -40;
        color.settings.colorFilter.value = Vector3(0.8f, 1.2f, 0.6f);
        const auto baseColor = color;
        add("color-adjustments", color);
        color.weight = 0.5f; add("color-weighted", color);
        color.weight = 1; color.settings.saturation.value = -100; add("color-grayscale", color);
        color.settings.contrast.value = -100; add("color-flat-contrast", color);
        color.settings.contrast.value = 100; color.settings.saturation.value = 100;
        color.settings.colorFilter.value = Vector3(16, 16, 16); add("color-maximum", color);
        auto colorGrading = enabled;
        colorGrading.settings.colorAdjustmentsEnabled.value = true;
        colorGrading.settings.colorTemperature.value = 4300;
        colorGrading.settings.colorTint.value = 0.3f;
        colorGrading.settings.colorFilter.value = Vector3(1.1f, 0.9f, 1.2f);
        colorGrading.settings.contrast.value = 18;
        colorGrading.settings.saturation.value = -12;
        colorGrading.settings.colorGamma.value = 1.2f;
        colorGrading.settings.colorOffset.value = Vector3(0.03f, -0.02f, 0.01f);
        colorGrading.settings.shadowSaturation.value = 0.7f;
        colorGrading.settings.shadowContrast.value = 1.15f;
        colorGrading.settings.shadowGamma.value = 1.25f;
        colorGrading.settings.shadowGain.value = 0.8f;
        colorGrading.settings.shadowColor.value = Vector3(0.04f, -0.02f, 0.01f);
        colorGrading.settings.midtoneSaturation.value = 1.2f;
        colorGrading.settings.midtoneContrast.value = 0.9f;
        colorGrading.settings.midtoneGamma.value = 0.85f;
        colorGrading.settings.midtoneGain.value = 1.1f;
        colorGrading.settings.midtoneColor.value = Vector3(-0.02f, 0.03f, 0.01f);
        colorGrading.settings.highlightSaturation.value = 0.8f;
        colorGrading.settings.highlightContrast.value = 1.1f;
        colorGrading.settings.highlightGamma.value = 1.3f;
        colorGrading.settings.highlightGain.value = 0.9f;
        colorGrading.settings.highlightColor.value = Vector3(0.02f, 0.0f, -0.03f);
        add("color-grading-tonal-ranges", colorGrading);
        colorGrading.weight = 0.45f;
        add("color-grading-weighted", colorGrading);
        auto chromaticAberration = enabled;
        chromaticAberration.settings.chromaticAberrationEnabled.value = true;
        chromaticAberration.settings.chromaticAberrationIntensity.value = 1.5f;
        chromaticAberration.settings.chromaticAberrationStartOffset.value = 0.1f;
        add("chromatic-aberration", chromaticAberration);
        chromaticAberration.settings.chromaticAberrationStartOffset.value = 1.0f;
        add("chromatic-aberration-maximum-start", chromaticAberration);
        chromaticAberration.weight = 0.4f;
        add("chromatic-aberration-weighted", chromaticAberration);
        auto filmGrain = enabled;
        filmGrain.settings.filmGrainEnabled.value = true;
        filmGrain.settings.filmGrainIntensity.value = 0.7f;
        filmGrain.settings.filmGrainResponse.value = 0.85f;
        add("film-grain-temporal", filmGrain);
        filmGrain.weight = 0.4f;
        add("film-grain-weighted", filmGrain);
        auto vignette = enabled;
        vignette.settings.vignetteEnabled.value = true;
        vignette.settings.vignetteIntensity.value = 0.8f;
        vignette.settings.vignetteColor.value = Vector3(0.05f, 0.02f, 0.1f);
        const auto baseVignette = vignette;
        add("vignette", vignette);
        vignette.weight = 0.5f; add("vignette-weighted", vignette);
        vignette.weight = 1; vignette.settings.vignetteRoundness.value = 0;
        vignette.settings.vignetteSmoothness.value = 0.01f; add("vignette-sharp", vignette);
        auto glow = enabled;
        glow.settings.bloomEnabled.value = true;
        glow.settings.bloomIntensity.value = 0.8f;
        const auto baseBloom = glow;
        add("bloom", glow);
        auto uniform = glow; uniform.settings.bloomThreshold.value = 0;
        glow.weight = 0.5f; add("bloom-weighted", glow);
        glow.weight = 1; add("bloom-odd", glow, 321, 181);
        glow.settings.bloomThreshold.value = 64; add("bloom-rejected-highlight", glow, 320, 180, {}, false, true);
        glow.settings.bloomThreshold.value = 0; glow.settings.bloomScatter.value = 0; add("bloom-local", glow);
        glow.settings.bloomScatter.value = 1; glow.settings.bloomIntensity.value = 100; add("bloom-maximum", glow);
        glow.settings.bloomIntensity.value = 0.8f;
        glow.settings.bloomThreshold.value = -1;
        add("bloom-unthresholded", glow);
        glow.settings.bloomThreshold.value = 1;
        glow.settings.bloomSizeScale.value = 0.1f;
        add("bloom-minimum-size", glow);
        glow.settings.bloomSizeScale.value = 10;
        add("bloom-maximum-size", glow);
        glow.settings.bloomThreshold.value = 0.5f;
        glow.settings.bloomSizeScale.value = 2;
        add("bloom-karis-hotspot", glow);
        cases.back().fixture = PostProcessValidationFixture::BloomHotspot;
        auto aa = enabled;
        aa.settings.fxaaEnabled.value = true;
        add("fxaa-diagonal", aa, 320, 180, {}, true);
        aa.weight = 0.5f; add("fxaa-weighted", aa, 320, 180, {}, true);
        aa.weight = 1; aa.settings.fxaaSubpixel.value = 0; add("fxaa-edge-only", aa, 320, 180, {}, true);
        aa.settings.fxaaEdgeThreshold.value = 0.5f; aa.settings.fxaaEdgeThresholdMin.value = 0.5f;
        aa.settings.fxaaSubpixel.value = 1; add("fxaa-maximum-thresholds", aa, 320, 180, {}, true);
        aa = enabled; aa.settings.fxaaEnabled.value = true;
        auto stack = enabled;
        stack.settings = baseColor.settings;
        stack.settings.bloomEnabled.value = true;
        stack.settings.bloomIntensity.value = 0.6f;
        stack.settings.bloomThreshold.value = 1.0f;
        stack.settings.exposureEnabled.value = true;
        stack.settings.exposureEV.value = 1;
        stack.settings.vignetteEnabled.value = true;
        stack.settings.vignetteIntensity.value = 0.5f;
        stack.settings.chromaticAberrationEnabled.value = true;
        stack.settings.chromaticAberrationIntensity.value = 0.75f;
        stack.settings.chromaticAberrationStartOffset.value = 0.2f;
        stack.settings.filmGrainEnabled.value = true;
        stack.settings.filmGrainIntensity.value = 0.4f;
        stack.settings.filmGrainResponse.value = 0.7f;
        stack.settings.fxaaEnabled.value = true;
        add("effects-stack", stack, 321, 181, {}, true);
        const CameraViewport cropped{ 0.153f, 0.137f, 0.711f, 0.697f };
        auto localBloom = baseBloom; localBloom.settings.bloomScatter.value = 0;
        add("bloom-local-cropped", localBloom, 321, 181, cropped, true);
        add("bloom-cropped", baseBloom, 321, 181, cropped, true);
        add("vignette-cropped", baseVignette, 321, 181, cropped, true);
        add("fxaa-cropped", aa, 321, 181, cropped, true);
        auto noAa = stack; noAa.settings.fxaaEnabled.value = false;
        add("effects-cropped-no-aa", noAa, 321, 181, cropped, true);
        add("effects-cropped", stack, 321, 181, { 0.153f, 0.137f, 0.711f, 0.697f }, true);
        add("effects-single-pixel", stack, 1, 1);
        add("effects-single-column", stack, 1, 63);
        add("effects-single-row", stack, 63, 1);
        add("effects-subpixel-viewport", stack, 320, 180, { 0.153f, 0.137f, 0.001f, 0.001f }, false, true);
        add("bloom-uniform", uniform);
        cases.back().fixture = PostProcessValidationFixture::Uniform;
        add("bloom-uniform-single-pixel", uniform, 1, 1);
        cases.back().fixture = PostProcessValidationFixture::Uniform;
        auto customStages = stack;
        customStages.settings.fxaaEnabled.value = false;
        add("effects-custom-stages-no-fxaa", customStages, 321, 181, {}, true);
        cases.back().customScales = true;
        add("effects-custom-stages-no-fxaa-cropped", customStages, 321, 181, cropped, true);
        cases.back().customScales = true;
        auto inactive = stack; inactive.weight = 0;
        add("effects-zero-weight", inactive, 320, 180, {}, false, true);
        inactive.weight = 1; inactive.enabled = false;
        add("effects-disabled", inactive, 320, 180, {}, false, true);
        auto zeroIntensity = enabled;
        zeroIntensity.settings.bloomEnabled.value = true;
        zeroIntensity.settings.vignetteEnabled.value = true;
        zeroIntensity.settings.colorAdjustmentsEnabled.value = true;
        zeroIntensity.settings.exposureEnabled.value = true;
        add("effects-enabled-neutral-values", zeroIntensity, 320, 180, {}, false, true);
        auto contributions = stack;
        for (std::size_t index = 0; index < POST_PROCESS_WEIGHT_MEMBERS.size(); ++index)
            contributions.effects.*POST_PROCESS_WEIGHT_MEMBERS[index] = 0.2f + static_cast<float>(index) * 0.04f;
        add("effects-continuous-switches", contributions, 321, 181, {}, true);
        contributions.weight = 0.5f;
        add("effects-continuous-switches-camera-weight", contributions, 321, 181, {}, true);
        for (const auto member : POST_PROCESS_WEIGHT_MEMBERS)
            contributions.effects.*member = 0.0f;
        add("effects-zero-contributions", contributions, 320, 180, {}, false, true);
        add("effects-camera-removed", {}, 320, 180, {}, false, true);
        cases.back().fixture = PostProcessValidationFixture::NoCamera;

        auto scale = std::make_shared<DX12Shader>();
        if (!scale->load({ .bytecodePath = "Assets\\Shaders\\Compiled\\PostProcessValidation_psScale_ps.cso" }))
            return false;
        const std::array customPasses{
            PostProcessPassDescription{ .name = "EffectsValidationLdr", .stage = PostProcessStage::AfterToneMapping,
                .pixelShader = scale, .parameters = { 0.75f } },
            PostProcessPassDescription{ .name = "EffectsValidationHdr", .stage = PostProcessStage::BeforeToneMapping,
                .pixelShader = scale, .parameters = { 0.5f } },
        };

        std::ofstream report(directory / L"effects-results.json", std::ios::binary);
        report << "{\n  \"phase\": 3,\n  \"displayToleranceBytes\": " << MAX_ERROR << ",\n  \"cases\": [\n";
        double aaRatio = 1;
        for (std::size_t index = 0; index < cases.size(); ++index)
        {
            const auto& test = cases[index];
            if (!renderer.setPostProcessPasses({}) || !renderer.resize(test.width, test.height))
                return false;
            const auto descriptorsBefore = TextureManager::instance().getDescriptorHeap().getAllocatedCount();
            if (test.fixture == PostProcessValidationFixture::NoCamera)
            {
                if (!render(test.width, test.height, {}, stack, PostProcessValidationFixture::Panels)
                    || !render(test.width, test.height, {}, {}, PostProcessValidationFixture::NoCamera)
                    || renderer.getCameraPostProcessSnapshot().enabled || !renderer.setPostProcessPasses({}))
                {
                    LOG_ERROR("[EffectsValidation] Failed to reset PostEffects when removing the Camera.");
                    return false;
                }
            }
            // Prime the entire target, including preserved pixels outside a cropped camera.
            if (!render(test.width, test.height, {}, {}, test.fixture)
                || !render(test.width, test.height, test.viewport, {}, test.fixture))
                return false;
            DX12FrameCapture baseline;
            if (!renderer.captureFrame(baseline))
                return false;
            if (test.customScales && !renderer.setPostProcessPasses(customPasses))
                return false;
            const unsigned int frames = test.customScales ? 30 : 4;
            DX12FrameCapture firstGrainFrame;
            for (unsigned int frame = 0; frame < frames; ++frame)
            {
                auto camera = test.camera;
                const bool switching = std::string_view(test.name) == "effects-stack";
                if (switching && frame == 0) camera.enabled = false;
                if (switching && frame == 1) camera.weight = 0;
                if (switching && frame == 2) camera.weight = 0.5f;
                if (!render(test.width, test.height, test.viewport, camera, test.fixture))
                    return false;
                if (std::string_view(test.name) == "film-grain-temporal" && frame == 0
                    && !renderer.captureFrame(firstGrainFrame))
                    return false;
                if (switching && frame < 2)
                {
                    DX12FrameCapture inactiveFrame;
                    if (!renderer.captureFrame(inactiveFrame) || !identical(baseline.display, inactiveFrame.display)
                        || renderer.getPostProcessStatistics().drawCalls != 1
                        || renderer.getPostProcessStatistics().temporaryTargets != 0)
                    {
                        LOG_ERROR("[EffectsValidation] Camera enable/weight switching changed the inactive path.");
                        return false;
                    }
                }
            }
            DX12FrameCapture captured;
            if (!renderer.captureFrame(captured) || !renderer.validateDebugMessages()
                || !identical(baseline.hdr, captured.hdr))
            {
                LOG_ERROR("[EffectsValidation] '{}' changed scene HDR or emitted D3D12 diagnostics.", test.name);
                return false;
            }
            const auto* const hdrImage = captured.hdr.GetImage(0, 0, 0);
            const auto* const displayImage = captured.display.GetImage(0, 0, 0);
            if (!hdrImage || !displayImage || hdrImage->width != test.width || hdrImage->height != test.height
                || displayImage->width != test.width || displayImage->height != test.height
                || hdrImage->format != DXGI_FORMAT_R16G16B16A16_FLOAT
                || displayImage->format != DXGI_FORMAT_R8G8B8A8_UNORM)
            {
                LOG_ERROR("[EffectsValidation] '{}' incorrect HDR/display dimensions or formats.", test.name);
                return false;
            }
            if (test.exact && !identical(baseline.display, captured.display))
            {
                LOG_ERROR("[EffectsValidation] '{}' should preserve the exact display image.", test.name);
                return false;
            }
            const auto& statistics = renderer.getPostProcessStatistics();
            if (std::string_view(test.name) == "film-grain-temporal"
                && identical(firstGrainFrame.display, captured.display))
            {
                LOG_ERROR("[EffectsValidation] Film Grain did not vary between rendered frames.");
                return false;
            }
            const auto expected = reference(captured.hdr, test, statistics.filmGrainFrameIndex);
            const std::wstring name(test.name, test.name + std::strlen(test.name));
            if (!captured.save(directory, name) || !verifySaved(captured, directory, name))
                return false;
            if (test.fixture == PostProcessValidationFixture::NoCamera
                && renderer.getCameraPostProcessSnapshot().enabled)
            {
                LOG_ERROR("[EffectsValidation] Removing the Camera left an active PostEffect snapshot.");
                return false;
            }
            if (statistics.drawCalls != expected.draws || statistics.effectPasses != expected.effects
                || statistics.bloomLevels != expected.bloomLevels
                || statistics.temporaryTargets > 28
                || TextureManager::instance().getDescriptorHeap().getAllocatedCount() != descriptorsBefore + statistics.temporaryTargets
                || (expected.draws == 1 && statistics.temporaryTargets != 0))
            {
                LOG_ERROR("[EffectsValidation] '{}' incorrect draw/level/target counts: {}/{}/{} (expected {}/{}).",
                    test.name, statistics.drawCalls, statistics.bloomLevels, statistics.temporaryTargets, expected.draws, expected.bloomLevels);
                return false;
            }
            const Bounds bounds = pixelBounds(test);
            const CpuImage sceneHdr = readHdr(captured.hdr);
            const auto& display = *captured.display.GetImage(0, 0, 0);
            const auto& unchanged = *baseline.display.GetImage(0, 0, 0);
            unsigned int maxError = 0;
            for (std::uint32_t y = 0; y < test.height; ++y)
                for (std::uint32_t x = 0; x < test.width; ++x)
                {
                    const auto* const pixel = display.pixels + y * display.rowPitch + x * 4;
                    if (pixel[3] != 255)
                    {
                        LOG_ERROR("[EffectsValidation] '{}' invalid display alpha.", test.name);
                        return false;
                    }
                    if (!inside(bounds, x, y))
                    {
                        if (std::memcmp(pixel, unchanged.pixels + y * unchanged.rowPitch + x * 4, 4) != 0)
                        {
                            LOG_ERROR("[EffectsValidation] '{}' wrote outside the Camera viewport.", test.name);
                            return false;
                        }
                        continue;
                    }
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const float referenceValue = expected.image.at(x, y)[channel];
                        if (!std::isfinite(referenceValue))
                        {
                            LOG_ERROR("[EffectsValidation] '{}' nonfinite CPU reference.", test.name);
                            return false;
                        }
                        const auto difference = static_cast<unsigned int>(std::abs(
                            static_cast<int>(pixel[channel]) - static_cast<int>(displayByte(referenceValue))));
                        if (difference > maxError) maxError = difference;
                        if (difference > MAX_ERROR)
                        {
                            LOG_ERROR("[EffectsValidation] '{}' pixel ({},{}) channel {}: expected {} (linear {}), actual {}, error {}.",
                                test.name, x, y, channel, displayByte(referenceValue), referenceValue, pixel[channel], difference);
                            return false;
                        }
                        if (test.fixture == PostProcessValidationFixture::Uniform)
                        {
                            const auto& sourceColor = sceneHdr.at(x, y);
                            const float brightness = std::max({ sourceColor[0], sourceColor[1], sourceColor[2] });
                            const float threshold = test.camera.settings.bloomThreshold.value;
                            const float bloomContribution = threshold < 0
                                ? 1.0f : std::clamp(brightness - threshold, 0.0f, 1.0f);
                            const double bloomWeight = test.camera.enabled ? test.camera.weight
                                * test.camera.effects.bloom : 0.0f;
                            const double hdrValue = sourceColor[channel] * (1 + bloomContribution
                                * test.camera.settings.bloomIntensity.value * bloomWeight);
                            const float normalized = static_cast<float>(std::clamp(hdrValue * (2.51 * hdrValue + 0.03)
                                / (hdrValue * (2.43 * hdrValue + 0.59) + 0.14), 0.0, 1.0));
                            if (std::abs(static_cast<int>(pixel[channel]) - static_cast<int>(displayByte(normalized))) > 2)
                            {
                                LOG_ERROR("[EffectsValidation] '{}' Bloom brightness depends on pyramid depth.", test.name);
                                return false;
                            }
                        }
                    }
                }
            if (std::string_view(test.name) == "fxaa-diagonal")
            {
                aaRatio = edgeEnergy(captured.display) / edgeEnergy(baseline.display);
                if (aaRatio >= 0.9)
                {
                    LOG_ERROR("[EffectsValidation] FXAA did not reduce diagonal edge energy by at least 10%: {}.", aaRatio);
                    return false;
                }
            }
            report << "    {\"name\": \"" << test.name << "\", \"maxDisplayErrorBytes\": " << maxError
                << ", \"drawCalls\": " << statistics.drawCalls << ", \"bloomLevels\": " << statistics.bloomLevels
                << ", \"temporaryTargets\": " << statistics.temporaryTargets << "}"
                << (index + 1 == cases.size() ? "\n" : ",\n");
            LOG_INFO("[EffectsValidation] '{}' passed; max display error {}, draws {}, Bloom levels {}.",
                test.name, maxError, statistics.drawCalls, statistics.bloomLevels);
        }
        if (!renderer.setPostProcessPasses({}))
            return false;
        if (!renderer.resize(320, 180) || !renderer.setPostProcessPasses(customPasses))
            return false;
        for (unsigned int mode = 0; mode < 4; ++mode)
        {
            renderer.setDeferredRenderingEnabled(mode != 0);
            renderer.setDeferredDebugView(mode < 2 ? DeferredDebugView::FinalLighting
                : (mode == 2 ? DeferredDebugView::Albedo : DeferredDebugView::Emission));
            const auto fixture = mode == 1 ? PostProcessValidationFixture::Nothing : PostProcessValidationFixture::Panels;
            DX12FrameCapture before, after;
            if (!render(320, 180, {}, {}, fixture) || !renderer.captureFrame(before)
                || !render(320, 180, {}, stack, fixture) || !renderer.captureFrame(after)
                || !identical(before.display, after.display) || renderer.getPostProcessStatistics().drawCalls != 0
                || !renderer.validateDebugMessages())
            {
                LOG_ERROR("[EffectsValidation] Camera effects leaked into Forward/Nothing/debug mode {}.", mode);
                return false;
            }
        }
        renderer.setDeferredRenderingEnabled(true);
        renderer.setDeferredDebugView(DeferredDebugView::FinalLighting);
        if (!renderer.setPostProcessPasses({}))
            return false;
        if (!renderer.resize(320, 180))
            return false;
        PostProcessCameraSnapshot neutralPhase6;
        neutralPhase6.enabled = true;
        neutralPhase6.nearClip = 0.1f;
        neutralPhase6.farClip = 10.0f;
        neutralPhase6.perspectiveProjection = false;
        DX12FrameCapture panelsBaseline;
        DX12FrameCapture uniformBaseline;
        if (!render(320, 180, {}, neutralPhase6, PostProcessValidationFixture::Panels)
            || !renderer.captureFrame(panelsBaseline)
            || !render(320, 180, {}, neutralPhase6, PostProcessValidationFixture::Uniform)
            || !renderer.captureFrame(uniformBaseline))
            return false;
        struct Phase6Result
        {
            std::string name;
            PostProcessCameraSnapshot camera;
            PostProcessValidationFixture fixture;
            std::uint32_t expectedEffects;
            std::uint32_t expectedDraws;
            bool expectChange;
            DX12FrameCapture capture;
        };
        std::vector<Phase6Result> phase6Cases;
        auto dof = neutralPhase6;
        dof.settings.depthOfFieldEnabled.value = true;
        dof.settings.depthOfFieldFocusDistance.value = 2.0f;
        dof.settings.depthOfFieldFocusRange.value = 0.01f;
        dof.settings.depthOfFieldMaxBlur.value = 0;
        phase6Cases.push_back({ "dof-neutral", dof, PostProcessValidationFixture::Panels, 0, 1, false });
        dof.settings.depthOfFieldMaxBlur.value = 8.0f;
        phase6Cases.push_back({ "dof-depth-aware", dof, PostProcessValidationFixture::Panels, 1, 2, true });
        auto temporalAA = neutralPhase6;
        temporalAA.cameraGuid = ObjectGUID::generate();
        temporalAA.settings.temporalAAEnabled.value = true;
        phase6Cases.push_back({ "temporal-aa-history", temporalAA, PostProcessValidationFixture::Panels, 1, 2, true });
        auto motionBlur = neutralPhase6;
        motionBlur.cameraGuid = ObjectGUID::generate();
        motionBlur.settings.motionBlurEnabled.value = true;
        motionBlur.settings.motionBlurAmount.value = 0.75f;
        motionBlur.settings.motionBlurMaxVelocity.value = 12.0f;
        phase6Cases.push_back({ "motion-blur-velocity", motionBlur, PostProcessValidationFixture::Panels, 1, 2, true });
        auto automatic = neutralPhase6;
        automatic.settings.autoExposureEnabled.value = true;
        phase6Cases.push_back({ "auto-exposure-current-frame", automatic, PostProcessValidationFixture::Uniform, 10, 11, true });

        constexpr std::uint32_t LUT_SIZE = 16;
        DirectX::ScratchImage lutImage;
        HRESULT lutResult = lutImage.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,
            LUT_SIZE * LUT_SIZE, LUT_SIZE, 1, 1);
        if (FAILED(lutResult))
        {
            LOG_ERROR("[Phase6Validation] LUT fixture allocation failed (HRESULT: 0x{:08X}).",
                static_cast<unsigned long>(lutResult));
            return false;
        }
        auto* const lutPixels = lutImage.GetImage(0, 0, 0);
        for (std::uint32_t green = 0; green < LUT_SIZE; ++green)
            for (std::uint32_t blue = 0; blue < LUT_SIZE; ++blue)
                for (std::uint32_t red = 0; red < LUT_SIZE; ++red)
                {
                    auto* const pixel = lutPixels->pixels + green * lutPixels->rowPitch
                        + (blue * LUT_SIZE + red) * 4;
                    pixel[0] = static_cast<std::uint8_t>(std::lround((1.0f - static_cast<float>(red) / (LUT_SIZE - 1)) * 255));
                    pixel[1] = static_cast<std::uint8_t>(std::lround(static_cast<float>(green) / (LUT_SIZE - 1) * 255));
                    pixel[2] = static_cast<std::uint8_t>(std::lround(static_cast<float>(blue) / (LUT_SIZE - 1) * 255));
                    pixel[3] = 255;
                }
        const auto lutPath = directory / L"phase6-invert-red-lut.dds";
        lutResult = DirectX::SaveToDDSFile(*lutPixels, DirectX::DDS_FLAGS_NONE, lutPath.c_str());
        if (FAILED(lutResult))
        {
            LOG_ERROR("[Phase6Validation] LUT fixture save failed (HRESULT: 0x{:08X}).",
                static_cast<unsigned long>(lutResult));
            return false;
        }
        const AssetGUID lutGuid = TextureManager::instance().registerAssetPath(lutPath);
        if (!lutGuid.isValid())
            return false;
        auto lut = neutralPhase6;
        lut.settings.colorGradingLutEnabled.value = true;
        lut.settings.colorGradingLutIntensity.value = 1;
        lut.settings.colorGradingLutAsset = { ObjectGUID{ lutGuid.high, lutGuid.low }, true };
        phase6Cases.push_back({ "color-grading-lut-strip", lut, PostProcessValidationFixture::Uniform, 1, 3, true });

        auto combined = dof;
        combined.cameraGuid = ObjectGUID::generate();
        combined.settings.temporalAAEnabled.value = true;
        combined.settings.motionBlurEnabled.value = true;
        combined.settings.motionBlurAmount.value = 0.5f;
        combined.settings.autoExposureEnabled.value = true;
        combined.settings.colorGradingLutEnabled.value = true;
        combined.settings.colorGradingLutAsset = lut.settings.colorGradingLutAsset;
        phase6Cases.push_back({ "phase6-combined-stack", combined, PostProcessValidationFixture::Panels, 14, 16, true });

        for (auto& test : phase6Cases)
        {
            const auto& baseline = test.fixture == PostProcessValidationFixture::Uniform
                ? uniformBaseline : panelsBaseline;
            if (test.name == "temporal-aa-history"
                && !render(320, 180, {}, test.camera, PostProcessValidationFixture::Panels))
                return false;
            if ((test.name == "motion-blur-velocity" || test.name == "phase6-combined-stack")
                && !render(320, 180, {}, test.camera, PostProcessValidationFixture::Diagonal))
                return false;
            if (!render(320, 180, {}, test.camera, test.fixture)
                || !renderer.captureFrame(test.capture) || !renderer.validateDebugMessages()
                || !identical(baseline.hdr, test.capture.hdr))
            {
                LOG_ERROR("[Phase6Validation] '{}' changed raw HDR or emitted a D3D12 diagnostic.", test.name);
                return false;
            }
            const auto& statistics = renderer.getPostProcessStatistics();
            if (statistics.effectPasses != test.expectedEffects || statistics.drawCalls != test.expectedDraws
                || (test.expectChange == identical(baseline.display, test.capture.display)))
            {
                LOG_ERROR("[Phase6Validation] '{}' produced unexpected output/pass counts ({}, {}).",
                    test.name, statistics.effectPasses, statistics.drawCalls);
                return false;
            }
            const std::wstring name(test.name.begin(), test.name.end());
            if (!test.capture.save(directory, name) || !verifySaved(test.capture, directory, name))
                return false;
        }

        const auto& autoCapture = phase6Cases[4].capture;
        const CpuImage uniformHdr = readHdr(autoCapture.hdr);
        double averageLogLuminance = 0;
        for (const auto& pixel : uniformHdr.pixels)
            averageLogLuminance += std::log2(std::max(static_cast<double>(luma(pixel)), 0.0001));
        averageLogLuminance /= uniformHdr.pixels.size();
        const float autoEV = std::clamp(std::log2(automatic.settings.autoExposureKey.value)
            - static_cast<float>(averageLogLuminance) + automatic.settings.autoExposureCompensation.value,
            automatic.settings.autoExposureMinEV.value, automatic.settings.autoExposureMaxEV.value);
        const auto& autoDisplay = *autoCapture.display.GetImage(0, 0, 0);
        for (std::uint32_t y = 0; y < 180; ++y)
            for (std::uint32_t x = 0; x < 320; ++x)
            {
                const auto* const pixel = autoDisplay.pixels + y * autoDisplay.rowPitch + x * 4;
                const auto& source = uniformHdr.at(x, y);
                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    const double value = std::max(static_cast<double>(source[channel]) * std::exp2(autoEV), 0.0);
                    const double mapped = std::clamp(value * (2.51 * value + 0.03)
                        / (value * (2.43 * value + 0.59) + 0.14), 0.0, 1.0);
                    if (std::abs(static_cast<int>(pixel[channel])
                        - static_cast<int>(displayByte(static_cast<float>(mapped)))) > MAX_ERROR)
                    {
                        LOG_ERROR("[Phase6Validation] Current-frame auto exposure differs from the CPU reference.");
                        return false;
                    }
                }
            }

        const auto& lutCapture = phase6Cases[5].capture;
        const auto& lutBaselineImage = *uniformBaseline.display.GetImage(0, 0, 0);
        const auto& lutDisplayImage = *lutCapture.display.GetImage(0, 0, 0);
        unsigned int maximumLutError = 0;
        for (std::uint32_t y = 0; y < 180; ++y)
            for (std::uint32_t x = 0; x < 320; ++x)
            {
                const auto* const before = lutBaselineImage.pixels + y * lutBaselineImage.rowPitch + x * 4;
                const auto* const after = lutDisplayImage.pixels + y * lutDisplayImage.rowPitch + x * 4;
                const float encodedRed = before[0] / 255.0f;
                const float linearRed = encodedRed <= 0.04045f ? encodedRed / 12.92f
                    : std::pow((encodedRed + 0.055f) / 1.055f, 2.4f);
                const auto expectedRed = displayByte(1.0f - linearRed);
                maximumLutError = std::max(maximumLutError,
                    static_cast<unsigned int>(std::abs(static_cast<int>(after[0]) - static_cast<int>(expectedRed))));
                if (maximumLutError > MAX_ERROR
                    || std::abs(static_cast<int>(after[1]) - static_cast<int>(before[1])) > MAX_ERROR
                    || std::abs(static_cast<int>(after[2]) - static_cast<int>(before[2])) > MAX_ERROR)
                {
                    LOG_ERROR("[Phase6Validation] LUT output differs from the independent CPU lookup reference.");
                    return false;
                }
            }
        std::ofstream phase6Report(directory / L"phase6-results.json", std::ios::binary);
        phase6Report << "{\n  \"phase\": 6,\n  \"cases\": " << phase6Cases.size()
            << ",\n  \"depthAwareEffects\": 2,\n  \"lutMaximumDisplayErrorBytes\": " << maximumLutError
            << ",\n  \"autoExposureCpuReference\": true,\n  \"combinedPassOrdering\": true,\n  \"passed\": true\n}\n";
        phase6Report.close();
        if (!phase6Report)
        {
            LOG_ERROR("[Phase6Validation] Cannot save phase6-results.json.");
            return false;
        }

        constexpr float PHASE7_DELTA_TIME = 0.1f;
        auto temporal = automatic;
        temporal.cameraGuid = ObjectGUID::generate();
        temporal.deltaTimeSeconds = 0;
        temporal.settings.autoExposureSpeedUp.value = 2.0f;
        temporal.settings.autoExposureSpeedDown.value = 4.0f;
        float temporalEV = autoEV;
        std::size_t phase7CasesPassed = 0;
        const auto captureTemporal = [&](const char* name, const PostProcessCameraSnapshot& camera,
            const float expectedEV)
        {
            DX12FrameCapture capture;
            if (!render(320, 180, {}, camera, PostProcessValidationFixture::Uniform)
                || !renderer.captureFrame(capture) || !renderer.validateDebugMessages()
                || !identical(uniformBaseline.hdr, capture.hdr))
            {
                LOG_ERROR("[Phase7Validation] '{}' changed raw HDR or emitted a D3D12 diagnostic.", name);
                return false;
            }
            const auto& statistics = renderer.getPostProcessStatistics();
            if (statistics.effectPasses != 10 || statistics.drawCalls != 11)
            {
                LOG_ERROR("[Phase7Validation] '{}' produced unexpected temporal exposure pass counts.", name);
                return false;
            }
            const auto& display = *capture.display.GetImage(0, 0, 0);
            for (std::uint32_t y = 0; y < 180; ++y)
                for (std::uint32_t x = 0; x < 320; ++x)
                {
                    const auto* const pixel = display.pixels + y * display.rowPitch + x * 4;
                    const auto& source = uniformHdr.at(x, y);
                    for (std::size_t channel = 0; channel < 3; ++channel)
                    {
                        const double value = std::max(static_cast<double>(source[channel])
                            * std::exp2(expectedEV), 0.0);
                        const double mapped = std::clamp(value * (2.51 * value + 0.03)
                            / (value * (2.43 * value + 0.59) + 0.14), 0.0, 1.0);
                        if (std::abs(static_cast<int>(pixel[channel])
                            - static_cast<int>(displayByte(static_cast<float>(mapped)))) > MAX_ERROR)
                        {
                            LOG_ERROR("[Phase7Validation] '{}' differs from CPU adapted EV {}.", name, expectedEV);
                            return false;
                        }
                    }
                }
            const std::wstring filename(name, name + std::strlen(name));
            if (!capture.save(directory, filename) || !verifySaved(capture, directory, filename))
                return false;
            ++phase7CasesPassed;
            return true;
        };
        if (!captureTemporal("auto-exposure-history-bootstrap", temporal, temporalEV))
            return false;

        temporal.deltaTimeSeconds = PHASE7_DELTA_TIME;
        temporal.settings.autoExposureCompensation.value = 2.0f;
        float targetEV = std::clamp(autoEV + temporal.settings.autoExposureCompensation.value,
            temporal.settings.autoExposureMinEV.value, temporal.settings.autoExposureMaxEV.value);
        float blend = 1.0f - std::exp(-temporal.settings.autoExposureSpeedUp.value * temporal.deltaTimeSeconds);
        temporalEV = std::lerp(temporalEV, targetEV, blend);
        if (!captureTemporal("auto-exposure-adapt-increasing", temporal, temporalEV))
            return false;

        temporal.deltaTimeSeconds = 0.25f;
        blend = 1.0f - std::exp(-temporal.settings.autoExposureSpeedUp.value * temporal.deltaTimeSeconds);
        temporalEV = std::lerp(temporalEV, targetEV, blend);
        if (!captureTemporal("auto-exposure-adapt-converging", temporal, temporalEV))
            return false;

        temporal.deltaTimeSeconds = PHASE7_DELTA_TIME;
        temporal.settings.autoExposureCompensation.value = -2.0f;
        targetEV = std::clamp(autoEV + temporal.settings.autoExposureCompensation.value,
            temporal.settings.autoExposureMinEV.value, temporal.settings.autoExposureMaxEV.value);
        blend = 1.0f - std::exp(-temporal.settings.autoExposureSpeedDown.value * temporal.deltaTimeSeconds);
        temporalEV = std::lerp(temporalEV, targetEV, blend);
        if (!captureTemporal("auto-exposure-adapt-decreasing", temporal, temporalEV))
            return false;

        temporal.deltaTimeSeconds = 0;
        temporal.settings.autoExposureCompensation.value = 1.0f;
        if (!captureTemporal("auto-exposure-zero-delta-freezes", temporal, temporalEV))
            return false;

        temporal.cameraGuid = ObjectGUID::generate();
        temporal.deltaTimeSeconds = PHASE7_DELTA_TIME;
        temporal.settings.autoExposureCompensation.value = -1.0f;
        temporalEV = std::clamp(autoEV + temporal.settings.autoExposureCompensation.value,
            temporal.settings.autoExposureMinEV.value, temporal.settings.autoExposureMaxEV.value);
        if (!captureTemporal("auto-exposure-camera-history-isolation", temporal, temporalEV))
            return false;

        temporal.settings.autoExposureEnabled.value = false;
        if (!render(320, 180, {}, temporal, PostProcessValidationFixture::Uniform)
            || !renderer.validateDebugMessages())
            return false;
        temporal.settings.autoExposureEnabled.value = true;
        temporal.settings.autoExposureCompensation.value = 1.0f;
        temporalEV = std::clamp(autoEV + temporal.settings.autoExposureCompensation.value,
            temporal.settings.autoExposureMinEV.value, temporal.settings.autoExposureMaxEV.value);
        if (!captureTemporal("auto-exposure-disabled-history-reset", temporal, temporalEV))
            return false;

        std::ofstream phase7Report(directory / L"phase7-results.json", std::ios::binary);
        phase7Report << "{\n  \"phase\": 7,\n  \"temporalCases\": " << phase7CasesPassed
            << ",\n  \"brightenAndDarkenRates\": true,\n  \"zeroDeltaFreezesHistory\": true,\n"
            "  \"cameraSwitchAndDisableResetHistory\": true,\n  \"cpuReference\": true,\n  \"passed\": true\n}\n";
        phase7Report.close();
        if (!phase7Report)
        {
            LOG_ERROR("[Phase7Validation] Cannot save phase7-results.json.");
            return false;
        }
        report << "  ],\n  \"fxaaDiagonalEdgeEnergyRatio\": " << aaRatio
            << ",\n  \"cameraBypassChecks\": 4,\n  \"cameraSettingSwitchChecks\": 2,\n"
            << "  \"savedImageRoundTrips\": " << cases.size() << ",\n  \"passed\": true\n}\n";
        report.close();
        if (!report)
        {
            LOG_ERROR("[EffectsValidation] Cannot save effects-results.json.");
            return false;
        }
        return true;
    }
}
