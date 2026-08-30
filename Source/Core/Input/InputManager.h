#pragma once

#include <Xinput.h>

namespace Engine
{
    /*
    * @brief 入力状態を表す列挙型
    */
    enum class InputState
    {
        None,
        Pressed,   //!< 今フレームで押された
        Held,      //!< 押し続けている
        Released,  //!< 今フレームで離された
        Max
    };

    /*
    * @brief 入力バッファの状態を表す列挙型
    */
    enum class BufferedState
    {
        None,
        Buffered,  //!< バッファリングされている
        Consumed   //!< バッファリングが消費された
    };

    /*
    * @brief ゲームパッドのボタンを表す列挙型
    */
    enum class GamepadButton : uint32_t
    {
        DPadUp = 0x00000001,        //!< 十字キー 上
        DPadDown = 0x00000002,      //!< 十字キー 下
        DPadLeft = 0x00000004,      //!< 十字キー 左
        DPadRight = 0x00000008,     //!< 十字キー 右
        Start = 0x00000010,         //!< スタートボタン
        Back = 0x00000020,          //!< バックボタン (Select)
        LeftThumb = 0x00000040,     //!< 左スティック押し込み
        RightThumb = 0x00000080,    //!< 右スティック押し込み
        LeftShoulder = 0x00000100,  //!< LB ボタン
        RightShoulder = 0x00000200, //!< RB ボタン
        A = 0x00001000,             //!< A ボタン
        B = 0x00002000,             //!< B ボタン
        X = 0x00004000,             //!< X ボタン
        Y = 0x00008000,             //!< Y ボタン
        LeftTrigger = 0x00010000,   //!< LT デジタル押し込み（閾値超過）
        RightTrigger = 0x00020000,  //!< RT デジタル押し込み（閾値超過）
    };

    /*
    * @brief ゲームパッドの軸を表す列挙型
    */
    enum class GamepadAxis : uint8_t
    {
        LeftStickX,    //!< 左スティック 水平軸  [-1, 1]
        LeftStickY,    //!< 左スティック 垂直軸  [-1, 1]  (+が上方向)
        RightStickX,   //!< 右スティック 水平軸  [-1, 1]
        RightStickY,   //!< 右スティック 垂直軸  [-1, 1]  (+が上方向)
        LeftTrigger,   //!< 左トリガー アナログ値 [0, 1]
        RightTrigger,  //!< 右トリガー アナログ値 [0, 1]
    };

    /*
    * @brief 入力マネージャークラス
    */
    class InputManager
    {
    public:

        /*
          * @brief シングルトンインスタンス取得
          */
        static InputManager& instance()
        {
            static InputManager instance;
            return instance;
        }

        /*
          * @brief デストラクタ
          */
        ~InputManager();

        /*
          * @brief 入力状態を更新
          */
        void update();

        /*
          * @brief ウィンドウのフォーカス状態を設定
          * @param focused フォーカスされているかどうか
          */
        void setWindowFocused(bool focused);

        /*
          * @brief マウスホイールの回転量を加算
          * @param delta 回転量（正の値で上方向、負の値で下方向）
          */
        void addMouseWheel(int delta);

        /*
          * @brief キーが押されたかどうかを取得
          * @param key 仮想キーコード
          * @return 押された場合は true、そうでない場合は false
          */
        bool isKeyPressed(uint8_t key) const;

        /*
          * @brief キーが押され続けているかどうかを取得
          * @param key 仮想キーコード
          * @return 押され続けている場合は true、そうでない場合は false
          */
        bool isKeyHeld(uint8_t key) const;

        /*
          * @brief キーが離されたかどうかを取得
          * @param key 仮想キーコード
          * @return 離された場合は true、そうでない場合は false
          */
        bool isKeyReleased(uint8_t key) const;

        /*
          * @brief マウスボタンが押されたかどうかを取得
          * @param button マウスボタン番号（0: 左, 1: 右, 2: 中）
          * @return 押された場合は true、そうでない場合は false
          */
        bool isMousePressed(uint8_t button) const;

        /*
          * @brief マウスボタンが押され続けているかどうかを取得
          * @param button マウスボタン番号（0: 左, 1: 右, 2: 中）
          * @return 押され続けている場合は true、そうでない場合は false
          */
        bool isMouseHeld(uint8_t button) const;

        /*
          * @brief マウスボタンが離されたかどうかを取得
          * @param button マウスボタン番号（0: 左, 1: 右, 2: 中）
          * @return 離された場合は true、そうでない場合は false
          */
        bool isMouseReleased(uint8_t button) const;

        /*
          * @brief マウスの現在位置を取得
          * @return マウスの現在位置（スクリーン座標）
          */
        POINT getMousePosition() const { return m_mousePos; }

        /*
          * @brief マウスの移動量を取得
          * @return マウスの移動量（スクリーン座標）
          */
        POINT getMouseDelta() const { return m_mouseDelta; }

        /*
          * @brief マウスホイールの回転量を取得
          * @return マウスホイールの回転量
          */
        int getMouseWheel() const { return m_prevMouseWheel; }

        /*
          * @brief ゲームパッドが接続されているかどうかを取得
          * @param index コントローラーのインデックス（0～3）
          * @return 接続されている場合は true、そうでない場合は false
          */
        bool isGamepadConnected(int index = 0) const;

        /*
          * @brief 接続されているゲームパッドの数を取得
          * @return 接続されているゲームパッドの数
          */
        int getConnectedGamepadCount() const;

        /*
          * @brief ゲームパッドのボタンが押されたかどうかを取得
          * @param btn ゲームパッドのボタン
          * @param index コントローラーのインデックス（0～3、-1で全コントローラー）
          * @return 押された場合は true、そうでない場合は false
          */
        bool isGamepadButtonPressed(GamepadButton btn, int index = 0) const;

        /*
          * @brief ゲームパッドのボタンが押され続けているかどうかを取得
          * @param btn ゲームパッドのボタン
          * @param index コントローラーのインデックス（0～3、-1で全コントローラー）
          * @return 押され続けている場合は true、そうでない場合は false
          */
        bool isGamepadButtonHeld(GamepadButton btn, int index = 0) const;

        /*
          * @brief ゲームパッドのボタンが離されたかどうかを取得
          * @param btn ゲームパッドのボタン
          * @param index コントローラーのインデックス（0～3、-1で全コントローラー）
          * @return 離された場合は true、そうでない場合は false
          */
        bool isGamepadButtonReleased(GamepadButton btn, int index = 0) const;

        /*
          * @brief ゲームパッドの軸の値を取得
          * @param axis ゲームパッドの軸
          * @param index コントローラーのインデックス（0～3）
          * @return 軸の値（-1.0f～1.0f）
          */
        float getGamepadAxis(GamepadAxis axis, int index = 0) const;

        /*
          * @brief ゲームパッドのバイブレーションを設定
          * @param leftMotor 左モーターの強さ（0.0f～1.0f）
          * @param rightMotor 右モーターの強さ（0.0f～1.0f）
          * @param duration 持続時間（秒、-1.0fで無限）
          * @param index コントローラーのインデックス（0～3）
          */
        void setGamepadVibration(float leftMotor, float rightMotor, float duration = -1.0f, int index = 0);

        /*
          * @brief ゲームパッドのバイブレーションを停止
          * @param index コントローラーのインデックス（0～3）
          */
        void stopGamepadVibration(int index = 0);

        /*
          * @brief 全てのゲームパッドのバイブレーションを停止
          */
        void stopAllGamepadVibration();

        /*
          * @brief アクションにキーをバインド
          * @param actionName アクション名
          * @param key 仮想キーコード
          * @param bufferTime バッファリング時間（秒）
          */
        void bindAction(const std::string& actionName, uint8_t key, float bufferTime = 0.15f);

        /*
          * @brief アクションにゲームパッドボタンをバインド
          * @param actionName アクション名
          * @param button ゲームパッドのボタン
          * @param bufferTime バッファリング時間（秒）
          * @param controllerIndex コントローラーのインデックス（0～3、-1で全コントローラー）
          */
        void bindAction(const std::string& actionName, GamepadButton button, float bufferTime = 0.15f, int controllerIndex = -1);

        /*
          * @brief アクションの状態を取得
          * @param actionName アクション名
          * @return 入力状態（Pressed, Held, Released, None）
          */
        InputState getActionState(const std::string& actionName) const;

        /*
          * @brief 軸にキーをバインド
          * @param name 軸名
          * @param negative 負方向の仮想キーコード
          * @param positive 正方向の仮想キーコード
          */
        void bindAxis(const std::string& name, uint8_t negative, uint8_t positive);

        /*
          * @brief 軸にゲームパッドの軸をバインド
          * @param name 軸名
          * @param axis ゲームパッドの軸
          * @param controllerIndex コントローラーのインデックス（0～3、-1で全コントローラー）
          * @param invert 反転するかどうか
          */
        void bindAxis(const std::string& name, GamepadAxis axis, int controllerIndex = -1, bool invert = false);

        /*
          * @brief 軸の値を取得
          * @param name 軸名
          * @return 軸の値（-1.0f～1.0f）
          */
        float getAxis(const std::string& name) const;

        /*
          * @brief アクションのバッファリング状態を消費
          * @param actionName アクション名
          * @return 消費できた場合は true、そうでない場合は false
          */
        bool consumeAction(const std::string& actionName);

    private:

        InputManager() = default;

        /*
          * @brief ゲームパッドボタンのバインド情報
          */
        struct GamepadButtonBind
        {
            GamepadButton button;                 //!< ゲームパッドのボタン
            int           controllerIndex = -1;   //!< -1 = 全コントローラー
        };

        /*
          * @brief アクションのバインド情報
          */
        struct ActionBinding
        {
            std::vector<uint8_t>           keys;               //!< 仮想キーコード
            std::vector<GamepadButtonBind> gamepadButtons;     //!< ゲームパッドボタンのバインド情報
            float                          bufferTime = 0.15f; //!< バッファリング時間（秒）
        };

        /*
          * @brief 入力バッファのエントリ
          */
        struct InputBufferEntry
        {
            float timeLeft = 0.0f;   //!< バッファリング残り時間（秒）
            bool  triggered = false; //!< バッファリングがトリガーされたかどうか
        };

        /*
          * @brief 軸のバインド情報
          */
        struct InputAxis
        {
            uint8_t     negativeKey = 0;                  //!< 負方向の仮想キーコード
            uint8_t     positiveKey = 0;                  //!< 正方向の仮想キーコード
            bool        hasGamepadAxis = false;           //!< ゲームパッドの軸がバインドされているかどうか
            GamepadAxis gpAxis = GamepadAxis::LeftStickX; //!< ゲームパッドの軸
            int         gpIndex = -1;                     //!< -1 = 全コントローラー
            bool        gpInvert = false;                 //!< ゲームパッドの軸を反転するかどうか
        };

        /*
          * @brief ゲームパッドの状態
          */
        struct GamepadState
        {
            bool     connected = false;       //!< 接続されているかどうか
            uint32_t prevButtons = 0;         //!< 前フレームのボタンビットフィールド
            uint32_t currButtons = 0;         //!< 今フレームのボタンビットフィールド
            float    leftStickX = 0.f;        //!< 左スティックのX軸
            float    leftStickY = 0.f;        //!< 左スティックのY軸
            float    rightStickX = 0.f;       //!< 右スティックのX軸
            float    rightStickY = 0.f;       //!< 右スティックのY軸
            float    leftTrigger = 0.f;       //!< アナログ値 [0, 1]
            float    rightTrigger = 0.f;      //!< アナログ値 [0, 1]
            float    vibrationDuration = 0.f; //!< 残り秒数（-1.0f = 無限）
            float    leftMotor = 0.f;         //!< 左モーターの強さ [0, 1]
            float    rightMotor = 0.f;        //!< 右モーターの強さ [0, 1]
        };

        /*
          * @brief 入力状態を更新する内部関数
          */
        void updateKeyboard();

        /*
          * @brief マウス状態を更新する内部関数
          */
        void updateMouse();

        /*
          * @brief ゲームパッド状態を更新する内部関数
          */
        void updateGamepads();

        /*
          * @brief ゲームパッドのバイブレーションを更新する内部関数
          * @param dt 前フレームからの経過時間（秒）
          */
        void updateVibration(float dt);

        /*
          * @brief 入力バッファを更新する内部関数
          */
        void updateBuffer();

        /*
          * @brief スティックのデッドゾーンを適用する内部関数
          * @param rawX 生のX軸値
          * @param rawY 生のY軸値
          * @param deadzone デッドゾーンの半径
          * @param outX デッドゾーン適用後のX軸値（-1.0f～1.0f）
          * @param outY デッドゾーン適用後のY軸値（-1.0f～1.0f）
          */
        static void applyStickDeadzone(float rawX, float rawY, float deadzone, float& outX, float& outY);

        /*
          * @brief トリガーのデッドゾーンを適用する内部関数
          * @param raw 生のトリガー値（0～255）
          * @return デッドゾーン適用後の値（0.0f～1.0f）
          */
        static float applyTriggerDeadzone(uint8_t raw);

        /*
          * @brief ゲームパッドのインデックスが有効かどうかをチェックする内部関数
          * @param index コントローラーのインデックス（0～3）
          * @return 有効な場合は true、そうでない場合は false
          */
        static bool isValidIndex(int index);

        /*
          * @brief ゲームパッドボタンが押されたかどうかをチェックする内部関数
          * @param btn ゲームパッドのボタン
          * @param index コントローラーのインデックス（0～3）
          * @return 押された場合は true、そうでない場合は false
          */
        bool checkGamepadButtonPressed(GamepadButton btn, int index) const;

        /*
          * @brief ゲームパッドボタンが押され続けているかどうかをチェックする内部関数
          * @param btn ゲームパッドのボタン
          * @param index コントローラーのインデックス（0～3）
          * @return 押され続けている場合は true、そうでない場合は false
          */
        bool checkGamepadButtonHeld(GamepadButton btn, int index) const;

        /*
          * @brief ゲームパッドボタンが離されたかどうかをチェックする内部関数
          * @param btn ゲームパッドのボタン
          * @param index コントローラーのインデックス（0～3）
          * @return 離された場合は true、そうでない場合は false
          */
        bool checkGamepadButtonReleased(GamepadButton btn, int index) const;

        /*
          * @brief ゲームパッドの軸の値を取得する内部関数
          * @param axis ゲームパッドの軸
          * @param index コントローラーのインデックス（0～3）
          * @return 軸の値（-1.0f～1.0f）
          */
        float getGamepadAxisInternal(GamepadAxis axis, int index) const;

        /*
          * @brief XINPUT_GAMEPAD 構造体からボタンマスクを構築する内部関数
          * @param gp XINPUT_GAMEPAD 構造体
          * @return ボタンマスク（uint32_t）
          */
        static uint32_t buildButtonMask(const XINPUT_GAMEPAD& gp);

        /*
          * @brief ゲームパッドのバイブレーションを適用する内部関数
          * @param index コントローラーのインデックス（0～3）
          */
        void applyVibration(int index);

        static constexpr int DISCONNECTED_POLL_INTERVAL = 60; //!< 未接続コントローラーのポーリング間隔（フレーム数）
        static constexpr uint8_t TRIGGER_THRESHOLD = 30;      //!< トリガーのデッドゾーン閾値（0～255）
        static constexpr float LEFT_STICK_DEADZONE = 7849.f;  //!< 左スティックのデッドゾーン半径（平方距離）
        static constexpr float RIGHT_STICK_DEADZONE = 8689.f; //!< 右スティックのデッドゾーン半径（平方距離）

        uint8_t m_prevKeys[256]{}; //!< 前フレームのキー状態
        uint8_t m_currKeys[256]{}; //!< 現フレームのキー状態
        uint8_t m_prevMouse[3]{};  //!< 前フレームのマウスボタン状態
        uint8_t m_currMouse[3]{};  //!< 現フレームのマウスボタン状態
        POINT m_mousePos{};        //!< 現フレームのマウス位置
        POINT m_prevMousePos{};    //!< 前フレームのマウス位置
        POINT m_mouseDelta{};      //!< 前フレームからのマウス移動量
        int m_mouseWheel = 0;      //!< マウスホイールの回転量（フレームごとに加算される）
        int m_prevMouseWheel = 0;  //!< 前フレームのマウスホイールの回転量
        GamepadState m_gamepads[XUSER_MAX_COUNT]{}; //!< ゲームパッドの状態
        int m_disconnectedPollCounter = 0;          //!< 未接続コントローラーのポーリングカウンター
        std::unordered_map<std::string, InputAxis>        m_axes;           //!< 軸のバインド情報
        std::unordered_map<std::string, ActionBinding>    m_actionBindings; //!< アクションのバインド情報
        std::unordered_map<std::string, InputBufferEntry> m_actionBuffers;  //!< アクションのバッファリング状態
        bool m_windowFocused = true; //!< ウィンドウがフォーカスされているかどうか
    };
}