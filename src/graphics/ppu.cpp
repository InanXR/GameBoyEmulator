#include "ppu.h"
#include "../memory/mmu.h"

/**
 * PPU Implementation
 * 
 * LCD Timing (per frame = 70224 cycles):
 * - OAM Scan (Mode 2): 80 cycles
 * - Pixel Transfer (Mode 3): 172 cycles
 * - H-Blank (Mode 0): 204 cycles
 * Total per scanline: 456 cycles
 * 
 * 144 visible scanlines + 10 V-Blank scanlines = 154 total
 */

PPU::PPU() : mode(OAM), mode_cycles(0), scanline(0), frame_ready(false) {
    framebuffer.fill(0);  // Initialize to white
}

void PPU::step(int cycles, MMU& mmu) {
    mode_cycles += cycles;
    
    // Check if LCD is enabled
    u8 lcdc = mmu.read(0xFF40);
    if (!(lcdc & 0x80)) {
        // LCD is off - reset state
        scanline = 0;
        mode_cycles = 0;
        mode = OAM;
        return;
    }
    
    switch (mode) {
        case OAM:  // Mode 2 - OAM scan (80 cycles)
            if (mode_cycles >= 80) {
                mode_cycles -= 80;
                setMode(VRAM, mmu);
            }
            break;
            
        case VRAM:  // Mode 3 - Pixel transfer (172 cycles)
            if (mode_cycles >= 172) {
                mode_cycles -= 172;
                
                // Render this scanline
                renderScanline(mmu);
                
                setMode(HBLANK, mmu);
            }
            break;
            
        case HBLANK:  // Mode 0 - H-Blank (204 cycles)
            if (mode_cycles >= 204) {
                mode_cycles -= 204;
                scanline++;
                
                // Update LY register
                mmu.setLY(scanline);
                
                // Check LYC=LY coincidence
                u8 lyc = mmu.read(0xFF45);
                u8 stat = mmu.read(0xFF41);
                
                if (scanline == lyc) {
                    stat |= 0x04;  // Set coincidence flag
                    
                    // Trigger STAT interrupt if enabled
                    if (stat & 0x40) {
                        u8 if_reg = mmu.read(0xFF0F);
                        mmu.write(0xFF0F, if_reg | 0x02);  // LCD STAT interrupt
                    }
                } else {
                    stat &= ~0x04;  // Clear coincidence flag
                }
                
                mmu.write(0xFF41, stat);
                
                // Scanline 144: Enter V-Blank
                if (scanline == 144) {
                    setMode(VBLANK, mmu);
                    frame_ready = true;
                    
                    // Trigger V-Blank interrupt
                    u8 if_reg = mmu.read(0xFF0F);
                    mmu.write(0xFF0F, if_reg | 0x01);
                } else {
                    // Next scanline
                    setMode(OAM, mmu);
                }
            }
            break;
            
        case VBLANK:  // Mode 1 - V-Blank (10 scanlines)
            if (mode_cycles >= 456) {  // Full scanline time
                mode_cycles -= 456;
                scanline++;
                
                mmu.setLY(scanline);
                
                // Scanline 154: Restart at 0
                if (scanline == 154) {
                    scanline = 0;
                    mmu.setLY(0);
                    setMode(OAM, mmu);
                }
            }
            break;
    }
}

void PPU::setMode(Mode new_mode, MMU& mmu) {
    mode = new_mode;
    
    // Update STAT register mode bits
    u8 stat = mmu.read(0xFF41);
    stat = (stat & 0xFC) | static_cast<u8>(new_mode);
    mmu.write(0xFF41, stat);
}

void PPU::renderScanline(MMU& mmu) {
    renderBackground(scanline, mmu);
    renderSprites(scanline, mmu);
}

void PPU::renderBackground(int line, MMU& mmu) {
    u8 lcdc = mmu.read(0xFF40);
    
    // Check if background is enabled
    if (!(lcdc & 0x01)) {
        // Background disabled - fill with white
        for (int x = 0; x < 160; x++) {
            framebuffer[line * 160 + x] = 0;
        }
        return;
    }
    
    u8 scy = mmu.read(0xFF42);  // Scroll Y
    u8 scx = mmu.read(0xFF43);  // Scroll X
    u8 bgp = mmu.read(0xFF47);  // BG Palette
    
    // Tile map: 0x9800-0x9BFF or 0x9C00-0x9FFF
    u16 tile_map = (lcdc & 0x08) ? 0x9C00 : 0x9800;
    
    // Tile data: 0x8000-0x8FFF or 0x8800-0x97FF
    u16 tile_data = (lcdc & 0x10) ? 0x8000 : 0x8800;
    bool unsigned_addressing = (lcdc & 0x10);
    
    // Calculate which row of tiles we're on
    int y = (line + scy) & 0xFF;  // Wrap around at 256
    int tile_row = y / 8;
    int tile_y = y % 8;  // Which row within the tile (0-7)
    
    // Render 160 pixels
    for (int x = 0; x < 160; x++) {
        // Calculate which tile column
        int pixel_x = (x + scx) & 0xFF;
        int tile_col = pixel_x / 8;
        int tile_x = pixel_x % 8;  // Which column within the tile (0-7)
        
        // Get tile number from tile map
        u16 tile_addr = tile_map + (tile_row * 32) + tile_col;
        u8 tile_num = mmu.read(tile_addr);
        
        // Calculate tile data address
        u16 tile_data_addr;
        if (unsigned_addressing) {
            tile_data_addr = tile_data + (tile_num * 16);
        } else {
            // Signed addressing: -128 to 127
            i8 signed_tile = static_cast<i8>(tile_num);
            tile_data_addr = tile_data + (signed_tile * 16) + 0x800;
        }
        
        // Each tile is 16 bytes (8x8 pixels, 2 bits per pixel)
        // 2 bytes per row
        u8 byte1 = mmu.read(tile_data_addr + (tile_y * 2));
        u8 byte2 = mmu.read(tile_data_addr + (tile_y * 2) + 1);
        
        // Get color (2-bit value from two bytes)
        int bit = 7 - tile_x;
        u8 color_id = ((byte2 >> bit) & 1) << 1 | ((byte1 >> bit) & 1);
        
        // Apply palette
        u8 shade = getColor(bgp, color_id);
        
        framebuffer[line * 160 + x] = shade;
    }
}

void PPU::renderSprites(int line, MMU& mmu) {
    u8 lcdc = mmu.read(0xFF40);
    
    // Check if sprites are enabled
    if (!(lcdc & 0x02)) {
        return;  // Sprites disabled
    }
    
    bool sprite_size_8x16 = (lcdc & 0x04);
    int sprite_height = sprite_size_8x16 ? 16 : 8;
    
    // OAM contains 40 sprites, 4 bytes each
    for (int i = 0; i < 40; i++) {
        u16 oam_addr = 0xFE00 + (i * 4);
        
        // Sprite attributes
        u8 y_pos = mmu.read(oam_addr) - 16;      // Y position
        u8 x_pos = mmu.read(oam_addr + 1) - 8;   // X position
        u8 tile = mmu.read(oam_addr + 2);        // Tile number
        u8 flags = mmu.read(oam_addr + 3);       // Attributes
        
        // Check if sprite is on this scanline
        if (line < y_pos || line >= y_pos + sprite_height) {
            continue;  // Not on this line
        }
        
        // Parse flags
        bool flip_x = flags & 0x20;
        bool flip_y = flags & 0x40;
        bool priority = flags & 0x80;  // 0=above BG, 1=behind BG colors 1-3
        u8 palette_num = (flags & 0x10) ? 1 : 0;
        u8 palette = mmu.read(palette_num ? 0xFF49 : 0xFF48);
        
        // Calculate which row of the sprite we're rendering
        int sprite_line = line - y_pos;
        if (flip_y) {
            sprite_line = sprite_height - 1 - sprite_line;
        }
        
        // Get tile data
        u16 tile_addr = 0x8000 + (tile * 16) + (sprite_line * 2);
        u8 byte1 = mmu.read(tile_addr);
        u8 byte2 = mmu.read(tile_addr + 1);
        
        // Render 8 pixels
        for (int px = 0; px < 8; px++) {
            int pixel_x = x_pos + px;
            
            // Check bounds
            if (pixel_x < 0 || pixel_x >= 160) {
                continue;
            }
            
            // Get color
            int bit = flip_x ? px : (7 - px);
            u8 color_id = ((byte2 >> bit) & 1) << 1 | ((byte1 >> bit) & 1);
            
            // Color 0 is transparent
            if (color_id == 0) {
                continue;
            }
            
            // Apply palette
            u8 shade = getColor(palette, color_id);
            
            // Draw pixel (sprites drawn last, so they appear on top)
            framebuffer[line * 160 + pixel_x] = shade;
        }
    }
}

u8 PPU::getColor(u8 palette, u8 color_id) const {
    // Extract 2-bit shade from palette
    return (palette >> (color_id * 2)) & 0x03;
}
