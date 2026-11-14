#pragma once

#include "Assets\PostProcess\PostProcessOcioAsset.h"

namespace Engine
{
    /**
     * @brief Generate and compile an OCIO GPU transform for an Editor-authored asset.
     * @param recipe Config path, color spaces, optional look and processing stage.
     * @param destination Receives the compiled shader and LUTs on success.
     * @return True on success. Failures are logged and leave destination unchanged.
     * @thread_safety Not thread-safe. Call from the Editor main thread.
     */
    bool compilePostProcessOcio(const PostProcessOcioAsset& recipe, PostProcessOcioAsset& destination);
}
