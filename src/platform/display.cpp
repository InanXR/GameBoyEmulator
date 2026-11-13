#include "display.h"
#include <iostream>
#include <cstdio>

Display::Display(int window_scale)
    : scale(window_scale > 0 ? window_scale : 4)
    , window(nullptr)
    , renderer(nullptr)
    , texture(nullptr)
    , controller(nullptr)
    , quit(false)
    , fullscreen(false)
    , keyboard_mask(0)
    , gamepad_mask(0)
    , joypad_mask(0)
    , turbo_held(false)
{
}

Display::~Display() {
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = nullptr;
    }
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    // SDL_Quit is called by main() after the emulator is destroyed,
    // because the APU still needs SDL during member destruction.
}

bool Display::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return false;
    }

    window = SDL_CreateWindow(
        "GameBoy Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        GB_WIDTH * scale,
        GB_HEIGHT * scale,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError()
                  << " - trying software renderer" << std::endl;
        renderer = SDL_CreateRenderer(window, -1, 0);
        if (!renderer) {
            std::cerr << "Software renderer failed too: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    texture = SDL_CreateTexture(renderer,
                               SDL_PIXELFORMAT_RGB24,
                               SDL_TEXTUREACCESS_STREAMING,
                               GB_WIDTH,
                               GB_HEIGHT);
    if (!texture) {
        std::cerr << "Texture creation failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Keep pixels crisp when scaling
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_RenderSetLogicalSize(renderer, GB_WIDTH, GB_HEIGHT);

    // Open the first available game controller
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            controller = SDL_GameControllerOpen(i);
            if (controller) {
                std::cout << "Game controller connected: "
                          << SDL_GameControllerName(controller) << std::endl;
                break;
            }
        }
    }

    std::cout << "SDL2 display initialized: " << GB_WIDTH * scale << "x"
              << GB_HEIGHT * scale << std::endl;
    return true;
}

void Display::render(const std::array<u8, 160 * 144>& framebuffer) {
    // Classic DMG green palette
    static const u8 palette[4][3] = {
        {224, 248, 208},
        {136, 192, 112},
        { 52, 104,  86},
        {  8,  24,  32}
    };

    static u8 pixels[GB_HEIGHT][GB_WIDTH][3];

    for (int y = 0; y < GB_HEIGHT; y++) {
        for (int x = 0; x < GB_WIDTH; x++) {
            u8 shade = framebuffer[static_cast<size_t>(y) * GB_WIDTH + x] & 0x03;
            pixels[y][x][0] = palette[shade][0];
            pixels[y][x][1] = palette[shade][1];
            pixels[y][x][2] = palette[shade][2];
        }
    }

    SDL_UpdateTexture(texture, nullptr, pixels, GB_WIDTH * 3);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void Display::poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                quit = true;
                break;

            case SDL_KEYDOWN: {
                if (event.key.repeat) break;
                SDL_Keycode key = event.key.keysym.sym;
                if (key == SDLK_ESCAPE) {
                    quit = true;
                    break;
                }
                if (key == SDLK_TAB) {
                    turbo_held = true;
                    break;
                }
                Hotkey hk = Hotkey::None;
                switch (key) {
                    case SDLK_F1:  hk = Hotkey::Reset; break;
                    case SDLK_F2:  hk = Hotkey::Screenshot; break;
                    case SDLK_F5:  hk = Hotkey::SaveState; break;
                    case SDLK_F8:  hk = Hotkey::LoadState; break;
                    case SDLK_F11: hk = Hotkey::ToggleFullscreen; break;
                    case SDLK_p: hk = Hotkey::Pause; break;
                    case SDLK_m: hk = Hotkey::Mute; break;
                    case SDLK_KP_PLUS: case SDLK_EQUALS:
                        hk = Hotkey::VolumeUp; break;
                    case SDLK_KP_MINUS: case SDLK_MINUS:
                        hk = Hotkey::VolumeDown; break;
                    default: break;
                }
                if (hk != Hotkey::None) hotkeys.push(hk);
                onKey(key, true);
                break;
            }

            case SDL_KEYUP:
                if (event.key.keysym.sym == SDLK_TAB) {
                    turbo_held = false;
                    break;
                }
                onKey(event.key.keysym.sym, false);
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                onControllerButton(event.cbutton.button, true);
                break;
            case SDL_CONTROLLERBUTTONUP:
                onControllerButton(event.cbutton.button, false);
                break;

            case SDL_CONTROLLERDEVICEADDED:
                if (!controller) {
                    controller = SDL_GameControllerOpen(event.cdevice.which);
                    if (controller) {
                        std::cout << "Game controller connected: "
                                  << SDL_GameControllerName(controller) << std::endl;
                    }
                }
                break;

            case SDL_CONTROLLERDEVICEREMOVED:
                if (controller && event.cdevice.which ==
                        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))) {
                    SDL_GameControllerClose(controller);
                    controller = nullptr;
                    gamepad_mask = 0;
                    std::cout << "Game controller disconnected" << std::endl;
                }
                break;
        }
    }

    joypad_mask = static_cast<u8>(keyboard_mask | gamepad_mask);
}

void Display::onKey(SDL_Keycode key, bool down) {
    u8 bit = 0;
    switch (key) {
        case SDLK_UP:    bit = Joypad::BTN_UP; break;
        case SDLK_DOWN:  bit = Joypad::BTN_DOWN; break;
        case SDLK_LEFT:  bit = Joypad::BTN_LEFT; break;
        case SDLK_RIGHT: bit = Joypad::BTN_RIGHT; break;
        case SDLK_z: bit = Joypad::BTN_A; break;
        case SDLK_x: bit = Joypad::BTN_B; break;
        case SDLK_RETURN: bit = Joypad::BTN_START; break;
        case SDLK_RSHIFT:
        case SDLK_LSHIFT: bit = Joypad::BTN_SELECT; break;
        default: return;
    }

    if (down) keyboard_mask |= bit;
    else keyboard_mask = static_cast<u8>(keyboard_mask & ~bit);
}

void Display::onControllerButton(Uint8 button, bool down) {
    u8 bit = 0;
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    bit = Joypad::BTN_UP; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  bit = Joypad::BTN_DOWN; break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  bit = Joypad::BTN_LEFT; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: bit = Joypad::BTN_RIGHT; break;
        case SDL_CONTROLLER_BUTTON_A: bit = Joypad::BTN_A; break;
        case SDL_CONTROLLER_BUTTON_B: bit = Joypad::BTN_B; break;
        case SDL_CONTROLLER_BUTTON_START: bit = Joypad::BTN_START; break;
        case SDL_CONTROLLER_BUTTON_BACK:  bit = Joypad::BTN_SELECT; break;
        default: return;
    }

    if (down) gamepad_mask |= bit;
    else gamepad_mask = static_cast<u8>(gamepad_mask & ~bit);
}

Display::Hotkey Display::nextHotkey() {
    if (hotkeys.empty()) return Hotkey::None;
    Hotkey hk = hotkeys.front();
    hotkeys.pop();
    return hk;
}

void Display::toggleFullscreen() {
    if (!window) return;
    Uint32 flags = SDL_GetWindowFlags(window);
    bool is_fullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    SDL_SetWindowFullscreen(window,
                            is_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void Display::setTitleFPS(double fps) {
    char title[64];
    std::snprintf(title, sizeof(title), "GameBoy Emulator - %.1f FPS", fps);
    SDL_SetWindowTitle(window, title);
}
