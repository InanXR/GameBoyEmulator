#include "display.h"
#include <iostream>

Display::Display() 
    : window(nullptr)
    , renderer(nullptr)
    , texture(nullptr)
    , quit(false)
    , keyState(nullptr)
{
}

Display::~Display() {
    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

bool Display::init() {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create window
    window = SDL_CreateWindow(
        "GameBoy Emulator - Built by Inan",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        GB_WIDTH * SCALE,
        GB_HEIGHT * SCALE,
        SDL_WINDOW_SHOWN
    );
    
    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Create texture for framebuffer
    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        GB_WIDTH,
        GB_HEIGHT
    );
    
    if (!texture) {
        std::cerr << "Texture creation failed: " << SDL_GetError() << std::endl;
        return false;
    }
    
    // Get keyboard state
    keyState = SDL_GetKeyboardState(nullptr);
    
    std::cout << "\n🎮 SDL2 Display initialized successfully!" << std::endl;
    std::cout << "   Window: " << GB_WIDTH * SCALE << "x" << GB_HEIGHT * SCALE << std::endl;
    std::cout << "   GameBoy Screen: " << GB_WIDTH << "x" << GB_HEIGHT << std::endl;
    
    return true;
}

void Display::render(const std::array<u8, 160 * 144>& framebuffer) {
    // Convert GameBoy grayscale (0-3) to RGB
    // GameBoy palette: 0=white, 1=light gray, 2=dark gray, 3=black
    static const u8 palette[4][3] = {
        {224, 248, 208},  // 0: Lightest (almost white-green)
        {136, 192, 112},  // 1: Light green
        { 52, 104,  86},  // 2: Dark green
        {  8,  24,  32}   // 3: Darkest (almost black)
    };
    
    u8 pixels[GB_HEIGHT][GB_WIDTH][3];
    
    for (int y = 0; y < GB_HEIGHT; y++) {
        for (int x = 0; x < GB_WIDTH; x++) {
            int idx = y * GB_WIDTH + x;
            u8 shade = framebuffer[idx] & 0x03;  // 0-3
            
            pixels[y][x][0] = palette[shade][0];  // R
            pixels[y][x][1] = palette[shade][1];  // G
            pixels[y][x][2] = palette[shade][2];  // B
        }
    }
    
    // Update texture with pixel data
    SDL_UpdateTexture(texture, nullptr, pixels, GB_WIDTH * 3);
    
    // Clear screen
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    
    // Render texture (scaled)
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    
    // Present
    SDL_RenderPresent(renderer);
}

void Display::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            quit = true;
        }
        else if (event.type == SDL_KEYDOWN) {
            // ESC to quit
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                quit = true;
            }
        }
    }
}

bool Display::isKeyPressed(SDL_Scancode key) const {
    return keyState && keyState[key];
}
