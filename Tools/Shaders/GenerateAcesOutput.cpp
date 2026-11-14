#include <OpenColorIO\OpenColorIO.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    namespace OCIO = OCIO_NAMESPACE;

    constexpr std::string_view license = R"(/*
Generated using OpenColorIO 2.6.0.
Copyright Contributors to the OpenColorIO Project.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
)";

    void replaceOnce(std::string& text, const std::string& from, const std::string& to)
    {
        const auto position = text.find(from);
        if (position == std::string::npos || text.find(from, position + from.size()) != std::string::npos)
            throw std::runtime_error("Unexpected OCIO shader declaration: " + from);
        text.replace(position, from.size(), to);
    }

    void replaceAll(std::string& text, const std::string& from, const std::string& to)
    {
        std::size_t position = 0;
        while ((position = text.find(from, position)) != std::string::npos)
        {
            text.replace(position, from.size(), to);
            position += to.size();
        }
    }

    void writeFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output)
            throw std::runtime_error("Cannot write generated file: " + path.string());
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: GenerateAcesOutput <repository-root>\n";
        return 1;
    }
    try
    {
        if (std::string_view(OCIO::GetVersion()) != "2.6.0")
            throw std::runtime_error("This generator requires OpenColorIO 2.6.0.");
        const auto config = OCIO::Config::CreateFromBuiltinConfig("cg-config-v4.0.0_aces-v2.0_ocio-v2.5");
        const auto transform = OCIO::DisplayViewTransform::Create();
        transform->setSrc("Linear Rec.709 (sRGB)");
        transform->setDisplay("sRGB - Display");
        transform->setView("ACES 2.0 - SDR 100 nits (Rec.709)");
        auto description = OCIO::GpuShaderDesc::CreateShaderDesc();
        description->setLanguage(OCIO::GPU_LANGUAGE_HLSL_SM_5_0);
        description->setFunctionName("applyAces2Output");
        description->setResourcePrefix("ocio_");
        config->getProcessor(transform)->getDefaultGPUProcessor()->extractGpuShaderInfo(description);
        if (description->getNumUniforms() != 0 || description->getNumTextures() != 2
            || description->getNum3DTextures() != 0)
            throw std::runtime_error("Unexpected ACES 2 shader resources.");

        constexpr std::array names{ "ocio_reach_m_table_0", "ocio_gamut_cusp_table_0" };
        constexpr std::array channels{ OCIO::GpuShaderCreator::TEXTURE_RED_CHANNEL,
            OCIO::GpuShaderCreator::TEXTURE_RGB_CHANNEL };
        std::string shader = description->getShaderText();
        std::ostringstream header;
        header.imbue(std::locale::classic());
        header << license << "#pragma once\n\n#include <array>\n\nnamespace Engine::Aces2Luts\n{\n";
        header << std::scientific << std::setprecision(std::numeric_limits<float>::max_digits10);
        for (unsigned index = 0; index < names.size(); ++index)
        {
            const char* name = nullptr;
            const char* sampler = nullptr;
            unsigned width = 0, height = 0;
            OCIO::GpuShaderCreator::TextureType channel;
            OCIO::GpuShaderCreator::TextureDimensions dimension;
            OCIO::Interpolation interpolation;
            description->getTexture(index, name, sampler, width, height, channel, dimension, interpolation);
            if (!name || !sampler || std::string_view(name) != names[index]
                || width != 363 || height != 1 || channel != channels[index]
                || dimension != OCIO::GpuShaderCreator::TEXTURE_1D || interpolation != OCIO::INTERP_NEAREST)
                throw std::runtime_error("Unexpected ACES 2 LUT layout.");
            const float* values = nullptr;
            description->getTextureValues(index, values);
            if (!values)
                throw std::runtime_error("Missing ACES 2 LUT values.");
            const unsigned count = width * (index == 0 ? 1u : 3u);
            header << "    inline constexpr std::array<float, " << count << "> "
                << (index == 0 ? "reach" : "cusp") << "{\n        ";
            for (unsigned value = 0; value < count; ++value)
            {
                header << values[value] << 'f';
                if (value + 1 != count)
                    header << ',';
                header << ((value + 1) % 6 == 0 ? "\n        " : " ");
            }
            header << "\n    };\n";
            replaceOnce(shader, std::string("Texture1D ") + name + ";",
                std::string("Texture1D ") + name + " : register(t" + std::to_string(index + 3) + ");");
            replaceOnce(shader, std::string("SamplerState ") + sampler + ";", "");
            replaceAll(shader, sampler, "acesPointSampler");
        }
        header << "}\n";
        const std::filesystem::path root(argv[1]);
        writeFile(root / "Assets" / "Shaders" / "Aces2Output.hlsli",
            std::string(license) + "\nSamplerState acesPointSampler : register(s1);\n" + shader);
        writeFile(root / "Source" / "Graphics" / "PostProcess" / "Aces2Luts.h", header.str());
        return 0;
    }
    catch (const OCIO::Exception& error)
    {
        std::cerr << "OpenColorIO: " << error.what() << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
