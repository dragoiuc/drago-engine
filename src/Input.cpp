#include "Input.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

std::unordered_map<SDL_Scancode, INPUT_STATE> Input::keyboard_states;
std::vector<SDL_Scancode> Input::just_became_down_scancodes;
std::vector<SDL_Scancode> Input::just_became_up_scancodes;
std::unordered_map<Uint8, INPUT_STATE> Input::mouse_button_states;
std::vector<Uint8> Input::just_became_down_mouse_buttons;
std::vector<Uint8> Input::just_became_up_mouse_buttons;
glm::vec2 Input::mouse_position = glm::vec2(0.0f, 0.0f);
float Input::mouse_scroll_delta = 0.0f;
bool Input::mouse_capture_blocked = false;
bool Input::keyboard_capture_blocked = false;

namespace {
    std::string NormalizeKeycode(std::string keycode) {
        std::transform(keycode.begin(), keycode.end(), keycode.begin(),
            [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
        return keycode;
    }

    const std::unordered_map<std::string, SDL_Scancode>& KeyAliases() {
        static const std::unordered_map<std::string, SDL_Scancode> aliases = {
            {"up", SDL_SCANCODE_UP},
            {"down", SDL_SCANCODE_DOWN},
            {"left", SDL_SCANCODE_LEFT},
            {"right", SDL_SCANCODE_RIGHT},
            {"escape", SDL_SCANCODE_ESCAPE},
            {"lshift", SDL_SCANCODE_LSHIFT},
            {"left shift", SDL_SCANCODE_LSHIFT},
            {"rshift", SDL_SCANCODE_RSHIFT},
            {"right shift", SDL_SCANCODE_RSHIFT},
            {"lctrl", SDL_SCANCODE_LCTRL},
            {"left ctrl", SDL_SCANCODE_LCTRL},
            {"rctrl", SDL_SCANCODE_RCTRL},
            {"right ctrl", SDL_SCANCODE_RCTRL},
            {"lalt", SDL_SCANCODE_LALT},
            {"left alt", SDL_SCANCODE_LALT},
            {"ralt", SDL_SCANCODE_RALT},
            {"right alt", SDL_SCANCODE_RALT},
            {"tab", SDL_SCANCODE_TAB},
            {"return", SDL_SCANCODE_RETURN},
            {"enter", SDL_SCANCODE_RETURN},
            {"backspace", SDL_SCANCODE_BACKSPACE},
            {"delete", SDL_SCANCODE_DELETE},
            {"insert", SDL_SCANCODE_INSERT},
            {"home", SDL_SCANCODE_HOME},
            {"end", SDL_SCANCODE_END},
            {"page up", SDL_SCANCODE_PAGEUP},
            {"page down", SDL_SCANCODE_PAGEDOWN},
            {"space", SDL_SCANCODE_SPACE},
            {"/", SDL_SCANCODE_SLASH},
            {"slash", SDL_SCANCODE_SLASH},
            {";", SDL_SCANCODE_SEMICOLON},
            {"semicolon", SDL_SCANCODE_SEMICOLON},
            {"=", SDL_SCANCODE_EQUALS},
            {"equals", SDL_SCANCODE_EQUALS},
            {"-", SDL_SCANCODE_MINUS},
            {"minus", SDL_SCANCODE_MINUS},
            {".", SDL_SCANCODE_PERIOD},
            {"period", SDL_SCANCODE_PERIOD},
            {",", SDL_SCANCODE_COMMA},
            {"comma", SDL_SCANCODE_COMMA},
            {"[", SDL_SCANCODE_LEFTBRACKET},
            {"left bracket", SDL_SCANCODE_LEFTBRACKET},
            {"]", SDL_SCANCODE_RIGHTBRACKET},
            {"right bracket", SDL_SCANCODE_RIGHTBRACKET},
            {"'", SDL_SCANCODE_APOSTROPHE},
            {"\"", SDL_SCANCODE_APOSTROPHE},
            {"apostrophe", SDL_SCANCODE_APOSTROPHE},
            {"quote", SDL_SCANCODE_APOSTROPHE},
            {"double quote", SDL_SCANCODE_APOSTROPHE},
            {"\\", SDL_SCANCODE_BACKSLASH},
            {"backslash", SDL_SCANCODE_BACKSLASH},
            {"`", SDL_SCANCODE_GRAVE},
            {"grave", SDL_SCANCODE_GRAVE}
        };
        return aliases;
    }
}

void Input::Init() {
    keyboard_states.clear();
    just_became_down_scancodes.clear();
    just_became_up_scancodes.clear();
    for (int code = SDL_SCANCODE_UNKNOWN; code < SDL_NUM_SCANCODES; ++code) {
        keyboard_states[static_cast<SDL_Scancode>(code)] = INPUT_STATE_UP;
    }

    mouse_button_states.clear();
    just_became_down_mouse_buttons.clear();
    just_became_up_mouse_buttons.clear();
    mouse_button_states[SDL_BUTTON_LEFT] = INPUT_STATE_UP;
    mouse_button_states[SDL_BUTTON_MIDDLE] = INPUT_STATE_UP;
    mouse_button_states[SDL_BUTTON_RIGHT] = INPUT_STATE_UP;
    mouse_position = glm::vec2(0.0f, 0.0f);
    mouse_scroll_delta = 0.0f;
    mouse_capture_blocked = false;
    keyboard_capture_blocked = false;
}

void Input::ProcessEvent(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN) {
        SDL_Scancode code = e.key.keysym.scancode;

        if (keyboard_states[code] == INPUT_STATE_UP ||
            keyboard_states[code] == INPUT_STATE_JUST_BECAME_UP) {
            keyboard_states[code] = INPUT_STATE_JUST_BECAME_DOWN;
            just_became_down_scancodes.push_back(code);
        }
    }
    else if (e.type == SDL_KEYUP) {
        SDL_Scancode code = e.key.keysym.scancode;

        if (keyboard_states[code] == INPUT_STATE_DOWN ||
            keyboard_states[code] == INPUT_STATE_JUST_BECAME_DOWN) {
            keyboard_states[code] = INPUT_STATE_JUST_BECAME_UP;
            just_became_up_scancodes.push_back(code);
        }
    }
    else if (e.type == SDL_MOUSEMOTION) {
        mouse_position.x = static_cast<float>(e.motion.x);
        mouse_position.y = static_cast<float>(e.motion.y);
    }
    else if (e.type == SDL_MOUSEBUTTONDOWN) {
        const Uint8 button = e.button.button;
        if (mouse_button_states[button] == INPUT_STATE_UP ||
            mouse_button_states[button] == INPUT_STATE_JUST_BECAME_UP) {
            mouse_button_states[button] = INPUT_STATE_JUST_BECAME_DOWN;
            just_became_down_mouse_buttons.push_back(button);
        }
    }
    else if (e.type == SDL_MOUSEBUTTONUP) {
        const Uint8 button = e.button.button;
        if (mouse_button_states[button] == INPUT_STATE_DOWN ||
            mouse_button_states[button] == INPUT_STATE_JUST_BECAME_DOWN) {
            mouse_button_states[button] = INPUT_STATE_JUST_BECAME_UP;
            just_became_up_mouse_buttons.push_back(button);
        }
    }
    else if (e.type == SDL_MOUSEWHEEL) {
        mouse_scroll_delta += e.wheel.preciseY;
    }
}

void Input::LateUpdate() {
    for (SDL_Scancode code : just_became_down_scancodes) {
        keyboard_states[code] = INPUT_STATE_DOWN;
    }
    just_became_down_scancodes.clear();

    for (SDL_Scancode code : just_became_up_scancodes) {
        keyboard_states[code] = INPUT_STATE_UP;
    }
    just_became_up_scancodes.clear();

    for (Uint8 button : just_became_down_mouse_buttons) {
        mouse_button_states[button] = INPUT_STATE_DOWN;
    }
    just_became_down_mouse_buttons.clear();

    for (Uint8 button : just_became_up_mouse_buttons) {
        mouse_button_states[button] = INPUT_STATE_UP;
    }
    just_became_up_mouse_buttons.clear();

    mouse_scroll_delta = 0.0f;
}

bool Input::GetKey(SDL_Scancode keycode) {
    if (keyboard_capture_blocked) {
        return false;
    }

    INPUT_STATE state = keyboard_states[keycode];
    return state == INPUT_STATE_DOWN || state == INPUT_STATE_JUST_BECAME_DOWN;
}

bool Input::GetKeyDown(SDL_Scancode keycode) {
    if (keyboard_capture_blocked) {
        return false;
    }

    return keyboard_states[keycode] == INPUT_STATE_JUST_BECAME_DOWN;
}

bool Input::GetKeyUp(SDL_Scancode keycode) {
    if (keyboard_capture_blocked) {
        return false;
    }

    return keyboard_states[keycode] == INPUT_STATE_JUST_BECAME_UP;
}

SDL_Scancode Input::ResolveKeycode(const std::string& keycode) {
    const std::string normalized_keycode = NormalizeKeycode(keycode);

    if (normalized_keycode.empty()) {
        return SDL_SCANCODE_UNKNOWN;
    }

    const auto aliasIt = KeyAliases().find(normalized_keycode);
    if (aliasIt != KeyAliases().end()) {
        return aliasIt->second;
    }

    if (normalized_keycode.size() == 1) {
        const char ch = normalized_keycode[0];
        if (ch >= 'a' && ch <= 'z') {
            return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (ch - 'a'));
        }
        switch (ch) {
        case '0': return SDL_SCANCODE_0;
        case '1': return SDL_SCANCODE_1;
        case '2': return SDL_SCANCODE_2;
        case '3': return SDL_SCANCODE_3;
        case '4': return SDL_SCANCODE_4;
        case '5': return SDL_SCANCODE_5;
        case '6': return SDL_SCANCODE_6;
        case '7': return SDL_SCANCODE_7;
        case '8': return SDL_SCANCODE_8;
        case '9': return SDL_SCANCODE_9;
        default:
            break;
        }
    }

    return SDL_SCANCODE_UNKNOWN;
}

bool Input::GetKey(const std::string& keycode) {
    const SDL_Scancode scancode = ResolveKeycode(keycode);
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return false;
    }

    return GetKey(scancode);
}

bool Input::GetKeyDown(const std::string& keycode) {
    const SDL_Scancode scancode = ResolveKeycode(keycode);
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return false;
    }

    return GetKeyDown(scancode);
}

bool Input::GetKeyUp(const std::string& keycode) {
    const SDL_Scancode scancode = ResolveKeycode(keycode);
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return false;
    }

    return GetKeyUp(scancode);
}

glm::vec2 Input::GetMousePosition() {
    return mouse_position;
}

bool Input::IsSupportedMouseButton(int button_num) {
    return button_num == 1 || button_num == 2 || button_num == 3;
}

bool Input::GetMouseButton(int button_num) {
    if (mouse_capture_blocked) {
        return false;
    }

    if (!IsSupportedMouseButton(button_num)) {
        return false;
    }

    const INPUT_STATE state = mouse_button_states[static_cast<Uint8>(button_num)];
    return state == INPUT_STATE_DOWN || state == INPUT_STATE_JUST_BECAME_DOWN;
}

bool Input::GetMouseButtonDown(int button_num) {
    if (mouse_capture_blocked) {
        return false;
    }

    if (!IsSupportedMouseButton(button_num)) {
        return false;
    }

    return mouse_button_states[static_cast<Uint8>(button_num)] == INPUT_STATE_JUST_BECAME_DOWN;
}

bool Input::GetMouseButtonUp(int button_num) {
    if (mouse_capture_blocked) {
        return false;
    }

    if (!IsSupportedMouseButton(button_num)) {
        return false;
    }

    return mouse_button_states[static_cast<Uint8>(button_num)] == INPUT_STATE_JUST_BECAME_UP;
}

float Input::GetMouseScrollDelta() {
    if (mouse_capture_blocked) {
        return 0.0f;
    }

    return mouse_scroll_delta;
}

void Input::HideCursor() {
    SDL_ShowCursor(SDL_DISABLE);
}

void Input::ShowCursor() {
    SDL_ShowCursor(SDL_ENABLE);
}

void Input::SetCaptureState(bool mouseCaptured, bool keyboardCaptured) {
    mouse_capture_blocked = mouseCaptured;
    keyboard_capture_blocked = keyboardCaptured;
}
