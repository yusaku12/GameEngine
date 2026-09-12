#include "Pch.h"
#include "Editor\Scene\SceneDocument.h"
#include "Core\Scene\SceneSerializer.h"

namespace Engine::Editor
{
    bool SceneDocument::save(const Scene& scene)
    {
        if (!hasPath())
        {
            m_status = "Save failed: no path selected.";
            return false;
        }
        return saveAs(scene, m_path);
    }

    bool SceneDocument::saveAs(const Scene& scene, const std::filesystem::path& path)
    {
        if (path.empty())
        {
            m_status = "Save failed: invalid path.";
            return false;
        }

        std::filesystem::path normalizedPath = path;
        if (!normalizedPath.has_extension())
            normalizedPath.replace_extension(".scene");
        if (!Serialization::SceneSerializer{}.save(normalizedPath, scene))
        {
            m_status = "Save failed: " + normalizedPath.string();
            return false;
        }

        m_path = std::move(normalizedPath);
        m_status = "Saved: " + m_path.string();
        return true;
    }

    bool SceneDocument::load(Scene& scene, const std::filesystem::path& path)
    {
        if (path.empty() || !Serialization::SceneSerializer{}.load(path, scene))
        {
            m_status = "Load failed: " + path.string();
            return false;
        }

        m_path = path;
        m_status = "Loaded: " + m_path.string();
        return true;
    }

    void SceneDocument::reset() noexcept
    {
        m_path.clear();
        m_status = "New scene created.";
    }
} // namespace Engine::Editor