#include "emulator.h"
#include "savestate.h"
#include <iostream>

Emulator::Emulator() 
    : running(false)
    , frameCount(0)
{
    cpu.reset();
    ppu.frame_ready = false;
}

Emulator::~Emulator() {
    running = false;
}

bool Emulator::loadROM(const std::string& filepath) {
    std::cout << "Loading ROM: " << filepath << std::endl;
    std::cout << "==========================================\n" << std::endl;
    
    mmu.loadCartridge(filepath);
    cpu.reset();
    running = true;
    
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Emulator ready!" << std::endl;
    std::cout << "==========================================\n" << std::endl;
    
    return true;
}

void Emulator::runWithDisplay() {
    // Initialize SDL2 display
    if (!display.init()) {
        std::cerr << "Failed to initialize display!" << std::endl;
        return;
    }
    
    // Initialize Audio
    mmu.getAPU().init();
    
    std::cout << "\n🚀 Starting emulation with SDL2 display..." << std::endl;
    std::cout << "   Controls:" << std::endl;
    std::cout << "     Arrow Keys → D-Pad" << std::endl;
    std::cout << "     Z → A Button" << std::endl;
    std::cout << "     X → B Button" << std::endl;
    std::cout << "     Enter → Start" << std::endl;
    std::cout << "     Shift → Select" << std::endl;
    std::cout << "     F5 → Quick Save" << std::endl;
    std::cout << "     F8 → Quick Load" << std::endl;
    std::cout << "     ESC → Quit" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    frameCount = 0;
    running = true;
    
    // Main loop with SDL2
    while (running && !display.shouldClose()) {
        // Handle SDL events
        display.handleEvents();
        
        // Save State Hotkeys
        if (display.isKeyPressed(SDL_SCANCODE_F5)) {
            static bool f5_was_pressed = false;
            if (!f5_was_pressed) {
                SaveState::save("quicksave.state", cpu, mmu, ppu, mmu.getAPU(), mmu.getTimer());
                f5_was_pressed = true;
            }
        } else {
            static bool f5_was_pressed = false;
            f5_was_pressed = false;
        }
        
        if (display.isKeyPressed(SDL_SCANCODE_F8)) {
            static bool f8_was_pressed = false;
            if (!f8_was_pressed) {
                SaveState::load("quicksave.state", cpu, mmu, ppu, mmu.getAPU(), mmu.getTimer());
                f8_was_pressed = true;
            }
        } else {
            static bool f8_was_pressed = false;
            f8_was_pressed = false;
        }
        
        // Update joypad state from keyboard
        Joypad& joypad = mmu.getJoypad();
        joypad.setButton(Joypad::BTN_UP,     display.isKeyPressed(SDL_SCANCODE_UP));
        joypad.setButton(Joypad::BTN_DOWN,   display.isKeyPressed(SDL_SCANCODE_DOWN));
        joypad.setButton(Joypad::BTN_LEFT,   display.isKeyPressed(SDL_SCANCODE_LEFT));
        joypad.setButton(Joypad::BTN_RIGHT,  display.isKeyPressed(SDL_SCANCODE_RIGHT));
        joypad.setButton(Joypad::BTN_A,      display.isKeyPressed(SDL_SCANCODE_Z));
        joypad.setButton(Joypad::BTN_B,      display.isKeyPressed(SDL_SCANCODE_X));
        joypad.setButton(Joypad::BTN_START,  display.isKeyPressed(SDL_SCANCODE_RETURN));
        joypad.setButton(Joypad::BTN_SELECT, display.isKeyPressed(SDL_SCANCODE_LSHIFT));
        
        // Run one frame worth of CPU cycles
        u32 cycles_this_frame = 0;
        while (cycles_this_frame < CYCLES_PER_FRAME) {
            u32 cycles_before = cpu.cycles;
            cpu.step(mmu);
            int cycles_elapsed = cpu.cycles - cycles_before;
            
            // Update PPU
            ppu.step(cycles_elapsed, mmu);
            
            // Update Timer
            mmu.getTimer().step(cycles_elapsed, mmu);
            
            // Update APU
            mmu.getAPU().step(cycles_elapsed);
            
            cycles_this_frame += cycles_elapsed;
        }
        
        // Render to SDL2 window
        display.render(ppu.framebuffer);
        
        frameCount++;
        
        // Debug output every second
        if (frameCount % 60 == 0) {
            std::cout << "Frame: " << frameCount 
                      << " | PC: 0x" << std::hex << cpu.PC << std::dec
                      << " | Cycles: " << cpu.cycles << std::endl;
        }
    }
    
    std::cout << "\n🎮 Emulation stopped!" << std::endl;
    std::cout << "   Total frames: " << frameCount << std::endl;
}
