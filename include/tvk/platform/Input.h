#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace tvk {

/// 2D integer coordinate vector used for terminal cell positions and mouse deltas.
struct Vec2i {
    int x = 0;
    int y = 0;

    constexpr bool operator==(const Vec2i& o) const noexcept { return x == o.x && y == o.y; }
    constexpr bool operator!=(const Vec2i& o) const noexcept { return !(*this == o); }
    constexpr Vec2i operator-(const Vec2i& o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Vec2i operator+(const Vec2i& o) const noexcept { return {x + o.x, y + o.y}; }
};

/// Universal key code enumeration supporting standard ASCII, ANSI VT100/VT220,
/// Kitty keyboard protocol, and function/navigation keys.
enum class KeyCode : uint16_t {
    Unknown = 0,

    // Printable ASCII
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Semicolon = 59,
    Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    GraveAccent = 96,

    // Control & Navigation
    Escape = 256,
    Enter,
    Tab,
    Backspace,
    Insert,
    Delete,
    Right,
    Left,
    Down,
    Up,
    PageUp,
    PageDown,
    Home,
    End,
    CapsLock,
    ScrollLock,
    NumLock,
    PrintScreen,
    Pause,

    // Function Keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Keypad Keys
    KP0, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
    KPDecimal, KPDivide, KPMultiply, KPSubtract, KPAdd, KPEnter, KPEqual,

    // Modifiers
    LeftShift, LeftControl, LeftAlt, LeftSuper,
    RightShift, RightControl, RightAlt, RightSuper,

    Count = 512
};

/// Modifier bitmask flags
enum class KeyMod : uint8_t {
    None     = 0,
    Shift    = 1 << 0,
    Alt      = 1 << 1,
    Ctrl     = 1 << 2,
    Super    = 1 << 3,
    CapsLock = 1 << 4,
    NumLock  = 1 << 5,
};

inline constexpr KeyMod operator|(KeyMod a, KeyMod b) noexcept {
    return static_cast<KeyMod>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline constexpr KeyMod operator&(KeyMod a, KeyMod b) noexcept {
    return static_cast<KeyMod>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
inline constexpr KeyMod operator^(KeyMod a, KeyMod b) noexcept {
    return static_cast<KeyMod>(static_cast<uint8_t>(a) ^ static_cast<uint8_t>(b));
}
inline constexpr KeyMod operator~(KeyMod a) noexcept {
    return static_cast<KeyMod>(~static_cast<uint8_t>(a));
}
inline constexpr bool operator!(KeyMod mod) noexcept {
    return static_cast<uint8_t>(mod) == 0;
}
inline constexpr bool HasMod(KeyMod mods, KeyMod flag) noexcept {
    return (static_cast<uint8_t>(mods) & static_cast<uint8_t>(flag)) != 0;
}

/// SGR 1006 mouse button identifiers
enum class MouseButton : uint8_t {
    Left = 0,
    Middle = 1,
    Right = 2,
    Button4 = 3,
    Button5 = 4,
    None = 255
};

/// Key event actuation type
enum class KeyEventType : uint8_t {
    Press = 1,
    Repeat = 2,
    Release = 3
};

struct KeyEvent {
    KeyCode code = KeyCode::Unknown;
    KeyEventType type = KeyEventType::Press;
    KeyMod modifiers = KeyMod::None;
    uint32_t codepoint = 0;
};

struct MouseButtonEvent {
    MouseButton button = MouseButton::None;
    bool isDown = false;
    Vec2i pos{0, 0};
    KeyMod modifiers = KeyMod::None;
};

struct MouseMoveEvent {
    Vec2i pos{0, 0};
    Vec2i delta{0, 0};
    KeyMod modifiers = KeyMod::None;
    MouseButton heldButton = MouseButton::None;
};

struct MouseScrollEvent {
    int delta = 0;
    Vec2i pos{0, 0};
    KeyMod modifiers = KeyMod::None;
};

struct TextInputEvent {
    char text[32] = {0};
    uint32_t codepoint = 0;
};

struct InputEvent {
    enum class Type : uint8_t {
        None,
        Key,
        MouseButton,
        MouseMove,
        MouseScroll,
        TextInput
    } type = Type::None;

    union {
        KeyEvent key;
        MouseButtonEvent mouseButton;
        MouseMoveEvent mouseMove;
        MouseScrollEvent mouseScroll;
        TextInputEvent textInput;
    };

    constexpr InputEvent() noexcept : type(Type::None), key{} {}
    constexpr InputEvent(const KeyEvent& e) noexcept : type(Type::Key), key(e) {}
    constexpr InputEvent(const MouseButtonEvent& e) noexcept : type(Type::MouseButton), mouseButton(e) {}
    constexpr InputEvent(const MouseMoveEvent& e) noexcept : type(Type::MouseMove), mouseMove(e) {}
    constexpr InputEvent(const MouseScrollEvent& e) noexcept : type(Type::MouseScroll), mouseScroll(e) {}
    constexpr InputEvent(const TextInputEvent& e) noexcept : type(Type::TextInput), textInput(e) {}
};

/// 7-state Input subsystem decoding raw ANSI/CSI escape streams, Kitty progressive
/// keyboard protocol, and SGR 1006 mouse tracking.
class Input {
public:
    enum class ParserState : uint8_t {
        Ground = 0,
        EscSeen = 1,
        CSI_Param = 2,
        CSI_Intermediate = 3,
        SS3 = 4,
        SGR_Mouse = 5,
        UTF8_MultiByte = 6
    };

    Input();

    /// Clears per-frame transient state (pressed/released edges, mouse delta)
    void BeginFrame() noexcept;

    /// Drains up to 4 KB non-blocking bytes from stdin via PAL
    void Poll();

    /// Consumes raw byte buffer through 7-state FSM
    void Feed(std::string_view bytes);

    /// Advances key-release decay timers for legacy non-Kitty terminals
    void Update(double currentTimeSeconds);

    // Keyboard query API
    [[nodiscard]] bool IsKeyDown(KeyCode code) const noexcept;
    [[nodiscard]] bool IsKeyPressed(KeyCode code) const noexcept;
    [[nodiscard]] bool IsKeyReleased(KeyCode code) const noexcept;
    [[nodiscard]] KeyMod GetActiveModifiers() const noexcept { return m_ActiveModifiers; }

    // Mouse query API
    [[nodiscard]] Vec2i GetMousePos() const noexcept { return m_MousePos; }
    [[nodiscard]] Vec2i GetMouseMoveDelta() const noexcept { return m_MouseMoveDelta; }
    [[nodiscard]] bool IsMouseButtonDown(MouseButton b) const noexcept;
    [[nodiscard]] bool IsMouseButtonPressed(MouseButton b) const noexcept;
    [[nodiscard]] bool IsMouseButtonReleased(MouseButton b) const noexcept;
    [[nodiscard]] int GetMouseWheelDelta() const noexcept { return m_MouseWheelDelta; }

    // Action binding API
    void BindAction(std::string_view action, KeyCode key);
    void UnbindAction(std::string_view action, KeyCode key);
    [[nodiscard]] bool IsActionDown(std::string_view action) const noexcept;
    [[nodiscard]] bool IsActionPressed(std::string_view action) const noexcept;
    [[nodiscard]] bool IsActionReleased(std::string_view action) const noexcept;

    // Decay timer configuration
    void SetKeyHoldDecaySeconds(double seconds) noexcept {
        m_KeyHoldDecaySeconds = seconds;
    }
    [[nodiscard]] double GetKeyHoldDecaySeconds() const noexcept {
        return m_KeyHoldDecaySeconds;
    }
    void SetKeyHoldDecayMs(uint32_t ms) noexcept {
        m_KeyHoldDecaySeconds = static_cast<double>(ms) * 0.001;
    }
    [[nodiscard]] uint32_t GetKeyHoldDecayMs() const noexcept {
        return static_cast<uint32_t>(m_KeyHoldDecaySeconds * 1000.0);
    }

    // Status queries
    [[nodiscard]] bool IsKittyProtocolActive() const noexcept { return m_KittyProtocolActive; }
    [[nodiscard]] ParserState GetParserState() const noexcept { return m_State; }
    [[nodiscard]] uint32_t GetLastUtf8Codepoint() const noexcept { return m_LastUtf8Codepoint; }
    [[nodiscard]] const char* GetLastUtf8Text() const noexcept { return m_LastUtf8Text; }

    // Event queue inspection
    [[nodiscard]] size_t GetEventQueueSize() const noexcept { return m_EventQueueCount; }
    [[nodiscard]] const InputEvent* GetEventQueue() const noexcept { return m_EventQueue.data(); }
    void ClearEventQueue() noexcept { m_EventQueueCount = 0; }

private:
    void ProcessByte(uint8_t b, bool isEndOfBuffer);
    void PushEvent(const InputEvent& ev) noexcept;
    void OnKeyPress(KeyCode code, KeyMod mods, uint32_t cp = 0, bool hasExplicitRelease = false) noexcept;
    void OnKeyRepeat(KeyCode code, KeyMod mods, uint32_t cp = 0) noexcept;
    void OnKeyRelease(KeyCode code, KeyMod mods) noexcept;
    static KeyCode CharToKeyCode(uint8_t b) noexcept;
    static KeyMod DecodeKittyMod(int modParam) noexcept;
    static KeyMod DecodeAnsiMod(int modParam) noexcept;

    struct KeyState {
        bool isDown = false;
        bool wasPressedThisFrame = false;
        bool wasReleasedThisFrame = false;
        double lastEventTime = 0.0;
        bool hasExplicitRelease = false;
    };

    struct MouseBtnState {
        bool isDown = false;
        bool wasPressedThisFrame = false;
        bool wasReleasedThisFrame = false;
    };

    std::array<KeyState, static_cast<size_t>(KeyCode::Count)> m_KeyStates{};
    std::array<MouseBtnState, 5> m_MouseButtonStates{};

    Vec2i m_MousePos{0, 0};
    Vec2i m_MouseMoveDelta{0, 0};
    int m_MouseWheelDelta = 0;
    KeyMod m_ActiveModifiers = KeyMod::None;

    bool m_KittyProtocolActive = false;
    double m_KeyHoldDecaySeconds = 0.200; // Default 200 ms
    double m_CurrentTimeSeconds = 0.0;

    // FSM parser state
    ParserState m_State = ParserState::Ground;
    static constexpr size_t kMaxCsiParams = 8;
    int m_Params[kMaxCsiParams] = {0};
    int m_SubParams[kMaxCsiParams] = {0};
    size_t m_ParamCount = 0;
    bool m_InSubParam = false;

    char m_Utf8Buf[8] = {0};
    uint8_t m_Utf8Len = 0;
    uint8_t m_Utf8Expected = 0;
    uint32_t m_LastUtf8Codepoint = 0;
    char m_LastUtf8Text[32] = {0};

    // Action bindings table (zero heap allocation)
    struct ActionBinding {
        char action[32] = {0};
        KeyCode key = KeyCode::Unknown;
        bool active = false;
    };
    static constexpr size_t kMaxActionBindings = 64;
    std::array<ActionBinding, kMaxActionBindings> m_ActionBindings{};

    // Event queue ring/buffer (zero heap allocation)
    static constexpr size_t kMaxEventsPerFrame = 256;
    std::array<InputEvent, kMaxEventsPerFrame> m_EventQueue{};
    size_t m_EventQueueCount = 0;
};

} // namespace tvk
