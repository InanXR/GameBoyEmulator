#pragma once
#include "common/types.h"
#include <array>
#include <fstream>

// Forward declaration
class MMU;

/**
 * Picture Processing Unit (PPU)
 *
 * Renders the GameBoy's 160x144 display on both hardware models:
 *  - Background layer with scrolling (SCY/SCX)
 *  - Window layer (WY/WX) with its own internal line counter
 *  - Up to 40 sprites (8x8 or 8x16), max 10 per scanline, with
 *    DMG priority rules (X position, OAM index, BG-priority flag)
 *  - STAT interrupts (LYC=LY, HBlank, VBlank, OAM) with correct
 *    rising-edge ("blocking") behavior
 *
 * CGB model additions:
 *  - Framebuffer pixels are RGB555 colors from palette RAM
 *  - BG map attributes from VRAM bank 1: palette, tile bank, flips,
 *    BG-priority flag
 *  - Sprites: 8 palettes, tile VRAM bank, OAM-index priority
 *    (DMG-style X priority when OPRI is set)
 *  - LCDC bit 0 acts as the BG/OBJ master-priority flag
 *  - Palette RAM (CRAM) is locked while the PPU reads it (mode 3)
 *  - One HDMA block moves during each HBlank (driven through MMU)
 *
 * LCD timing per scanline: 456 T-cycles (dots)
 *   mode 2 (OAM scan)     80
 *   mode 3 (pixel transfer) 172 (fixed approximation)
 *   mode 0 (HBlank)      204
 * Frame: 154 scanlines (144 visible + 10 VBlank) = 70224 T-cycles
 * The dot clock always runs at 4.19 MHz; in CGB double-speed mode a
 * CPU M-cycle is only 2 dots long, so tick() advances accordingly.
 */
class PPU {
public:
    PPU();

    // Power-cycle: clear scanline/window state and blank the framebuffer.
    void reset();

    // Advance the PPU by one CPU M-cycle.
    void tick(MMU& mmu);

    // Framebuffer: 160x144 pixels.
    //  - DMG: palette shade 0-3 in the low bits
    //  - CGB: RGB555 color (R = bits 0-4, G = bits 5-9, B = bits 10-14)
    std::array<u16, 160 * 144> framebuffer;

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
    std::array<u8, 160> line_color_id;   // BG/window color indices (pre-palette)
    std::array<u8, 160> line_sprite_id;  // winning sprite color ID or 0xFF
    std::array<u8, 160> line_sprite_flags; // winning sprite's raw OAM attributes
    std::array<u8, 160> line_bg_attr;    // CGB: BG map attributes (palette + priority)

    void setMode(Mode new_mode, MMU& mmu);
    void requestStatInterrupt(MMU& mmu);
    void updateSTAT(MMU& mmu);
    void renderScanline(MMU& mmu);
    void renderBackgroundAndWindow(MMU& mmu);
    void renderSprites(MMU& mmu);

    u8 tilePixel(MMU& mmu, u8 bank, u16 tile_data_base, bool unsigned_mode,
                 u8 tile_num, int tile_y, int tile_x) const;
    u8 getColor(u8 palette, u8 color_id) const { return (palette >> (color_id * 2)) & 0x03; }
};
