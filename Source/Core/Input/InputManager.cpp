#include "Pch.h"
#include "InputManager.h"
#pragma comment(lib, "Xinput.lib")

namespace Engine
{
    InputManager::~InputManager()
    {
        stopAllGamepadVibration();
    }

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
        m_windowFocused = focused;

        if (!focused)
        {
            ZeroMemory(m_currKeys, 256);
            ZeroMemory(m_prevKeys, 256);
            ZeroMemory(m_currMouse, sizeof(m_currMouse));
            ZeroMemory(m_prevMouse, sizeof(m_prevMouse));
            m_mouseWheel = 0;
            m_prevMouseWheel = 0;
            stopAllGamepadVibration();
        }
    }

    void InputManager::addMouseWheel(int delta)
    {
        m_mouseWheel += delta;
    }

    void InputManager::updateKeyboard()
    {
        memcpy(m_prevKeys, m_currKeys, 256);
        GetKeyboardState(m_currKeys);
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
        GetCursorPos(&m_mousePos);

        m_mouseDelta.x = m_mousePos.x - m_prevMousePos.x;
        m_mouseDelta.y = m_mousePos.y - m_prevMousePos.y;
    }

    bool InputManager::isMousePressed(uint8_t button) const
    {
        return !m_prevMouse[button] && m_currMouse[button];
    }

    bool InputManager::isMouseHeld(uint8_t button) const
    {
        return m_currMouse[button] != 0;
    }

    bool InputManager::isMouseReleased(uint8_t button) const
    {
        return m_prevMouse[button] && !m_currMouse[button];
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
        if (!isValidIndex(index)) return;

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
        auto& action = m_actionBindings[actionName];
        action.keys.push_back(key);
        if (bufferTime >= 0.f)
            action.bufferTime = bufferTime;
    }

    void InputManager::bindAction(const std::string& actionName, GamepadButton button, float bufferTime, int controllerIndex)
    {
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

        // キーボードチェック
        for (uint8_t key : action.keys)
        {
            if (isKeyPressed(key))  return InputState::Pressed;
            if (isKeyHeld(key))     return InputState::Held;
            if (isKeyReleased(key)) return InputState::Released;
        }

        // ゲームパッドボタンチェック
        for (const auto& bind : action.gamepadButtons)
        {
            if (isGamepadButtonPressed(bind.button, bind.controllerIndex)) return InputState::Pressed;
            if (isGamepadButtonHeld(bind.button, bind.controllerIndex)) return InputState::Held;
            if (isGamepadButtonReleased(bind.button, bind.controllerIndex)) return InputState::Released;
        }

        return InputState::None;
    }

    void InputManager::bindAxis(const std::string& name, uint8_t negative, uint8_t positive)
    {
        auto& axis = m_axes[name];
        axis.negativeKey = negative;
        axis.positiveKey = positive;
    }

    void InputManager::bindAxis(const std::string& name, GamepadAxis axis, int controllerIndex, bool invert)
    {
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
                buf.triggered = true;
            }
        }

        // バッファ寿命管理
        for (auto it = m_actionBuffers.begin(); it != m_actionBuffers.end(); )
        {
            it->second.timeLeft -= dt;
            if (it->second.timeLeft <= 0.f)
                it = m_actionBuffers.erase(it);
            else
                ++it;
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