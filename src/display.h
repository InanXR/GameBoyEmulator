#pragma once
#include <SDL.h>
#include "utils/types.h"
#include <array>

/**
 * SDL2 Display Wrapper
 * Handles window creation and rendering of the GameBoy screen
 */
class Display {
public:
    // GameBoy screen dimensions
    static constexpr int GB_WIDTH = 160;
    static constexpr int GB_HEIGHT = 144;
    static constexpr int SCALE = 4;  // 4x scaling for visibility
    
    Display();
    ~Display();
    
    // Initialize SDL and create window
    bool init();
    
    // Render the framebuffer to screen
    void render(const std::array<u8, 160 * 144>& framebuffer);
    
    // Check if window should close
    bool shouldClose() const { return quit; }
    
    // Handle input events
    void handleEvents();
    
    // Get key states for joypad
    bool isKeyPressed(SDL_Scancode key) const;
    
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    bool quit;
    
    const Uint8* keyState;
};
