#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

enum INPUT_STATE {
    INPUT_STATE_UP,
    INPUT_STATE_JUST_BECAME_DOWN,
    INPUT_STATE_DOWN,
    INPUT_STATE_JUST_BECAME_UP
};

class Input {
public:
    static bool GetKey(SDL_Scancode keycode);
    static bool GetKeyDown(SDL_Scancode keycode);
    static bool GetKeyUp(SDL_Scancode keycode);
    static bool GetKey(const std::string& keycode);
    static bool GetKeyDown(const std::string& keycode);
    static bool GetKeyUp(const std::string& keycode);
    static glm::vec2 GetMousePosition();
    static bool GetMouseButton(int button_num);
    static bool GetMouseButtonDown(int button_num);
    static bool GetMouseButtonUp(int button_num);
    static float GetMouseScrollDelta();
    static void HideCursor();
    static void ShowCursor();
    static void SetCaptureState(bool mouseCaptured, bool keyboardCaptured);

    static void Init();                 // Call before main loop begins.
    static void ProcessEvent(const SDL_Event& e); // Call during event loop.
    static void LateUpdate();           // Call at very end of frame.

private:
    static SDL_Scancode ResolveKeycode(const std::string& keycode);
    static bool IsSupportedMouseButton(int button_num);

    static std::unordered_map<SDL_Scancode, INPUT_STATE> keyboard_states;
    static std::vector<SDL_Scancode> just_became_down_scancodes;
    static std::vector<SDL_Scancode> just_became_up_scancodes;
    static std::unordered_map<Uint8, INPUT_STATE> mouse_button_states;
    static std::vector<Uint8> just_became_down_mouse_buttons;
    static std::vector<Uint8> just_became_up_mouse_buttons;
    static glm::vec2 mouse_position;
    static float mouse_scroll_delta;
    static bool mouse_capture_blocked;
    static bool keyboard_capture_blocked;
};
