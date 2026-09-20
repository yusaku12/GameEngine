#include "Pch.h"
#include "Core\Serialization\ComponentSerialization.h"
#include "Core\GameObject\Component\TransformComponent.h"
#include "Core\GameObject\ComponentRegistry.h"
#include "Core\GameObject\GameObject.h"

namespace Engine::Serialization
{
    bool captureComponents(const GameObject& object, std::vector<ComponentSnapshot>& snapshots)
    {
        snapshots.clear();
        bool valid = true;
        object.forEachComponent([&](const Component& component, const std::type_index type)
            {
                if (!valid || type == std::type_index(typeid(TransformComponent)))
                    return;
                const ComponentTypeInfo* info = ComponentRegistry::instance().get(type);
                if (info == nullptr)
                    return;
                ComponentSnapshot snapshot;
                snapshot.type = info->name;
                snapshot.enabled = component.isEnabled();
                snapshot.payloadVersion = component.getPayloadVersion();
                valid = component.serializePayload(snapshot.payload);
                if (valid)
                    snapshots.push_back(std::move(snapshot));
            });
        if (!valid)
        {
            snapshots.clear();
            return false;
        }
        std::stable_sort(snapshots.begin(), snapshots.end(), [](const ComponentSnapshot& left, const ComponentSnapshot& right)
            {
                return left.type < right.type;
            });
        return true;
    }

    bool restoreComponents(GameObject& object, const std::vector<ComponentSnapshot>& snapshots)
    {
        for (const ComponentSnapshot& snapshot : snapshots)
        {
            Component* component = object.addComponent(snapshot.type);
            if (component == nullptr)
                continue;
            component->setEnabled(snapshot.enabled);
            if ((snapshot.payloadVersion != 0 || !snapshot.payload.empty())
                && !component->deserializePayload(snapshot.payloadVersion, snapshot.payload))
                return false;
        }
        return true;
    }
}