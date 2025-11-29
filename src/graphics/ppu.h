#pragma once
#include "../utils/types.h"
#include <array>
#include <fstream>

// Forward declaration
class MMU;

/**
 * Picture Processing Unit (PPU)
 * 
 * Renders the GameBoy's 160x144 display:
 * - Tile-based background and window
 * - Up to 40 sprites (8x8 or 8x16)
 * - 4 shades of gray
 * - ~59.7 Hz refresh rate
 */
class PPU {
public:
    PPU();
    
    // Step PPU forward by given cycles
    void step(int cycles, MMU& mmu);
    
    // Render current scanline
    void render(MMU& mmu);
    
    // Framebuffer: 160x144 pixels, each pixel is 0-3 (4 shades)
    std::array<u8, 160 * 144> framebuffer;
    
    // Signal that a frame is ready to display
    bool frame_ready;
    
    // Save/Load state (minimal stub)
    void saveState(std::ofstream& file) const { /* TODO: implement */ }
    void loadState(std::ifstream& file) { /* TODO: implement */ }
    
private:
    // LCD timing modes
    enum Mode {
        HBLANK  = 0,  // Horizontal blank
        VBLANK  = 1,  // Vertical blank
        OAM     = 2,  // OAM scan
        VRAM    = 3   // Pixel transfer (drawing)
    };
    
    Mode mode;
    int mode_cycles;  // Cycles in current mode
    int scanline;     // Current scanline (LY register, 0-153)
    
    // Change LCD mode and update STAT register
    void setMode(Mode new_mode, MMU& mmu);
    
    // Render a single scanline
    void renderScanline(MMU& mmu);
    
    // Render background layer for a scanline
    void renderBackground(int line, MMU& mmu);
    
    // Render sprites for a scanline
    void renderSprites(int line, MMU& mmu);
    
    // Convert palette color ID to shade (0-3)
    u8 getColor(u8 palette, u8 color_id) const;
};
