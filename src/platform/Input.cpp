#include "tvk/platform/Input.h"
#include "tvk/platform/PAL.h"
#include <algorithm>
#include <cstring>

namespace tvk {

Input::Input() {
    m_EventQueueCount = 0;
}

void Input::BeginFrame() noexcept {
    for (auto& s : m_KeyStates) {
        s.wasPressedThisFrame = false;
        s.wasReleasedThisFrame = false;
    }
    for (auto& s : m_MouseButtonStates) {
        s.wasPressedThisFrame = false;
        s.wasReleasedThisFrame = false;
    }
    m_MouseMoveDelta = {0, 0};
    m_MouseWheelDelta = 0;
    m_EventQueueCount = 0;
}

void Input::Poll() {
    char buffer[4096];
    size_t bytes = pal::ReadInput(buffer, sizeof(buffer));
    if (bytes > 0) {
        Feed(std::string_view(buffer, bytes));
    }
    uint64_t nowNs = pal::GetTimestampNs();
    Update(static_cast<double>(nowNs) * 1e-9);
}

void Input::Feed(std::string_view bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        ProcessByte(static_cast<uint8_t>(bytes[i]), i + 1 == bytes.size());
    }
}

void Input::Update(double currentTimeSeconds) {
    m_CurrentTimeSeconds = currentTimeSeconds;

    for (size_t k = 1; k < static_cast<size_t>(KeyCode::Count); ++k) {
        KeyState& ks = m_KeyStates[k];
        if (ks.wasPressedThisFrame) {
            ks.lastEventTime = currentTimeSeconds;
        } else if (ks.isDown) {
            // Decay applies if Kitty protocol is inactive OR if the key was not initiated
            // via a protocol that guarantees explicit key release events.
            if (!m_KittyProtocolActive || !ks.hasExplicitRelease) {
                if ((currentTimeSeconds - ks.lastEventTime) > m_KeyHoldDecaySeconds) {
                    ks.isDown = false;
                    ks.hasExplicitRelease = false;
                    ks.wasReleasedThisFrame = true;
                    PushEvent(InputEvent(KeyEvent{static_cast<KeyCode>(k), KeyEventType::Release, m_ActiveModifiers, 0}));
                }
            }
        }
    }
}

bool Input::IsKeyDown(KeyCode code) const noexcept {
    size_t idx = static_cast<size_t>(code);
    return idx < m_KeyStates.size() && m_KeyStates[idx].isDown;
}

bool Input::IsKeyPressed(KeyCode code) const noexcept {
    size_t idx = static_cast<size_t>(code);
    return idx < m_KeyStates.size() && m_KeyStates[idx].wasPressedThisFrame;
}

bool Input::IsKeyReleased(KeyCode code) const noexcept {
    size_t idx = static_cast<size_t>(code);
    return idx < m_KeyStates.size() && m_KeyStates[idx].wasReleasedThisFrame;
}

bool Input::IsMouseButtonDown(MouseButton b) const noexcept {
    size_t idx = static_cast<size_t>(b);
    return idx < m_MouseButtonStates.size() && m_MouseButtonStates[idx].isDown;
}

bool Input::IsMouseButtonPressed(MouseButton b) const noexcept {
    size_t idx = static_cast<size_t>(b);
    return idx < m_MouseButtonStates.size() && m_MouseButtonStates[idx].wasPressedThisFrame;
}

bool Input::IsMouseButtonReleased(MouseButton b) const noexcept {
    size_t idx = static_cast<size_t>(b);
    return idx < m_MouseButtonStates.size() && m_MouseButtonStates[idx].wasReleasedThisFrame;
}

void Input::BindAction(std::string_view action, KeyCode key) {
    if (action.empty() || action.size() >= 32) return;
    for (auto& b : m_ActionBindings) {
        if (b.active && action == b.action && b.key == key) {
            return;
        }
    }
    for (auto& b : m_ActionBindings) {
        if (!b.active) {
            std::memcpy(b.action, action.data(), action.size());
            b.action[action.size()] = '\0';
            b.key = key;
            b.active = true;
            return;
        }
    }
}

void Input::UnbindAction(std::string_view action, KeyCode key) {
    for (auto& b : m_ActionBindings) {
        if (b.active && action == b.action && (key == KeyCode::Unknown || b.key == key)) {
            b.active = false;
        }
    }
}

bool Input::IsActionDown(std::string_view action) const noexcept {
    for (const auto& b : m_ActionBindings) {
        if (b.active && action == b.action && IsKeyDown(b.key)) {
            return true;
        }
    }
    return false;
}

bool Input::IsActionPressed(std::string_view action) const noexcept {
    for (const auto& b : m_ActionBindings) {
        if (b.active && action == b.action && IsKeyPressed(b.key)) {
            return true;
        }
    }
    return false;
}

bool Input::IsActionReleased(std::string_view action) const noexcept {
    for (const auto& b : m_ActionBindings) {
        if (b.active && action == b.action && IsKeyReleased(b.key)) {
            return true;
        }
    }
    return false;
}

void Input::PushEvent(const InputEvent& ev) noexcept {
    if (m_EventQueueCount < kMaxEventsPerFrame) {
        m_EventQueue[m_EventQueueCount++] = ev;
    }
}

void Input::OnKeyPress(KeyCode code, KeyMod mods, uint32_t cp, bool hasExplicitRelease) noexcept {
    m_ActiveModifiers = mods;
    size_t idx = static_cast<size_t>(code);
    if (idx < m_KeyStates.size()) {
        m_KeyStates[idx].wasPressedThisFrame = true;
        m_KeyStates[idx].isDown = true;
        m_KeyStates[idx].lastEventTime = m_CurrentTimeSeconds;
        m_KeyStates[idx].hasExplicitRelease = hasExplicitRelease;
    }
    PushEvent(InputEvent(KeyEvent{code, KeyEventType::Press, mods, cp}));
}

void Input::OnKeyRepeat(KeyCode code, KeyMod mods, uint32_t cp) noexcept {
    m_ActiveModifiers = mods;
    size_t idx = static_cast<size_t>(code);
    if (idx < m_KeyStates.size()) {
        m_KeyStates[idx].isDown = true;
        m_KeyStates[idx].lastEventTime = m_CurrentTimeSeconds;
    }
    PushEvent(InputEvent(KeyEvent{code, KeyEventType::Repeat, mods, cp}));
}

void Input::OnKeyRelease(KeyCode code, KeyMod mods) noexcept {
    m_ActiveModifiers = mods;
    size_t idx = static_cast<size_t>(code);
    if (idx < m_KeyStates.size()) {
        m_KeyStates[idx].isDown = false;
        m_KeyStates[idx].hasExplicitRelease = false;
        m_KeyStates[idx].wasReleasedThisFrame = true;
    }
    PushEvent(InputEvent(KeyEvent{code, KeyEventType::Release, mods, 0}));
}

KeyCode Input::CharToKeyCode(uint8_t b) noexcept {
    if (b >= 'a' && b <= 'z') {
        return static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::A) + (b - 'a'));
    }
    if (b >= 'A' && b <= 'Z') {
        return static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::A) + (b - 'A'));
    }
    if (b >= '0' && b <= '9') {
        return static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::Num0) + (b - '0'));
    }
    switch (b) {
    case ' ':  return KeyCode::Space;
    case '\'': return KeyCode::Apostrophe;
    case ',':  return KeyCode::Comma;
    case '-':  return KeyCode::Minus;
    case '.':  return KeyCode::Period;
    case '/':  return KeyCode::Slash;
    case ';':  return KeyCode::Semicolon;
    case '=':  return KeyCode::Equal;
    case '[':  return KeyCode::LeftBracket;
    case '\\': return KeyCode::Backslash;
    case ']':  return KeyCode::RightBracket;
    case '`':  return KeyCode::GraveAccent;
    case '\t': return KeyCode::Tab;
    case '\n':
    case '\r': return KeyCode::Enter;
    case 0x08:
    case 0x7F: return KeyCode::Backspace;
    case 0x1B: return KeyCode::Escape;
    default:   return KeyCode::Unknown;
    }
}

KeyMod Input::DecodeKittyMod(int modParam) noexcept {
    if (modParam <= 1) return KeyMod::None;
    int mask = modParam - 1;
    KeyMod m = KeyMod::None;
    if (mask & 1) m = m | KeyMod::Shift;
    if (mask & 2) m = m | KeyMod::Alt;
    if (mask & 4) m = m | KeyMod::Ctrl;
    if (mask & 8) m = m | KeyMod::Super;
    return m;
}

KeyMod Input::DecodeAnsiMod(int modParam) noexcept {
    return DecodeKittyMod(modParam);
}

static KeyCode DecodeKittyFunctionalKey(uint32_t cp) noexcept {
    switch (cp) {
    // Functional & Navigation keys (57344..57364)
    case 57344: return KeyCode::Escape;
    case 57345: return KeyCode::Enter;
    case 57346: return KeyCode::Tab;
    case 57347: return KeyCode::Backspace;
    case 57348: return KeyCode::Insert;
    case 57349: return KeyCode::Delete;
    case 57350: return KeyCode::Left;
    case 57351: return KeyCode::Right;
    case 57352: return KeyCode::Up;
    case 57353: return KeyCode::Down;
    case 57354: return KeyCode::PageUp;
    case 57355: return KeyCode::PageDown;
    case 57356: return KeyCode::Home;
    case 57357: return KeyCode::End;
    case 57358: return KeyCode::CapsLock;
    case 57359: return KeyCode::ScrollLock;
    case 57360: return KeyCode::NumLock;
    case 57361: return KeyCode::PrintScreen;
    case 57362: return KeyCode::Pause;

    // Function keys F1..F12 (57376..57387)
    case 57376: return KeyCode::F1;
    case 57377: return KeyCode::F2;
    case 57378: return KeyCode::F3;
    case 57379: return KeyCode::F4;
    case 57380: return KeyCode::F5;
    case 57381: return KeyCode::F6;
    case 57382: return KeyCode::F7;
    case 57383: return KeyCode::F8;
    case 57384: return KeyCode::F9;
    case 57385: return KeyCode::F10;
    case 57386: return KeyCode::F11;
    case 57387: return KeyCode::F12;

    // Keypad keys (57399..57415)
    case 57399: return KeyCode::KP0;
    case 57400: return KeyCode::KP1;
    case 57401: return KeyCode::KP2;
    case 57402: return KeyCode::KP3;
    case 57403: return KeyCode::KP4;
    case 57404: return KeyCode::KP5;
    case 57405: return KeyCode::KP6;
    case 57406: return KeyCode::KP7;
    case 57407: return KeyCode::KP8;
    case 57408: return KeyCode::KP9;
    case 57409: return KeyCode::KPDecimal;
    case 57410: return KeyCode::KPDivide;
    case 57411: return KeyCode::KPMultiply;
    case 57412: return KeyCode::KPSubtract;
    case 57413: return KeyCode::KPAdd;
    case 57414: return KeyCode::KPEnter;
    case 57415: return KeyCode::KPEqual;

    default: return KeyCode::Unknown;
    }
}

void Input::ProcessByte(uint8_t b, bool isEndOfBuffer) {
    switch (m_State) {
    case ParserState::Ground: {
        if (b == 0x1B) {
            if (isEndOfBuffer) {
                OnKeyPress(KeyCode::Escape, KeyMod::None, 27);
                m_State = ParserState::Ground;
            } else {
                m_State = ParserState::EscSeen;
            }
        } else if (b == '\t') {
            OnKeyPress(KeyCode::Tab, KeyMod::None, b);
        } else if (b == '\n' || b == '\r') {
            OnKeyPress(KeyCode::Enter, KeyMod::None, b);
        } else if (b == 0x08 || b == 0x7F) {
            OnKeyPress(KeyCode::Backspace, KeyMod::None, b);
        } else if (b >= 1 && b <= 26) {
            // Ctrl + A..Z
            KeyCode code = static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::A) + (b - 1));
            OnKeyPress(code, KeyMod::Ctrl, b);
        } else if (b >= 'a' && b <= 'z') {
            KeyCode code = static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::A) + (b - 'a'));
            OnKeyPress(code, KeyMod::None, b);
            TextInputEvent tie;
            tie.text[0] = static_cast<char>(b);
            tie.text[1] = '\0';
            tie.codepoint = b;
            PushEvent(InputEvent(tie));
        } else if (b >= 'A' && b <= 'Z') {
            KeyCode code = static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::A) + (b - 'A'));
            OnKeyPress(code, KeyMod::Shift, b);
            TextInputEvent tie;
            tie.text[0] = static_cast<char>(b);
            tie.text[1] = '\0';
            tie.codepoint = b;
            PushEvent(InputEvent(tie));
        } else if (b >= 0x20 && b <= 0x7E) {
            KeyCode code = CharToKeyCode(b);
            OnKeyPress(code, KeyMod::None, b);
            TextInputEvent tie;
            tie.text[0] = static_cast<char>(b);
            tie.text[1] = '\0';
            tie.codepoint = b;
            PushEvent(InputEvent(tie));
        } else if ((b & 0xE0) == 0xC0) {
            m_Utf8Buf[0] = static_cast<char>(b);
            m_Utf8Len = 1;
            m_Utf8Expected = 2;
            m_State = ParserState::UTF8_MultiByte;
        } else if ((b & 0xF0) == 0xE0) {
            m_Utf8Buf[0] = static_cast<char>(b);
            m_Utf8Len = 1;
            m_Utf8Expected = 3;
            m_State = ParserState::UTF8_MultiByte;
        } else if ((b & 0xF8) == 0xF0) {
            m_Utf8Buf[0] = static_cast<char>(b);
            m_Utf8Len = 1;
            m_Utf8Expected = 4;
            m_State = ParserState::UTF8_MultiByte;
        }
        break;
    }

    case ParserState::EscSeen: {
        if (b == '[') {
            m_State = ParserState::CSI_Param;
            for (size_t i = 0; i < kMaxCsiParams; ++i) {
                m_Params[i] = 0;
                m_SubParams[i] = 0;
            }
            m_ParamCount = 0;
            m_InSubParam = false;
        } else if (b == 'O') {
            m_State = ParserState::SS3;
        } else if (b == 0x1B) {
            OnKeyPress(KeyCode::Escape, KeyMod::None, 27);
            m_State = ParserState::Ground;
        } else {
            KeyCode code = CharToKeyCode(b);
            KeyMod m = KeyMod::Alt;
            if (b >= 'A' && b <= 'Z') m = m | KeyMod::Shift;
            OnKeyPress(code, m, b);
            m_State = ParserState::Ground;
        }
        break;
    }

    case ParserState::SS3: {
        switch (b) {
        case 'P': OnKeyPress(KeyCode::F1, KeyMod::None); break;
        case 'Q': OnKeyPress(KeyCode::F2, KeyMod::None); break;
        case 'R': OnKeyPress(KeyCode::F3, KeyMod::None); break;
        case 'S': OnKeyPress(KeyCode::F4, KeyMod::None); break;
        case 'A': OnKeyPress(KeyCode::Up, KeyMod::None); break;
        case 'B': OnKeyPress(KeyCode::Down, KeyMod::None); break;
        case 'C': OnKeyPress(KeyCode::Right, KeyMod::None); break;
        case 'D': OnKeyPress(KeyCode::Left, KeyMod::None); break;
        default: break;
        }
        m_State = ParserState::Ground;
        break;
    }

    case ParserState::CSI_Param: {
        if (b == '<' && m_ParamCount == 0 && m_Params[0] == 0) {
            m_State = ParserState::SGR_Mouse;
            for (size_t i = 0; i < kMaxCsiParams; ++i) {
                m_Params[i] = 0;
                m_SubParams[i] = 0;
            }
            m_ParamCount = 0;
            return;
        }

        if (b == '?') {
            // Private CSI prefix, e.g. \x1b[?25h
            return;
        }

        if (b >= '0' && b <= '9') {
            int d = b - '0';
            if (m_ParamCount < kMaxCsiParams) {
                int& target = m_InSubParam ? m_SubParams[m_ParamCount] : m_Params[m_ParamCount];
                if (target < 100000000) {
                    target = target * 10 + d;
                }
            }
            return;
        }

        if (b == ';') {
            if (m_ParamCount + 1 < kMaxCsiParams) {
                ++m_ParamCount;
            }
            m_InSubParam = false;
            return;
        }

        if (b == ':') {
            m_InSubParam = true;
            return;
        }

        if (b >= 0x20 && b <= 0x2F) {
            m_State = ParserState::CSI_Intermediate;
            return;
        }

        // Final byte handlers
        KeyMod mods = DecodeAnsiMod(m_ParamCount >= 1 ? m_Params[1] : 0);
        switch (b) {
        case 'A': OnKeyPress(KeyCode::Up, mods); break;
        case 'B': OnKeyPress(KeyCode::Down, mods); break;
        case 'C': OnKeyPress(KeyCode::Right, mods); break;
        case 'D': OnKeyPress(KeyCode::Left, mods); break;
        case 'H': OnKeyPress(KeyCode::Home, mods); break;
        case 'F': OnKeyPress(KeyCode::End, mods); break;
        case 'Z': OnKeyPress(KeyCode::Tab, KeyMod::Shift); break;
        case '~': {
            int p = m_Params[0];
            switch (p) {
            case 1:
            case 7:  OnKeyPress(KeyCode::Home, mods); break;
            case 2:  OnKeyPress(KeyCode::Insert, mods); break;
            case 3:  OnKeyPress(KeyCode::Delete, mods); break;
            case 4:
            case 8:  OnKeyPress(KeyCode::End, mods); break;
            case 5:  OnKeyPress(KeyCode::PageUp, mods); break;
            case 6:  OnKeyPress(KeyCode::PageDown, mods); break;
            case 11:
            case 12:
            case 13:
            case 14: OnKeyPress(static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::F1) + (p - 11)), mods); break;
            case 15: OnKeyPress(KeyCode::F5, mods); break;
            case 17: OnKeyPress(KeyCode::F6, mods); break;
            case 18: OnKeyPress(KeyCode::F7, mods); break;
            case 19: OnKeyPress(KeyCode::F8, mods); break;
            case 20: OnKeyPress(KeyCode::F9, mods); break;
            case 21: OnKeyPress(KeyCode::F10, mods); break;
            case 23: OnKeyPress(KeyCode::F11, mods); break;
            case 24: OnKeyPress(KeyCode::F12, mods); break;
            default: break;
            }
            break;
        }
        case 'u': {
            // Kitty keyboard protocol
            uint32_t cp = static_cast<uint32_t>(m_Params[0]);
            int modP = (m_ParamCount >= 1 && m_Params[1] > 0) ? m_Params[1] : 1;
            KeyMod kittyMods = DecodeKittyMod(modP);
            int evType = 1;
            if (m_SubParams[m_ParamCount] > 0) {
                evType = m_SubParams[m_ParamCount];
            } else if (m_SubParams[0] > 0) {
                evType = m_SubParams[0];
            } else if (m_SubParams[1] > 0) {
                evType = m_SubParams[1];
            }

            KeyCode code = KeyCode::Unknown;
            if (cp >= 57344 && cp <= 57420) {
                code = DecodeKittyFunctionalKey(cp);
            } else if (cp >= 'a' && cp <= 'z') {
                code = static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::A) + (cp - 'a'));
            } else if (cp >= 'A' && cp <= 'Z') {
                code = static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::A) + (cp - 'A'));
            } else if (cp >= '0' && cp <= '9') {
                code = static_cast<KeyCode>(static_cast<uint16_t>(KeyCode::Num0) + (cp - '0'));
            } else if (cp <= 127) {
                code = CharToKeyCode(static_cast<uint8_t>(cp));
            }

            m_KittyProtocolActive = true;
            if (evType == 3) {
                OnKeyRelease(code, kittyMods);
            } else if (evType == 2) {
                OnKeyRepeat(code, kittyMods, cp);
            } else {
                OnKeyPress(code, kittyMods, cp, true);
            }
            break;
        }
        default: break;
        }
        m_State = ParserState::Ground;
        break;
    }

    case ParserState::CSI_Intermediate: {
        if (b >= 0x20 && b <= 0x2F) {
            return;
        }
        if (b >= 0x40 && b <= 0x7E) {
            m_State = ParserState::Ground;
            return;
        }
        m_State = ParserState::Ground;
        ProcessByte(b, isEndOfBuffer);
        return;
    }

    case ParserState::SGR_Mouse: {
        if (b >= '0' && b <= '9') {
            if (m_ParamCount < kMaxCsiParams) {
                if (m_Params[m_ParamCount] < 100000000) {
                    m_Params[m_ParamCount] = m_Params[m_ParamCount] * 10 + (b - '0');
                }
            }
            return;
        }

        if (b == ';') {
            if (m_ParamCount + 1 < kMaxCsiParams) {
                ++m_ParamCount;
            }
            return;
        }

        if (b == 'M' || b == 'm') {
            int cb = m_Params[0];
            int cx = m_Params[1];
            int cy = m_Params[2];
            int cellX = cx > 0 ? cx - 1 : 0;
            int cellY = cy > 0 ? cy - 1 : 0;
            Vec2i newPos{cellX, cellY};

            KeyMod mods = KeyMod::None;
            if (cb & 4) mods = mods | KeyMod::Shift;
            if (cb & 8) mods = mods | KeyMod::Alt;
            if (cb & 16) mods = mods | KeyMod::Ctrl;
            m_ActiveModifiers = mods;

            if (b == 'm') {
                // Button release
                int bcode = cb & 3;
                MouseButton btn = (bcode == 0) ? MouseButton::Left :
                                  (bcode == 1) ? MouseButton::Middle :
                                  (bcode == 2) ? MouseButton::Right : MouseButton::None;
                m_MousePos = newPos;
                size_t bi = static_cast<size_t>(btn);
                if (bi < m_MouseButtonStates.size()) {
                    m_MouseButtonStates[bi].isDown = false;
                    m_MouseButtonStates[bi].wasReleasedThisFrame = true;
                }
                PushEvent(InputEvent(MouseButtonEvent{btn, false, newPos, mods}));
            } else {
                // 'M'
                if (cb & 64) {
                    // Scroll wheel
                    int delta = (cb & 1) ? -1 : 1;
                    m_MouseWheelDelta += delta;
                    PushEvent(InputEvent(MouseScrollEvent{delta, newPos, mods}));
                } else if (cb & 32) {
                    // Motion / Drag
                    int bcode = cb & 3;
                    MouseButton btn = MouseButton::None;
                    if (bcode == 0) btn = MouseButton::Left;
                    else if (bcode == 1) btn = MouseButton::Middle;
                    else if (bcode == 2) btn = MouseButton::Right;
                    else btn = MouseButton::None;

                    Vec2i delta = newPos - m_MousePos;
                    m_MouseMoveDelta = m_MouseMoveDelta + delta;
                    m_MousePos = newPos;
                    PushEvent(InputEvent(MouseMoveEvent{newPos, delta, mods, btn}));
                } else {
                    // Press
                    int bcode = cb & 3;
                    MouseButton btn = (bcode == 0) ? MouseButton::Left :
                                      (bcode == 1) ? MouseButton::Middle :
                                      (bcode == 2) ? MouseButton::Right : MouseButton::None;
                    m_MousePos = newPos;
                    size_t bi = static_cast<size_t>(btn);
                    if (bi < m_MouseButtonStates.size()) {
                        if (!m_MouseButtonStates[bi].isDown) {
                            m_MouseButtonStates[bi].wasPressedThisFrame = true;
                        }
                        m_MouseButtonStates[bi].isDown = true;
                    }
                    PushEvent(InputEvent(MouseButtonEvent{btn, true, newPos, mods}));
                }
            }
            m_State = ParserState::Ground;
            return;
        }

        // Non-conforming character or truncated SGR stream:
        // Safely recover to Ground and reprocess byte in Ground to avoid dropping keystrokes
        m_State = ParserState::Ground;
        ProcessByte(b, isEndOfBuffer);
        return;
    }

    case ParserState::UTF8_MultiByte: {
        if ((b & 0xC0) == 0x80) {
            m_Utf8Buf[m_Utf8Len++] = static_cast<char>(b);
            if (m_Utf8Len == m_Utf8Expected) {
                uint32_t cp = 0;
                if (m_Utf8Expected == 2) {
                    cp = ((static_cast<uint8_t>(m_Utf8Buf[0]) & 0x1F) << 6) |
                         (static_cast<uint8_t>(m_Utf8Buf[1]) & 0x3F);
                } else if (m_Utf8Expected == 3) {
                    cp = ((static_cast<uint8_t>(m_Utf8Buf[0]) & 0x0F) << 12) |
                         ((static_cast<uint8_t>(m_Utf8Buf[1]) & 0x3F) << 6) |
                         (static_cast<uint8_t>(m_Utf8Buf[2]) & 0x3F);
                } else if (m_Utf8Expected == 4) {
                    cp = ((static_cast<uint8_t>(m_Utf8Buf[0]) & 0x07) << 18) |
                         ((static_cast<uint8_t>(m_Utf8Buf[1]) & 0x3F) << 12) |
                         ((static_cast<uint8_t>(m_Utf8Buf[2]) & 0x3F) << 6) |
                         (static_cast<uint8_t>(m_Utf8Buf[3]) & 0x3F);
                }
                m_Utf8Buf[m_Utf8Len] = '\0';
                m_LastUtf8Codepoint = cp;
                std::memcpy(m_LastUtf8Text, m_Utf8Buf, m_Utf8Len + 1);

                TextInputEvent tie;
                std::memcpy(tie.text, m_Utf8Buf, m_Utf8Len + 1);
                tie.codepoint = cp;
                PushEvent(InputEvent(tie));

                m_Utf8Len = 0;
                m_Utf8Expected = 0;
                m_State = ParserState::Ground;
            }
        } else {
            m_Utf8Len = 0;
            m_Utf8Expected = 0;
            m_State = ParserState::Ground;
        }
        break;
    }
    }
}

} // namespace tvk
