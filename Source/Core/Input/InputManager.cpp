#include "Pch.h"
#include "InputManager.h"
#pragma comment(lib, "Xinput.lib")

namespace Engine
{
    void InputManager::update()
    {
        if (!m_windowFocused)
            return;

        m_prevMouseWheel = m_mouseWheel;
        m_mouseWheel = 0;

        updateKeyboard();
        updateMouse();
        updateGamepads();
        updateVibration(TimeManager::instance().getDeltaTime());
        updateBuffer();
    }

    void InputManager::setWindowFocused(bool focused)
    {
        if (m_windowFocused == focused)
            return;

        m_windowFocused = focused;

        if (!focused)
        {
            ZeroMemory(m_currKeys, 256);
            ZeroMemory(m_prevKeys, 256);
            ZeroMemory(m_currMouse, sizeof(m_currMouse));
            ZeroMemory(m_prevMouse, sizeof(m_prevMouse));
            m_mouseWheel = 0;
            m_prevMouseWheel = 0;
            m_mouseDelta = {};
            m_actionBuffers.clear();
            for (GamepadState& state : m_gamepads)
            {
                state.prevButtons = 0;
                state.currButtons = 0;
                state.leftStickX = 0.0f;
                state.leftStickY = 0.0f;
                state.rightStickX = 0.0f;
                state.rightStickY = 0.0f;
                state.leftTrigger = 0.0f;
                state.rightTrigger = 0.0f;
            }
            stopAllGamepadVibration();
            return;
        }

        if (GetCursorPos(&m_mousePos))
            m_prevMousePos = m_mousePos;
        m_mouseDelta = {};
    }

    void InputManager::addMouseWheel(int delta)
    {
        if (m_windowFocused)
        {
            const std::int64_t accumulated = static_cast<std::int64_t>(m_mouseWheel) + delta;
            m_mouseWheel = static_cast<int>(std::clamp(
                accumulated,
                static_cast<std::int64_t>(std::numeric_limits<int>::min()),
                static_cast<std::int64_t>(std::numeric_limits<int>::max())));
        }
    }

    void InputManager::updateKeyboard()
    {
        memcpy(m_prevKeys, m_currKeys, 256);
        if (!GetKeyboardState(m_currKeys))
        {
            ZeroMemory(m_currKeys, sizeof(m_currKeys));
            LOG_ERROR("Failed to retrieve keyboard state.");
        }
    }

    bool InputManager::isKeyPressed(uint8_t key) const
    {
        return !(m_prevKeys[key] & 0x80) && (m_currKeys[key] & 0x80);
    }

    bool InputManager::isKeyHeld(uint8_t key) const
    {
        return (m_currKeys[key] & 0x80) != 0;
    }

    bool InputManager::isKeyReleased(uint8_t key) const
    {
        return (m_prevKeys[key] & 0x80) && !(m_currKeys[key] & 0x80);
    }

    void InputManager::updateMouse()
    {
        memcpy(m_prevMouse, m_currMouse, sizeof(m_currMouse));

        m_currMouse[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        m_currMouse[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        m_currMouse[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

        m_prevMousePos = m_mousePos;
        if (!GetCursorPos(&m_mousePos))
        {
            m_mouseDelta = {};
            LOG_ERROR("Failed to retrieve mouse cursor position.");
            return;
        }

        m_mouseDelta.x = m_mousePos.x - m_prevMousePos.x;
        m_mouseDelta.y = m_mousePos.y - m_prevMousePos.y;
    }

    bool InputManager::isMousePressed(uint8_t button) const
    {
        return button < std::size(m_currMouse) && !m_prevMouse[button] && m_currMouse[button];
    }

    bool InputManager::isMouseHeld(uint8_t button) const
    {
        return button < std::size(m_currMouse) && m_currMouse[button] != 0;
    }

    bool InputManager::isMouseReleased(uint8_t button) const
    {
        return button < std::size(m_currMouse) && m_prevMouse[button] && !m_currMouse[button];
    }

    void InputManager::updateGamepads()
    {
        // 未接続コントローラーは N フレームに 1 回だけポーリングする（パフォーマンス最適化）
        const bool pollDisconnected = (m_disconnectedPollCounter == 0);
        m_disconnectedPollCounter = (m_disconnectedPollCounter + 1) % DISCONNECTED_POLL_INTERVAL;

        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i)
        {
            auto& state = m_gamepads[i];

            if (!state.connected && !pollDisconnected)
                continue;

            XINPUT_STATE xstate{};
            const DWORD result = XInputGetState(i, &xstate);

            if (result != ERROR_SUCCESS)
            {
                if (state.connected)
                {
                    state = GamepadState{};
                    LOG_INFO("Gamepad {}: disconnected", i);
                }
                continue;
            }

            if (!state.connected)
                LOG_INFO("Gamepad {}: connected", i);

            state.connected = true;

            // ボタンビットフィールド 更新
            state.prevButtons = state.currButtons;
            state.currButtons = buildButtonMask(xstate.Gamepad);

            // スティック（放射状デッドゾーン）
            applyStickDeadzone(
                static_cast<float>(xstate.Gamepad.sThumbLX),
                static_cast<float>(xstate.Gamepad.sThumbLY),
                LEFT_STICK_DEADZONE,
                state.leftStickX, state.leftStickY);

            applyStickDeadzone(
                static_cast<float>(xstate.Gamepad.sThumbRX),
                static_cast<float>(xstate.Gamepad.sThumbRY),
                RIGHT_STICK_DEADZONE,
                state.rightStickX, state.rightStickY);

            // トリガー
            state.leftTrigger = applyTriggerDeadzone(xstate.Gamepad.bLeftTrigger);
            state.rightTrigger = applyTriggerDeadzone(xstate.Gamepad.bRightTrigger);
        }
    }

    uint32_t InputManager::buildButtonMask(const XINPUT_GAMEPAD& gp)
    {
        // XInput の 16-bit ボタンフィールドを下位ビットに格納
        uint32_t mask = static_cast<uint32_t>(gp.wButtons);

        // トリガーのデジタル判定を拡張ビットに追加
        if (gp.bLeftTrigger > TRIGGER_THRESHOLD)
            mask |= static_cast<uint32_t>(GamepadButton::LeftTrigger);
        if (gp.bRightTrigger > TRIGGER_THRESHOLD)
            mask |= static_cast<uint32_t>(GamepadButton::RightTrigger);

        return mask;
    }

    void InputManager::applyStickDeadzone(float rawX, float rawY, float deadzone, float& outX, float& outY)
    {
        const float magnitude = std::sqrtf(rawX * rawX + rawY * rawY);

        if (magnitude < deadzone)
        {
            outX = outY = 0.f;
            return;
        }

        // デッドゾーン除去後に [0, 1] へ正規化
        const float normalizedMag = std::clamp(
            (magnitude - deadzone) / (32767.f - deadzone), 0.f, 1.f);

        const float scale = normalizedMag / magnitude;
        outX = rawX * scale;
        outY = rawY * scale;
    }

    float InputManager::applyTriggerDeadzone(uint8_t raw)
    {
        if (raw <= TRIGGER_THRESHOLD)
            return 0.f;

        return static_cast<float>(raw - TRIGGER_THRESHOLD)
            / static_cast<float>(255 - TRIGGER_THRESHOLD);
    }

    bool InputManager::isValidIndex(int index)
    {
        return index >= 0 && index < static_cast<int>(XUSER_MAX_COUNT);
    }

    namespace
    {
        bool isValidGamepadButton(const GamepadButton button) noexcept
        {
            switch (button)
            {
            case GamepadButton::DPadUp:
            case GamepadButton::DPadDown:
            case GamepadButton::DPadLeft:
            case GamepadButton::DPadRight:
            case GamepadButton::Start:
            case GamepadButton::Back:
            case GamepadButton::LeftThumb:
            case GamepadButton::RightThumb:
            case GamepadButton::LeftShoulder:
            case GamepadButton::RightShoulder:
            case GamepadButton::A:
            case GamepadButton::B:
            case GamepadButton::X:
            case GamepadButton::Y:
            case GamepadButton::LeftTrigger:
            case GamepadButton::RightTrigger:
                return true;
            default:
                return false;
            }
        }

        bool isValidGamepadAxis(const GamepadAxis axis) noexcept
        {
            switch (axis)
            {
            case GamepadAxis::LeftStickX:
            case GamepadAxis::LeftStickY:
            case GamepadAxis::RightStickX:
            case GamepadAxis::RightStickY:
            case GamepadAxis::LeftTrigger:
            case GamepadAxis::RightTrigger:
                return true;
            default:
                return false;
            }
        }
    }

    bool InputManager::checkGamepadButtonPressed(GamepadButton btn, int index) const
    {
        const uint32_t b = static_cast<uint32_t>(btn);
        return m_gamepads[index].connected
            && !(m_gamepads[index].prevButtons & b)
            && (m_gamepads[index].currButtons & b);
    }

    bool InputManager::checkGamepadButtonHeld(GamepadButton btn, int index) const
    {
        return m_gamepads[index].connected
            && (m_gamepads[index].currButtons & static_cast<uint32_t>(btn)) != 0;
    }

    bool InputManager::checkGamepadButtonReleased(GamepadButton btn, int index) const
    {
        const uint32_t b = static_cast<uint32_t>(btn);
        return m_gamepads[index].connected
            && (m_gamepads[index].prevButtons & b)
            && !(m_gamepads[index].currButtons & b);
    }

    bool InputManager::isGamepadConnected(int index) const
    {
        return isValidIndex(index) && m_gamepads[index].connected;
    }

    int InputManager::getConnectedGamepadCount() const
    {
        int count = 0;
        for (int i = 0; i < static_cast<int>(XUSER_MAX_COUNT); ++i)
            if (m_gamepads[i].connected) ++count;
        return count;
    }

    bool InputManager::isGamepadButtonPressed(GamepadButton btn, int index) const
    {
        if (index == -1)
        {
            for (int i = 0; i < static_cast<int>(XUSER_MAX_COUNT); ++i)
                if (checkGamepadButtonPressed(btn, i)) return true;
            return false;
        }
        return isValidIndex(index) && checkGamepadButtonPressed(btn, index);
    }

    bool InputManager::isGamepadButtonHeld(GamepadButton btn, int index) const
    {
        if (index == -1)
        {
            for (int i = 0; i < static_cast<int>(XUSER_MAX_COUNT); ++i)
                if (checkGamepadButtonHeld(btn, i)) return true;
            return false;
        }
        return isValidIndex(index) && checkGamepadButtonHeld(btn, index);
    }

    bool InputManager::isGamepadButtonReleased(GamepadButton btn, int index) const
    {
        if (index == -1)
        {
            for (int i = 0; i < static_cast<int>(XUSER_MAX_COUNT); ++i)
                if (checkGamepadButtonReleased(btn, i)) return true;
            return false;
        }
        return isValidIndex(index) && checkGamepadButtonReleased(btn, index);
    }

    float InputManager::getGamepadAxisInternal(GamepadAxis axis, int index) const
    {
        if (!isValidIndex(index) || !m_gamepads[index].connected)
            return 0.f;

        const auto& s = m_gamepads[index];
        switch (axis)
        {
        case GamepadAxis::LeftStickX:   return s.leftStickX;
        case GamepadAxis::LeftStickY:   return s.leftStickY;
        case GamepadAxis::RightStickX:  return s.rightStickX;
        case GamepadAxis::RightStickY:  return s.rightStickY;
        case GamepadAxis::LeftTrigger:  return s.leftTrigger;
        case GamepadAxis::RightTrigger: return s.rightTrigger;
        default:                        return 0.f;
        }
    }

    float InputManager::getGamepadAxis(GamepadAxis axis, int index) const
    {
        if (index == -1)
        {
            // 全コントローラーの中で絶対値最大の値を返す
            float best = 0.f;
            for (int i = 0; i < static_cast<int>(XUSER_MAX_COUNT); ++i)
            {
                const float v = getGamepadAxisInternal(axis, i);
                if (std::fabsf(v) > std::fabsf(best))
                    best = v;
            }
            return best;
        }
        return getGamepadAxisInternal(axis, index);
    }

    void InputManager::setGamepadVibration(float leftMotor, float rightMotor, float duration, int index)
    {
        if (!isValidIndex(index))
        {
            LOG_ERROR("Cannot set gamepad vibration: invalid controller index {}.", index);
            return;
        }

        if (!std::isfinite(leftMotor) || !std::isfinite(rightMotor)
            || !std::isfinite(duration) || (duration < 0.0f && duration != -1.0f))
        {
            LOG_ERROR("Cannot set gamepad vibration: invalid motor strength or duration.");
            return;
        }

        if (duration == 0.0f)
        {
            stopGamepadVibration(index);
            return;
        }

        auto& s = m_gamepads[index];
        s.leftMotor = std::clamp(leftMotor, 0.f, 1.f);
        s.rightMotor = std::clamp(rightMotor, 0.f, 1.f);
        s.vibrationDuration = duration;

        applyVibration(index);
    }

    void InputManager::stopGamepadVibration(int index)
    {
        if (!isValidIndex(index)) return;

        auto& s = m_gamepads[index];
        s.leftMotor = 0.f;
        s.rightMotor = 0.f;
        s.vibrationDuration = 0.f;

        XINPUT_VIBRATION vib{};
        XInputSetState(static_cast<DWORD>(index), &vib);
    }

    void InputManager::stopAllGamepadVibration()
    {
        for (int i = 0; i < static_cast<int>(XUSER_MAX_COUNT); ++i)
            stopGamepadVibration(i);
    }

    void InputManager::applyVibration(int index)
    {
        const auto& s = m_gamepads[index];

        XINPUT_VIBRATION vib{};
        vib.wLeftMotorSpeed = static_cast<WORD>(s.leftMotor * 65535.f);
        vib.wRightMotorSpeed = static_cast<WORD>(s.rightMotor * 65535.f);
        XInputSetState(static_cast<DWORD>(index), &vib);
    }

    void InputManager::updateVibration(float dt)
    {
        for (int i = 0; i < static_cast<int>(XUSER_MAX_COUNT); ++i)
        {
            auto& s = m_gamepads[i];
            if (!s.connected || s.vibrationDuration < 0.f)
                continue;   // 未接続、または無限継続（-1.0f）はスキップ

            s.vibrationDuration -= dt;
            if (s.vibrationDuration <= 0.f)
                stopGamepadVibration(i);
        }
    }

    void InputManager::bindAction(const std::string& actionName, uint8_t key, float bufferTime)
    {
        if (actionName.empty() || !std::isfinite(bufferTime))
        {
            LOG_WARNING("Cannot bind keyboard action: action name or buffer time is invalid.");
            return;
        }

        auto& action = m_actionBindings[actionName];
        action.keys.push_back(key);
        if (bufferTime >= 0.f)
            action.bufferTime = bufferTime;
    }

    void InputManager::bindAction(const std::string& actionName, GamepadButton button, float bufferTime, int controllerIndex)
    {
        if (actionName.empty() || !std::isfinite(bufferTime) || !isValidGamepadButton(button)
            || (controllerIndex != -1 && !isValidIndex(controllerIndex)))
        {
            LOG_WARNING("Cannot bind gamepad action: action name, button, controller index, or buffer time is invalid.");
            return;
        }

        auto& action = m_actionBindings[actionName];
        action.gamepadButtons.push_back({ button, controllerIndex });
        if (bufferTime >= 0.f)
            action.bufferTime = bufferTime;
    }

    InputState InputManager::getActionState(const std::string& actionName) const
    {
        auto it = m_actionBindings.find(actionName);
        if (it == m_actionBindings.end())
            return InputState::None;

        const auto& action = it->second;
        bool held = false;
        bool released = false;

        for (uint8_t key : action.keys)
        {
            if (isKeyPressed(key))
                return InputState::Pressed;
            held = held || isKeyHeld(key);
            released = released || isKeyReleased(key);
        }

        for (const auto& bind : action.gamepadButtons)
        {
            if (isGamepadButtonPressed(bind.button, bind.controllerIndex))
                return InputState::Pressed;
            held = held || isGamepadButtonHeld(bind.button, bind.controllerIndex);
            released = released || isGamepadButtonReleased(bind.button, bind.controllerIndex);
        }

        if (held)
            return InputState::Held;
        if (released)
            return InputState::Released;
        return InputState::None;
    }

    void InputManager::bindAxis(const std::string& name, uint8_t negative, uint8_t positive)
    {
        if (name.empty())
        {
            LOG_WARNING("Cannot bind keyboard axis: axis name is empty.");
            return;
        }

        auto& axis = m_axes[name];
        axis.negativeKey = negative;
        axis.positiveKey = positive;
    }

    void InputManager::bindAxis(const std::string& name, GamepadAxis axis, int controllerIndex, bool invert)
    {
        if (name.empty() || !isValidGamepadAxis(axis)
            || (controllerIndex != -1 && !isValidIndex(controllerIndex)))
        {
            LOG_WARNING("Cannot bind gamepad axis: axis name, axis, or controller index is invalid.");
            return;
        }

        auto& a = m_axes[name];
        a.hasGamepadAxis = true;
        a.gpAxis = axis;
        a.gpIndex = controllerIndex;
        a.gpInvert = invert;
    }

    float InputManager::getAxis(const std::string& name) const
    {
        auto it = m_axes.find(name);
        if (it == m_axes.end())
            return 0.f;

        const auto& axis = it->second;
        float val = 0.f;

        // キーボード入力
        if (isKeyHeld(axis.negativeKey)) val -= 1.f;
        if (isKeyHeld(axis.positiveKey)) val += 1.f;

        // ゲームパッド入力（絶対値が大きい方を採用）
        if (axis.hasGamepadAxis)
        {
            float gpVal = getGamepadAxis(axis.gpAxis, axis.gpIndex);
            if (axis.gpInvert) gpVal = -gpVal;
            if (std::fabsf(gpVal) > std::fabsf(val))
                val = gpVal;
        }

        return val;
    }

    void InputManager::updateBuffer()
    {
        const float dt = TimeManager::instance().getDeltaTime();

        for (auto it = m_actionBuffers.begin(); it != m_actionBuffers.end(); )
        {
            it->second.timeLeft -= dt;
            if (it->second.timeLeft <= 0.f)
                it = m_actionBuffers.erase(it);
            else
                ++it;
        }

        for (auto& [name, action] : m_actionBindings)
        {
            bool justPressed = false;

            // キーボード判定
            for (uint8_t key : action.keys)
            {
                if (isKeyPressed(key)) { justPressed = true; break; }
            }

            // ゲームパッドボタン判定
            if (!justPressed)
            {
                for (const auto& bind : action.gamepadButtons)
                {
                    if (isGamepadButtonPressed(bind.button, bind.controllerIndex))
                    {
                        justPressed = true;
                        break;
                    }
                }
            }

            if (justPressed)
            {
                auto& buf = m_actionBuffers[name];
                buf.timeLeft = action.bufferTime;
            }
        }
    }

    bool InputManager::consumeAction(const std::string& actionName)
    {
        auto it = m_actionBuffers.find(actionName);
        if (it == m_actionBuffers.end())
            return false;

        m_actionBuffers.erase(it);
        return true;
    }
}