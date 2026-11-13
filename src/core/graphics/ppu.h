#pragma once
#include "common/types.h"
#include <array>
#include <fstream>

// Forward declaration
class MMU;

/**
 * Picture Processing Unit (PPU)
 *
 * Renders the GameBoy's 160x144 display:
 *  - Background layer with scrolling (SCY/SCX)
 *  - Window layer (WY/WX) with its own internal line counter
 *  - Up to 40 sprites (8x8 or 8x16), max 10 per scanline, with
 *    DMG priority rules (X position, OAM index, BG-priority flag)
 *  - STAT interrupts (LYC=LY, HBlank, VBlank, OAM) with correct
 *    rising-edge ("blocking") behavior
 *
 * LCD timing per scanline: 456 T-cycles
 *   mode 2 (OAM scan)     80
 *   mode 3 (pixel transfer) 172 (fixed approximation)
 *   mode 0 (HBlank)      204
 * Frame: 154 scanlines (144 visible + 10 VBlank) = 70224 T-cycles
 */
class PPU {
public:
    PPU();

    // Power-cycle: clear scanline/window state and blank the framebuffer.
    void reset();

    // Advance the PPU by one M-cycle (4 T-cycles).
    void tick(MMU& mmu);

    // Framebuffer: 160x144 pixels, each pixel is a palette shade (0-3)
    std::array<u8, 160 * 144> framebuffer;

    // Set when a frame has been fully rendered and can be displayed.
    bool frame_ready;

    // Save/Load state
    void saveState(std::ofstream& file) const;
    void loadState(std::ifstream& file);

private:
    enum Mode : u8 {
        HBLANK = 0,
        VBLANK = 1,
        OAM    = 2,
        VRAM   = 3
    };

    Mode mode;
    int  dots;              // T-cycles within the current scanline
    int  scanline;          // LY (0-153)
    int  window_line;       // internal window line counter
    bool window_shown;      // window rendered at least once this frame
    bool stat_line;         // last state of the STAT interrupt line
    bool lcd_was_on;

    // Per-scanline scratch buffers
    std::array<u8, 160> line_color_id;   // BG/window color IDs (pre-palette)
    std::array<u8, 160> line_sprite_id;  // winning sprite color ID or 0xFF
    std::array<u8, 160> line_sprite_bg_priority; // winning sprite's BG priority flag

    void setMode(Mode new_mode, MMU& mmu);
    void requestStatInterrupt(MMU& mmu);
    void updateSTAT(MMU& mmu);
    void renderScanline(MMU& mmu);
    void renderBackgroundAndWindow(MMU& mmu);
    void renderSprites(MMU& mmu);

    u8 tilePixel(MMU& mmu, u16 tile_data_base, bool unsigned_mode,
                 u8 tile_num, int tile_y, int tile_x) const;
    u8 getColor(u8 palette, u8 color_id) const { return (palette >> (color_id * 2)) & 0x03; }
};
