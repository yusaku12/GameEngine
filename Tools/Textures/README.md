# Offline texture conversion

Keep authoring images unchanged. Convert them to DDS before packaging the game;
the runtime does not compress textures. The converter uses the vendored
DirectXTex `texconv` source, not a downloaded executable.

## Build

Run from the repository root with Visual Studio C++ tools and a Windows SDK:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Textures\BuildTexconv.ps1
```

The execution-policy override applies only to this process. The build script
accepts `-MSBuild <path>` for an explicit MSBuild installation.

## Convert

### Batch-file entry point

Double-click [ConvertTexture.bat](./ConvertTexture.bat), or drag **one** source
image onto it. Enter the source path (if not dropped), a usage number, and an
output path. Press Enter at the output prompt to use `Cooked\<source-name>.dds`
beside the source. Existing outputs require explicit confirmation; declining
cancels without modifying them. The console stays open to show success/errors.
Usage is mandatory: numerical images must not be treated as sRGB color.

For unattended conversion, pass the existing converter's named arguments:

```powershell
.\Tools\Textures\ConvertTexture.bat -Source ".\Assets\Textures\albedo.png" -Output ".\Assets\Textures\Cooked\albedo.dds" -Usage Color
```

Argument mode does not pause and returns a nonzero exit code on failure.
Quote paths containing spaces. Multiple-file drag-and-drop is not supported.
If texconv is missing, run [BuildTexconv.bat](./BuildTexconv.bat) first; this
requires Visual Studio C++ tools and the Windows SDK.

The batch files reuse the PowerShell scripts below. They do not automatically
switch the game's texture references to DDS.

### PowerShell entry point

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Textures\ConvertTexture.ps1 `
    -Source .\Assets\Textures\albedo.png -Output .\Assets\Textures\Cooked\albedo.dds -Usage Color

powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\Textures\ConvertTexture.ps1 `
    -Source .\Assets\Textures\orm.png -Output .\Assets\Textures\Cooked\orm.dds -Usage Data
```

| Usage | DDS format | Input interpretation | Default mips |
| --- | --- | --- | --- |
| Color | BC7_UNORM_SRGB | sRGB color with straight alpha | Full chain |
| Data | BC7_UNORM | Linear numerical data, all RGBA channels | Full chain |
| Mask | BC4_UNORM | Linear mask in the **red** channel | Full chain |
| UI | R8G8B8A8_UNORM_SRGB | sRGB color with straight alpha | One |
| HDR | BC6H_SF16 | Linear Radiance `.hdr`, no alpha | Full chain |

- Supported source extensions: PNG, JPEG, BMP, TIFF, TGA, HDR. Only a single
  Texture2D is accepted. DDS inputs are deliberately rejected to avoid accidental
  repeated lossy compression. Always regenerate from the original source.
- Color/UI explicitly interpret input as sRGB; Data/Mask ignore source gamma
  metadata. Do not use Color for roughness, metallic, AO, height or normal data.
- BC conversion uses the CPU high-quality codec (`-nogpu -bc x`), including
  BC7 three-subset search. This can be slow for large images, but happens offline.
- No automatic resizing: BC top-level width/height must be divisible by four,
  and dimensions must not exceed 16384. Odd-size color/UI can use the
  uncompressed UI preset; numerical data should be explicitly resized offline.
- `-MipLevels 0` requests a full chain; `-MipLevels 1` disables mips.
  Other counts must fit the image. UI accepts an explicit mip count too.
- Color accepts `-AlphaCutoff 0.5` to preserve mip alpha coverage for alpha-tested
  foliage/hair. Match this to the material's cutoff; do not enable it for ordinary
  alpha blending. Color mips filter in linear light and alpha separately.
- Normal maps can use **Data** to retain the existing XYZ representation.
  This is not a normal-specific mip baker: for highest-quality normal mips,
  use a baker that renormalizes vectors. BC5 is not offered because the current
  shader path would also need to reconstruct Z.
- Existing DDS outputs are rejected unless `-Force` is supplied. Conversion
  uses a unique staging directory on the output volume. The DX10 DDS header,
  format, dimensions, mip count and exact payload size are validated before an
  atomic move/replace. Failures leave the source and previous DDS untouched.
- `-Texconv <path>` selects a different executable if needed. The default is
  the Release x64 executable built by [BuildTexconv.ps1](./BuildTexconv.ps1).

## Runtime integration

Point the texture asset path/material reference at the cooked `.dds`, rather
than the authoring image. Conversion deliberately does not rewrite existing
materials, GUID registrations or authoring images. Keep logical asset GUIDs
stable when changing the registered runtime path.

[Texture](../../Source/Graphics/Texture/Texture.h) loads DDS with its stored
format and mip chain. `TextureColorSpace::Auto` preserves the loader's format;
use the correct DDS preset rather than guessing the purpose from the filename.
An explicit Linear/SRGB override retags compatible formats **before** resource
creation and mip filtering. It changes interpretation, not the base-level
pixel values. Formats without an sRGB variant remain Linear.

- `allowCompression = true` retains BC pixels without a full pixel copy or
  runtime recompression. `false` explicitly decompresses every existing mip.
- `generateMips = true` generates mips only for uncompressed single-level
  images. A compressed single-level image is retained with a warning; its mips
  must be cooked offline (or explicitly decompressed first).
- To keep UI at one mip, also set `TextureLoadDesc::generateMips = false` when
  loading it. The default runtime setting generates mips for uncompressed DDS.
- Texture2D and a single cubemap are supported. Array textures, cube arrays,
  volumes and typeless/planar inputs are rejected rather than given an
  incompatible SRV.
- Cache entries include load options, so loading the same DDS as BC/decoded or
  sRGB/Linear cannot accidentally return a different option's resource.
- `getGPUMemorySize()` reports pixel payload bytes including full BC tail
  blocks and all cube faces, **not** GPU allocation size or upload padding.
- Upload buffers, command lists and allocators are held until the upload fence
  completes. The fence must outlive its textures; destruction calls `finalize()`.

GPU compression is lossy. A BC7 RGBA8 texture normally uses one quarter of
uncompressed pixel storage; this does not mean the DDS file will be smaller
than a PNG. Inspect gradients, text, transparency and normal-map shading in the
actual game. Alpha is also lossy, even on some opaque BC7 blocks; use UI when
exact color/alpha bytes are required. Neither conversion nor packaging prevents
asset extraction.
