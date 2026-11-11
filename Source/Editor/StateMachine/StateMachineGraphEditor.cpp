#include "Pch.h"
#include "Editor\StateMachine\StateMachineGraphEditor.h"
#include "Assets\Animation\AnimationAssetManager.h"
#include "Assets\StateMachine\Serialization\StateMachineAssetSerializer.h"
#include "Assets\StateMachine\StateMachineAssetManager.h"
#include "Core\Containers\String.h"
#include "Core\System\Dialog.h"
#include "Core\Threading\MainThreadDispatcher.h"
#include "StateMachine\StateMachineActionRegistry.h"
#include <imgui.h>

namespace Engine
{
    namespace
    {
        StateMachineID nextStateId(const std::vector<StateMachineState>& states)
        {
            StateMachineID next = 1;
            for (const StateMachineState& state : states)
            {
                if (state.id == (std::numeric_limits<StateMachineID>::max)())
                    return 0;
                next = std::max(next, state.id + 1);
            }
            return next;
        }

        StateMachineTransitionID nextTransitionId(const std::vector<StateMachineTransition>& transitions)
        {
            StateMachineTransitionID next = 1;
            for (const StateMachineTransition& transition : transitions)
            {
                if (transition.id == (std::numeric_limits<StateMachineTransitionID>::max)())
                    return 0;
                next = std::max(next, transition.id + 1);
            }
            return next;
        }

        StateMachineParameterID nextParameterId(const std::vector<StateMachineParameter>& parameters)
        {
            StateMachineParameterID next = 1;
            for (const StateMachineParameter& parameter : parameters)
            {
                if (parameter.id == (std::numeric_limits<StateMachineParameterID>::max)())
                    return 0;
                next = std::max(next, parameter.id + 1);
            }
            return next;
        }

        const char* actionTypeName(const StateMachineActionType type) noexcept
        {
            switch (type)
            {
            case StateMachineActionType::EmitEvent: return "Emit Event";
            case StateMachineActionType::PlayAnimationClip: return "Play Animation Clip";
            case StateMachineActionType::Custom: return "Custom";
            }
            return "Invalid";
        }

        ImVec2 nodePosition(const ImVec2 origin, const ImVec2 pan, const float x, const float y,
            const float zoom) noexcept
        {
            return { origin.x + pan.x + x * zoom, origin.y + pan.y + y * zoom };
        }

        bool editCondition(StateMachineCondition& condition, const std::vector<StateMachineParameter>& parameters)
        {
            bool changed = false;
            const auto current = std::ranges::find(parameters, condition.parameterId, &StateMachineParameter::id);
            int parameterIndex = current == parameters.end() ? 0
                : static_cast<int>(current - parameters.begin());
            if (ImGui::BeginCombo("Parameter", parameters.empty() ? "<none>"
                : parameters[static_cast<std::size_t>(parameterIndex)].name.c_str()))
            {
                for (std::size_t index = 0; index < parameters.size(); ++index)
                {
                    ImGui::PushID(static_cast<int>(index));
                    if (ImGui::Selectable(parameters[index].name.c_str(),
                        static_cast<int>(index) == parameterIndex))
                    {
                        parameterIndex = static_cast<int>(index);
                        condition.parameterId = parameters[index].id;
                        changed = true;
                        switch (parameters[index].type)
                        {
                        case StateMachineParameterType::Float:
                        case StateMachineParameterType::Int:
                            condition.mode = StateMachineConditionMode::Greater;
                            break;
                        case StateMachineParameterType::Bool:
                            condition.mode = StateMachineConditionMode::If;
                            break;
                        case StateMachineParameterType::Trigger:
                            condition.mode = StateMachineConditionMode::Triggered;
                            break;
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (parameters.empty())
                return changed;

            const StateMachineParameter& parameter = parameters[static_cast<std::size_t>(parameterIndex)];
            const char* modes = nullptr;
            int mode = 0;
            switch (parameter.type)
            {
            case StateMachineParameterType::Float:
            case StateMachineParameterType::Int:
                modes = "Greater\0Less\0Equals\0Not Equal\0";
                mode = static_cast<int>(condition.mode);
                mode = std::clamp(mode, 0, 3);
                if (ImGui::Combo("Condition", &mode, modes))
                {
                    condition.mode = static_cast<StateMachineConditionMode>(mode);
                    changed = true;
                }
                if (parameter.type == StateMachineParameterType::Float)
                    changed |= ImGui::InputFloat("Threshold", &condition.floatThreshold);
                else
                    changed |= ImGui::InputInt("Threshold", &condition.intThreshold);
                break;
            case StateMachineParameterType::Bool:
                mode = condition.mode == StateMachineConditionMode::IfNot ? 1 : 0;
                if (ImGui::Combo("Condition", &mode, "If\0If Not\0"))
                {
                    condition.mode = mode == 0 ? StateMachineConditionMode::If : StateMachineConditionMode::IfNot;
                    changed = true;
                }
                break;
            case StateMachineParameterType::Trigger:
                changed |= condition.mode != StateMachineConditionMode::Triggered;
                condition.mode = StateMachineConditionMode::Triggered;
                ImGui::TextDisabled("Triggered");
                break;
            }
            return changed;
        }
    }

    bool StateMachineGraphEditor::createAsset()
    {
        StateMachineAsset asset;
        asset.states.push_back({ 1, "Start", 80.0f, 80.0f });
        asset.defaultState = 1;
        const StateMachineAssetHandle handle = StateMachineAssetManager::instance().create(std::move(asset));
        if (!handle.isValid())
        {
            m_status = "Could not create State Machine. See the engine log.";
            return false;
        }
        m_handle = handle;
        m_draft = *StateMachineAssetManager::instance().get(handle);
        invalidateStateIndex();
        m_selectedState = m_draft.defaultState;
        m_linkSource = 0;
        m_hasAsset = true;
        m_dirty = true;
        m_status = "New State Machine. Save to assign a file path.";
        return true;
    }

    bool StateMachineGraphEditor::openAsset(const HWND owner)
    {
        static constexpr std::array filters = { FileDialogFilter{ L"State Machine", L"*.statemachine" } };
        std::vector<std::filesystem::path> paths;
        const DialogResult result = Dialog::openFile(paths, L"Open State Machine",
            L"Assets", filters, false, owner);
        if (result == DialogResult::Cancel)
            return false;
        if (result != DialogResult::Ok || paths.size() != 1)
        {
            LOG_ERROR("[StateMachineEditor] State Machine file selection failed.");
            m_status = "Open failed. See the engine log.";
            return false;
        }

        const StateMachineAssetHandle handle = StateMachineAssetManager::instance().load(paths.front());
        const auto asset = StateMachineAssetManager::instance().get(handle);
        if (!asset)
        {
            m_status = "Could not load State Machine. See the engine log.";
            return false;
        }
        m_handle = handle;
        m_draft = *asset;
        invalidateStateIndex();
        m_selectedState = m_draft.defaultState;
        m_linkSource = 0;
        m_hasAsset = true;
        m_dirty = false;
        m_status.clear();
        return true;
    }

    bool StateMachineGraphEditor::saveAsset(const HWND owner)
    {
        if (!m_hasAsset)
            return false;
        if (!validateStateMachineAsset(m_draft))
        {
            m_status = "Graph validation failed. See the engine log.";
            return false;
        }
        StateMachineAssetManager& manager = StateMachineAssetManager::instance();
        if (!manager.update(m_handle, m_draft))
        {
            m_status = "Could not publish the graph. See the engine log.";
            return false;
        }
        std::filesystem::path path = manager.getPath(m_handle);
        if (path.empty())
        {
            static constexpr std::array filters = { FileDialogFilter{ L"State Machine", L"*.statemachine" } };
            if (Dialog::saveFile(path, L"Save State Machine", L"Assets",
                L"statemachine", filters, owner) != DialogResult::Ok)
            {
                m_status = "Save cancelled. The graph remains dirty.";
                return false;
            }
        }
        if (!manager.save(m_handle, path))
        {
            m_status = "Could not save State Machine. See the engine log.";
            return false;
        }
        m_dirty = false;
        m_status = "Saved.";
        return true;
    }

    bool StateMachineGraphEditor::addState(const float x, const float y)
    {
        if (!m_hasAsset)
            return false;
        const StateMachineID id = nextStateId(m_draft.states);
        if (id == 0)
        {
            m_status = "State ID space is exhausted.";
            return false;
        }
        m_draft.states.push_back({ id, "State " + std::to_string(id), x, y });
        invalidateStateIndex();
        if (m_draft.defaultState == 0)
            m_draft.defaultState = id;
        m_selectedState = id;
        m_dirty = true;
        return true;
    }

    bool StateMachineGraphEditor::addTransition(const StateMachineID source, const StateMachineID destination)
    {
        if (source == destination || findState(source) == nullptr || findState(destination) == nullptr)
        {
            m_status = "A transition requires two different existing states.";
            return false;
        }
        const StateMachineTransitionID id = nextTransitionId(m_draft.transitions);
        if (id == 0)
        {
            m_status = "Transition ID space is exhausted.";
            return false;
        }
        m_draft.transitions.push_back({ id, source, destination });
        m_dirty = true;
        m_status = "Transition created. Configure its event/conditions in the Inspector.";
        return true;
    }

    void StateMachineGraphEditor::rebuildStateIndex() const
    {
        m_stateIndexDirty = true;
        m_stateIndices.clear();
        m_stateIndices.reserve(m_draft.states.size());
        for (std::size_t index = 0; index < m_draft.states.size(); ++index)
            m_stateIndices.emplace(m_draft.states[index].id, index);
        m_stateIndexDirty = false;
    }

    StateMachineState* StateMachineGraphEditor::findState(const StateMachineID id)
    {
        if (m_stateIndexDirty)
            rebuildStateIndex();
        const auto found = m_stateIndices.find(id);
        return found != m_stateIndices.end() ? &m_draft.states[found->second] : nullptr;
    }

    const StateMachineState* StateMachineGraphEditor::findState(const StateMachineID id) const
    {
        if (m_stateIndexDirty)
            rebuildStateIndex();
        const auto found = m_stateIndices.find(id);
        return found != m_stateIndices.end() ? &m_draft.states[found->second] : nullptr;
    }

    void StateMachineGraphEditor::draw(bool& visible)
    {
        if (!visible)
            return;
        ImGui::SetNextWindowSize(ImVec2(1050.0f, 650.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("State Machine", &visible))
        {
            ImGui::End();
            return;
        }

        ImGui::BeginDisabled(m_dirty);
        if (ImGui::Button("New"))
            createAsset();
        ImGui::SameLine();
        if (ImGui::Button("Open..."))
        {
            const HWND owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
            MainThreadDispatcher::instance().post([this, owner] { openAsset(owner); });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_hasAsset);
        if (ImGui::Button("Save"))
        {
            const HWND owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
            MainThreadDispatcher::instance().post([this, owner] { saveAsset(owner); });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (m_dirty)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Unsaved");
        else if (!m_status.empty())
            ImGui::TextDisabled("%s", m_status.c_str());

        if (!m_hasAsset)
        {
            ImGui::Separator();
            ImGui::TextDisabled("Create or open a State Machine asset to edit its graph.");
            ImGui::End();
            return;
        }

        std::array<char, 128> name{};
        std::copy_n(m_draft.name.data(), std::min(m_draft.name.size(), name.size() - 1), name.data());
        if (ImGui::InputText("Asset Name", name.data(), name.size()))
        {
            m_draft.name = name.data();
            markDirty();
        }

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float canvasWidth = available.x * 0.64f;
        drawCanvas(canvasWidth, available.y);
        ImGui::SameLine();
        ImGui::BeginChild("StateMachineInspector", ImVec2(0.0f, available.y), true);
        drawInspector();
        ImGui::EndChild();
        ImGui::End();
    }

    void StateMachineGraphEditor::drawCanvas(const float width, const float height)
    {
        ImGui::BeginChild("StateMachineCanvas", ImVec2(width, height), true,
            ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 pan(m_panX, m_panY);
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        const float cell = 64.0f * m_zoom;
        float gridX = std::fmod(m_panX, cell);
        float gridY = std::fmod(m_panY, cell);
        if (gridX < 0.0f) gridX += cell;
        if (gridY < 0.0f) gridY += cell;
        for (float x = gridX; x < size.x; x += cell)
            drawList->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + size.y),
                IM_COL32(55, 60, 68, 100));
        for (float y = gridY; y < size.y; y += cell)
            drawList->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x + size.x, origin.y + y),
                IM_COL32(55, 60, 68, 100));

        constexpr float nodeWidth = 190.0f;
        constexpr float nodeHeight = 72.0f;
        for (const StateMachineTransition& transition : m_draft.transitions)
        {
            const StateMachineState* const source = findState(transition.sourceState);
            const StateMachineState* const destination = findState(transition.destinationState);
            if (source == nullptr || destination == nullptr)
                continue;
            const ImVec2 from = nodePosition(origin, pan, source->positionX + nodeWidth,
                source->positionY + nodeHeight * 0.5f, m_zoom);
            const ImVec2 to = nodePosition(origin, pan, destination->positionX,
                destination->positionY + nodeHeight * 0.5f, m_zoom);
            const float curve = std::max(45.0f * m_zoom, std::abs(to.x - from.x) * 0.45f);
            const ImVec2 controlA(from.x + curve, from.y);
            const ImVec2 controlB(to.x - curve, to.y);
            ImVec2 previous = from;
            for (int segment = 1; segment <= 16; ++segment)
            {
                const float t = static_cast<float>(segment) / 16.0f;
                const float inverse = 1.0f - t;
                const ImVec2 point(
                    inverse * inverse * inverse * from.x + 3.0f * inverse * inverse * t * controlA.x
                        + 3.0f * inverse * t * t * controlB.x + t * t * t * to.x,
                    inverse * inverse * inverse * from.y + 3.0f * inverse * inverse * t * controlA.y
                        + 3.0f * inverse * t * t * controlB.y + t * t * t * to.y);
                drawList->AddLine(previous, point, IM_COL32(150, 170, 195, 220), 2.0f);
                previous = point;
            }
            const ImVec2 arrow(to.x - 6.0f * m_zoom, to.y);
            drawList->AddTriangleFilled(arrow, ImVec2(to.x - 15.0f * m_zoom, to.y - 5.0f * m_zoom),
                ImVec2(to.x - 15.0f * m_zoom, to.y + 5.0f * m_zoom), IM_COL32(190, 205, 220, 240));
        }

        StateMachineID clickedState = 0;
        for (StateMachineState& state : m_draft.states)
        {
            const ImVec2 position = nodePosition(origin, pan, state.positionX, state.positionY, m_zoom);
            const ImVec2 extent(nodeWidth * m_zoom, nodeHeight * m_zoom);
            const ImVec2 maximum(position.x + extent.x, position.y + extent.y);
            const bool hovered = ImGui::IsMouseHoveringRect(position, maximum);
            const bool selected = state.id == m_selectedState;
            const ImU32 fill = selected ? IM_COL32(61, 93, 138, 255) : IM_COL32(47, 53, 62, 255);
            drawList->AddRectFilled(position, maximum, fill, 7.0f);
            drawList->AddRect(position, maximum, selected ? IM_COL32(125, 185, 255, 255)
                : IM_COL32(105, 115, 128, 255), 7.0f, 0, 1.5f);
            drawList->AddCircleFilled(ImVec2(position.x, position.y + extent.y * 0.5f),
                5.0f * m_zoom, IM_COL32(180, 190, 205, 255));
            drawList->AddCircleFilled(ImVec2(maximum.x, position.y + extent.y * 0.5f),
                5.0f * m_zoom, IM_COL32(180, 190, 205, 255));
            drawList->AddText(ImVec2(position.x + 12.0f * m_zoom, position.y + 10.0f * m_zoom),
                IM_COL32(245, 245, 245, 255), state.name.c_str());
            drawList->AddText(ImVec2(position.x + 12.0f * m_zoom, position.y + 42.0f * m_zoom),
                IM_COL32(185, 195, 208, 255), state.id == m_draft.defaultState ? "Default State" : "State");

            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                clickedState = state.id;
            if (hovered && selected && m_linkSource == 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                state.positionX += delta.x / m_zoom;
                state.positionY += delta.y / m_zoom;
                markDirty();
            }
        }

        const bool hoveredCanvas = ImGui::IsWindowHovered();
        if (hoveredCanvas && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
        {
            m_panX += ImGui::GetIO().MouseDelta.x;
            m_panY += ImGui::GetIO().MouseDelta.y;
        }
        if (hoveredCanvas)
        {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f)
            {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float oldZoom = m_zoom;
                m_zoom = std::clamp(m_zoom * (wheel > 0.0f ? 1.1f : 0.9f), 0.4f, 2.0f);
                const float scale = m_zoom / oldZoom;
                m_panX = mouse.x - origin.x - (mouse.x - origin.x - m_panX) * scale;
                m_panY = mouse.y - origin.y - (mouse.y - origin.y - m_panY) * scale;
            }
        }
        if (clickedState != 0)
        {
            if (m_linkSource != 0)
            {
                if (addTransition(m_linkSource, clickedState))
                    m_selectedState = m_linkSource;
                m_linkSource = 0;
            }
            else
                m_selectedState = clickedState;
        }
        else if (hoveredCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
            && m_linkSource == 0)
            m_selectedState = 0;

        if (hoveredCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("StateMachineCanvasContext");
        if (ImGui::BeginPopup("StateMachineCanvasContext"))
        {
            if (ImGui::MenuItem("Add State"))
            {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                addState((mouse.x - origin.x - m_panX) / m_zoom,
                    (mouse.y - origin.y - m_panY) / m_zoom);
            }
            ImGui::EndPopup();
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x + 8.0f, origin.y + size.y - 22.0f));
        if (m_linkSource != 0)
            ImGui::TextDisabled("Select a destination node to complete the transition.");
        else
            ImGui::TextDisabled("Drag nodes. Middle-drag to pan; wheel to zoom.");
        ImGui::EndChild();
    }

    void StateMachineGraphEditor::drawInspector()
    {
        StateMachineState* selected = findState(m_selectedState);
        if (selected == nullptr)
        {
            ImGui::TextDisabled("Select a state node to edit it.");
        }
        else
        {
            ImGui::Text("State %u", selected->id);
            std::array<char, 128> stateName{};
            std::copy_n(selected->name.data(), std::min(selected->name.size(), stateName.size() - 1),
                stateName.data());
            if (ImGui::InputText("Name", stateName.data(), stateName.size()))
            {
                selected->name = stateName.data();
                markDirty();
            }
            if (selected->id != m_draft.defaultState && ImGui::Button("Set as Default"))
            {
                m_draft.defaultState = selected->id;
                markDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("Link from this state"))
            {
                m_linkSource = selected->id;
                m_status = "Click another node to create a transition.";
            }
            if (m_draft.states.size() > 1 && ImGui::Button("Delete State"))
            {
                const StateMachineID deleted = selected->id;
                std::erase_if(m_draft.transitions, [deleted](const StateMachineTransition& transition)
                    {
                        return transition.sourceState == deleted || transition.destinationState == deleted;
                    });
                std::erase_if(m_draft.states, [deleted](const StateMachineState& state) { return state.id == deleted; });
                invalidateStateIndex();
                if (m_draft.defaultState == deleted)
                    m_draft.defaultState = m_draft.states.front().id;
                m_selectedState = 0;
                if (m_linkSource == deleted)
                    m_linkSource = 0;
                markDirty();
                selected = nullptr;
            }
        }

        if (selected != nullptr)
        {
            ImGui::Separator();
            drawActions(selected->onEnter, "On Enter");
            drawActions(selected->onExit, "On Exit");
            ImGui::Separator();
            ImGui::TextUnformatted("Transitions");
            for (std::size_t index = 0; index < m_draft.transitions.size();)
            {
                StateMachineTransition& transition = m_draft.transitions[index];
                if (transition.sourceState != selected->id)
                {
                    ++index;
                    continue;
                }
                ImGui::PushID(static_cast<int>(transition.id));
                const StateMachineState* const destination = findState(transition.destinationState);
                ImGui::Text("To: %s", destination != nullptr ? destination->name.c_str() : "<missing>");
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    m_draft.transitions.erase(m_draft.transitions.begin() + static_cast<std::ptrdiff_t>(index));
                    ImGui::PopID();
                    markDirty();
                    continue;
                }
                if (ImGui::InputInt("Priority", &transition.priority))
                    markDirty();

                std::array<char, STATE_MACHINE_MAX_EVENT_NAME_LENGTH + 1> eventName{};
                std::copy_n(transition.eventName.data(), std::min(transition.eventName.size(), eventName.size() - 1),
                    eventName.data());
                if (ImGui::InputText("Required Event", eventName.data(), eventName.size()))
                {
                    transition.eventName = eventName.data();
                    markDirty();
                }
                for (std::size_t conditionIndex = 0; conditionIndex < transition.conditions.size(); ++conditionIndex)
                {
                    ImGui::PushID(static_cast<int>(conditionIndex));
                    if (editCondition(transition.conditions[conditionIndex], m_draft.parameters))
                        markDirty();
                    if (ImGui::SmallButton("Remove Condition"))
                    {
                        transition.conditions.erase(transition.conditions.begin()
                            + static_cast<std::ptrdiff_t>(conditionIndex));
                        markDirty();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopID();
                }
                ImGui::BeginDisabled(m_draft.parameters.empty());
                if (ImGui::SmallButton("Add Condition") && !m_draft.parameters.empty())
                {
                    const StateMachineParameter& parameter = m_draft.parameters.front();
                    StateMachineCondition condition;
                    condition.parameterId = parameter.id;
                    condition.mode = parameter.type == StateMachineParameterType::Bool
                        ? StateMachineConditionMode::If
                        : parameter.type == StateMachineParameterType::Trigger
                            ? StateMachineConditionMode::Triggered : StateMachineConditionMode::Greater;
                    transition.conditions.push_back(condition);
                    markDirty();
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                ++index;
            }
        }

        ImGui::Separator();
        drawParameters();
    }

    void StateMachineGraphEditor::drawParameters()
    {
        if (!ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        for (std::size_t index = 0; index < m_draft.parameters.size(); ++index)
        {
            StateMachineParameter& parameter = m_draft.parameters[index];
            ImGui::PushID(static_cast<int>(parameter.id));
            std::array<char, 128> name{};
            std::copy_n(parameter.name.data(), std::min(parameter.name.size(), name.size() - 1), name.data());
            if (ImGui::InputText("Name", name.data(), name.size()))
            {
                parameter.name = name.data();
                markDirty();
            }
            int type = static_cast<int>(parameter.type);
            if (ImGui::Combo("Type", &type, "Float\0Int\0Bool\0Trigger\0"))
            {
                parameter.type = static_cast<StateMachineParameterType>(type);
                for (StateMachineTransition& transition : m_draft.transitions)
                    for (StateMachineCondition& condition : transition.conditions)
                        if (condition.parameterId == parameter.id)
                        {
                            switch (parameter.type)
                            {
                            case StateMachineParameterType::Float:
                            case StateMachineParameterType::Int:
                                if (condition.mode != StateMachineConditionMode::Greater
                                    && condition.mode != StateMachineConditionMode::Less
                                    && condition.mode != StateMachineConditionMode::Equals
                                    && condition.mode != StateMachineConditionMode::NotEqual)
                                    condition.mode = StateMachineConditionMode::Greater;
                                break;
                            case StateMachineParameterType::Bool:
                                if (condition.mode != StateMachineConditionMode::If
                                    && condition.mode != StateMachineConditionMode::IfNot)
                                    condition.mode = StateMachineConditionMode::If;
                                break;
                            case StateMachineParameterType::Trigger:
                                condition.mode = StateMachineConditionMode::Triggered;
                                break;
                            }
                        }
                markDirty();
            }
            switch (parameter.type)
            {
            case StateMachineParameterType::Float:
                if (ImGui::InputFloat("Default", &parameter.defaultFloat))
                    markDirty();
                break;
            case StateMachineParameterType::Int:
                if (ImGui::InputInt("Default", &parameter.defaultInt))
                    markDirty();
                break;
            case StateMachineParameterType::Bool:
            case StateMachineParameterType::Trigger:
                if (ImGui::Checkbox("Default", &parameter.defaultBool))
                    markDirty();
                break;
            }
            ImGui::Separator();
            ImGui::PopID();
        }
        if (ImGui::Button("Add Parameter"))
        {
            const StateMachineParameterID id = nextParameterId(m_draft.parameters);
            if (id == 0)
                m_status = "Parameter ID space is exhausted.";
            else
            {
                m_draft.parameters.push_back({ id, "Parameter " + std::to_string(id) });
                markDirty();
            }
        }
    }

    bool StateMachineGraphEditor::selectAnimationClip(StateMachineAction& action, const HWND owner)
    {
        static constexpr std::array filters = { FileDialogFilter{ L"Animation Clip", L"*.anim" } };
        std::vector<std::filesystem::path> paths;
        const DialogResult result = Dialog::openFile(paths, L"Select Animation Clip",
            L"Assets", filters, false, owner);
        if (result == DialogResult::Cancel)
            return false;
        if (result != DialogResult::Ok || paths.size() != 1)
        {
            LOG_ERROR("[StateMachineEditor] Animation Clip selection failed.");
            m_status = "Animation Clip selection failed. See the engine log.";
            return false;
        }
        AnimationAssetManager& manager = AnimationAssetManager::instance();
        const AnimationClipHandle handle = manager.loadClip(paths.front());
        const auto clip = manager.getClip(handle);
        if (!clip)
        {
            m_status = "Could not load Animation Clip. See the engine log.";
            return false;
        }
        action.type = StateMachineActionType::PlayAnimationClip;
        action.eventName.clear();
        action.animationClipGuid = clip->guid;
        action.animationClipPath = manager.getClipPath(handle);
        action.customType.clear();
        action.payload.clear();
        return true;
    }

    void StateMachineGraphEditor::drawActions(std::vector<StateMachineAction>& actions,
        const std::string_view label)
    {
        const HWND owner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        const auto queueClipSelection = [this, owner](StateMachineAction& action)
        {
            MainThreadDispatcher::instance().post([this, &action, owner]
            {
                if (selectAnimationClip(action, owner))
                    markDirty();
            });
        };
        ImGui::PushID(label.data(), label.data() + label.size());
        ImGui::Text("%.*s", static_cast<int>(label.size()), label.data());
        for (std::size_t index = 0; index < actions.size();)
        {
            StateMachineAction& action = actions[index];
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::BeginCombo("Action", action.type == StateMachineActionType::Custom
                ? action.customType.c_str() : actionTypeName(action.type)))
            {
                if (ImGui::Selectable("Emit Event", action.type == StateMachineActionType::EmitEvent))
                {
                    action = {};
                    action.type = StateMachineActionType::EmitEvent;
                    action.eventName = "Event";
                    markDirty();
                }
                if (ImGui::Selectable("Play Animation Clip", action.type == StateMachineActionType::PlayAnimationClip))
                    queueClipSelection(action);
                const std::vector<std::string> registeredTypes =
                    StateMachineActionRegistry::instance().getRegisteredTypes();
                for (const std::string& type : registeredTypes)
                    if (ImGui::Selectable(type.c_str(),
                        action.type == StateMachineActionType::Custom && action.customType == type))
                    {
                        action = {};
                        action.type = StateMachineActionType::Custom;
                        action.customType = type;
                        markDirty();
                    }
                ImGui::EndCombo();
            }

            if (action.type == StateMachineActionType::EmitEvent)
            {
                std::array<char, STATE_MACHINE_MAX_EVENT_NAME_LENGTH + 1> eventName{};
                std::copy_n(action.eventName.data(), std::min(action.eventName.size(), eventName.size() - 1),
                    eventName.data());
                if (ImGui::InputText("Event Name", eventName.data(), eventName.size()))
                {
                    action.eventName = eventName.data();
                    markDirty();
                }
            }
            else if (action.type == StateMachineActionType::PlayAnimationClip)
            {
                const auto clipPath = String::fromWide(action.animationClipPath.filename().c_str());
                ImGui::TextWrapped("%s", clipPath.size() > 0 ? clipPath.c_str() : "<no clip>");
                if (ImGui::Button("Choose Clip..."))
                    queueClipSelection(action);
            }
            else if (action.type == StateMachineActionType::Custom)
            {
                bool changed = false;
                if (StateMachineActionRegistry::instance().drawEditor(action, changed))
                {
                    if (changed)
                        markDirty();
                }
                else
                    ImGui::TextDisabled("No editor is registered for this action type.");
            }

            if (ImGui::SmallButton("Remove Action"))
            {
                actions.erase(actions.begin() + static_cast<std::ptrdiff_t>(index));
                ImGui::PopID();
                markDirty();
                continue;
            }
            ImGui::Separator();
            ImGui::PopID();
            ++index;
        }

        if (ImGui::Button("Add Event Action"))
        {
            StateMachineAction action;
            action.type = StateMachineActionType::EmitEvent;
            action.eventName = "Event";
            actions.push_back(std::move(action));
            markDirty();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Animation Action"))
        {
            MainThreadDispatcher::instance().post([this, &actions, owner]
            {
                StateMachineAction action;
                if (selectAnimationClip(action, owner))
                {
                    actions.push_back(std::move(action));
                    markDirty();
                }
            });
        }
        const std::vector<std::string> customTypes = StateMachineActionRegistry::instance().getRegisteredTypes();
        if (!customTypes.empty() && ImGui::BeginCombo("Add Custom Action", "Select type"))
        {
            for (const std::string& type : customTypes)
                if (ImGui::Selectable(type.c_str()))
                {
                    StateMachineAction action;
                    action.type = StateMachineActionType::Custom;
                    action.customType = type;
                    actions.push_back(std::move(action));
                    markDirty();
                }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
}
