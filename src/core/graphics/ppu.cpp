#include "ppu.h"
#include "memory/mmu.h"
#include <algorithm>

PPU::PPU() : frame_ready(false) {
    reset();
}

void PPU::reset() {
    framebuffer.fill(0);
    mode = HBLANK;
    dots = 0;
    scanline = 0;
    window_line = 0;
    window_shown = false;
    stat_line = false;
    lcd_was_on = false;
    frame_ready = false;
    line_color_id.fill(0);
    line_sprite_id.fill(0xFF);
    line_sprite_flags.fill(0);
    line_bg_attr.fill(0);
}

void PPU::tick(MMU& mmu) {
    u8 lcdc = mmu.read(0xFF40);
    bool lcd_on = (lcdc & 0x80) != 0;

    // LCD switched off: LY=0, PPU idles in mode 0, screen goes white.
    if (!lcd_on) {
        if (lcd_was_on) {
            scanline = 0;
            dots = 0;
            mode = HBLANK;
            window_line = 0;
            window_shown = false;
            stat_line = false;
            mmu.setLY(0);
            framebuffer.fill(0);
            // Palette RAM is free again, and no HDMA block can move
            // without HBlanks.
            mmu.setCramLocked(false);
        }
        lcd_was_on = false;
        return;
    }

    // LCD switched back on: restart the frame from line 0.
    if (!lcd_was_on) {
        scanline = 0;
        dots = 0;
        window_line = 0;
        window_shown = false;
        mode = OAM;
        mmu.setLY(0);
        mmu.setCramLocked(false);
    }
    lcd_was_on = true;

    // The dot clock always runs at 4.19 MHz. A normal-speed CPU
    // M-cycle is 4 dots; a CGB double-speed M-cycle is 2 dots.
    dots += mmu.isDoubleSpeed() ? 2 : 4;

    switch (mode) {
        case OAM:  // mode 2: 80 T-cycles
            if (dots >= 80) {
                setMode(VRAM, mmu);
            }
            break;

        case VRAM:  // mode 3: 172 T-cycles (fixed approximation)
            if (dots >= 80 + 172) {
                renderScanline(mmu);
                setMode(HBLANK, mmu);
            }
            break;

        case HBLANK:  // mode 0: 204 T-cycles
            if (dots >= 456) {
                dots -= 456;
                scanline++;
                mmu.setLY(static_cast<u8>(scanline));

                if (scanline == 144) {
                    // VBlank: request VBlank interrupt, mark frame ready
                    mmu.requestInterrupt(0x01);
                    frame_ready = true;
                    setMode(VBLANK, mmu);
                } else {
                    setMode(OAM, mmu);
                }
            }
            break;

        case VBLANK:  // mode 1: lines 144-153
            if (dots >= 456) {
                dots -= 456;
                if (scanline == 153) {
                    scanline = 0;
                    window_line = 0;
                    window_shown = false;
                    setMode(OAM, mmu);
                } else {
                    scanline++;
                }
                mmu.setLY(static_cast<u8>(scanline));
            }
            break;
    }

    updateSTAT(mmu);
}

// Change LCD mode and update the STAT mode bits. Also informs the MMU
// about the CGB side effects of mode transitions: palette RAM (CRAM)
// is only inaccessible during mode 3, and one HBlank DMA block moves
// at each HBlank entry while lines are visible.
void PPU::setMode(Mode new_mode, MMU& mmu) {
    mode = new_mode;
    u8 stat = mmu.read(0xFF41);
    stat = static_cast<u8>((stat & 0xFC) | mode);
    mmu.setSTAT(stat);

    mmu.setCramLocked(mode == VRAM);
    if (mode == HBLANK && scanline < 144) {
        mmu.hblankHDMA();
    }
}

// The STAT interrupt is the OR of four conditions; the interrupt fires
// only on a rising edge of that combined line ("blocking" behavior that
// games like Alleyway rely on).
//
// The LYC=LY coincidence flag is re-evaluated every tick (not just when
// LY changes), because the game may rewrite LYC mid-line: the flag must
// drop as soon as LYC no longer matches, or subsequent coincidences
// would not produce a rising edge.
void PPU::updateSTAT(MMU& mmu) {
    u8 stat = mmu.read(0xFF41);
    u8 lyc = mmu.read(0xFF45);

    bool eq = (static_cast<u8>(scanline) == lyc);
    stat = static_cast<u8>((stat & ~0x04) | (eq ? 0x04 : 0));
    mmu.setSTAT(stat);

    bool line = false;
    if (stat & 0x08) line = line || (mode == HBLANK);
    if (stat & 0x10) line = line || (mode == VBLANK);
    if (stat & 0x20) line = line || (mode == OAM);
    if (stat & 0x40) line = line || eq;  // LYC=LY

    if (line && !stat_line) {
        mmu.requestInterrupt(0x02);  // LCD STAT interrupt
    }
    stat_line = line;
}

// ============================================
// RENDERING
// ============================================

void PPU::renderScanline(MMU& mmu) {
    if (scanline >= 144) return;

    renderBackgroundAndWindow(mmu);
    renderSprites(mmu);

    size_t base = static_cast<size_t>(scanline) * 160;

    if (!mmu.isCGB()) {
        // DMG: BG/window use BGP, sprites use OBP0/OBP1, honoring
        // each winning sprite's BG-priority flag (sprites with the
        // flag set hide behind BG colors 1-3).
        u8 bgp  = mmu.read(0xFF47);
        u8 obp0 = mmu.read(0xFF48);
        u8 obp1 = mmu.read(0xFF49);

        for (int x = 0; x < 160; x++) {
            u8 bg_id = line_color_id[x];
            u8 sp = line_sprite_id[x];

            if (sp != 0xFF && sp != 0) {
                u8 sf = line_sprite_flags[x];
                bool bg_priority = (sf & 0x80) != 0;
                if (!bg_priority || bg_id == 0) {
                    bool pal1 = (sf & 0x10) != 0;
                    framebuffer[base + x] = getColor(pal1 ? obp1 : obp0, sp);
                    continue;
                }
            }
            framebuffer[base + x] = getColor(bgp, bg_id);
        }
        return;
    }

    // CGB: colors come from palette RAM; priority between BG and the
    // winning sprite pixel is decided by three flags (Pan Docs,
    // "BG-to-OBJ Priority in CGB Mode"):
    //   - BG color index 0: OBJ always wins
    //   - LCDC bit 0 clear: OBJ always wins (master priority off)
    //   - otherwise OBJ wins only if neither the sprite's OAM
    //     attribute bit 7 nor the tile's BG map attribute bit 7
    //     requests BG-over-OBJ priority
    bool master_priority = (mmu.read(0xFF40) & 0x01) != 0;

    for (int x = 0; x < 160; x++) {
        u8 bg_id  = line_color_id[x];
        u8 bg_att = line_bg_attr[x];

        if (line_sprite_id[x] != 0xFF) {
            u8 sf = line_sprite_flags[x];
            if (bg_id == 0 || !master_priority ||
                (((sf & 0x80) == 0) && ((bg_att & 0x80) == 0))) {
                framebuffer[base + x] =
                    mmu.objPaletteColor(sf & 0x07, line_sprite_id[x]);
                continue;
            }
        }
        framebuffer[base + x] = mmu.bgPaletteColor(bg_att & 0x07, bg_id);
    }
}

u8 PPU::tilePixel(MMU& mmu, u8 bank, u16 tile_data_base, bool unsigned_mode,
                  u8 tile_num, int tile_y, int tile_x) const {
    u16 addr;
    if (unsigned_mode) {
        addr = static_cast<u16>(tile_data_base + static_cast<u16>(tile_num) * 16);
    } else {
        // Signed addressing: tile numbers 128-255 map below the base
        i8 signed_tile = static_cast<i8>(tile_num);
        addr = static_cast<u16>(tile_data_base + 0x800 + static_cast<u16>(signed_tile) * 16);
    }
    addr = static_cast<u16>(addr + tile_y * 2);

    u8 byte1 = mmu.vramAt(bank, addr);
    u8 byte2 = mmu.vramAt(bank, static_cast<u16>(addr + 1));

    int bit = 7 - tile_x;
    return static_cast<u8>((((byte2 >> bit) & 1) << 1) | ((byte1 >> bit) & 1));
}

void PPU::renderBackgroundAndWindow(MMU& mmu) {
    u8 lcdc = mmu.read(0xFF40);
    bool cgb = mmu.isCGB();

    // On DMG, LCDC bit 0 blanks both BG and window. On CGB it is the
    // BG/OBJ master-priority flag instead: BG and window always draw.
    bool bg_win_enabled = (lcdc & 0x01) != 0;
    bool window_enabled = (lcdc & 0x20) != 0 && (bg_win_enabled || cgb);
    bool bg_enabled = bg_win_enabled || cgb;

    u8 scy = mmu.read(0xFF42);
    u8 scx = mmu.read(0xFF43);
    u8 wy  = mmu.read(0xFF4A);
    u8 wx  = mmu.read(0xFF4B);

    u16 bg_tilemap  = (lcdc & 0x08) ? 0x9C00 : 0x9800;
    u16 win_tilemap = (lcdc & 0x40) ? 0x9C00 : 0x9800;
    u16 tile_data   = (lcdc & 0x10) ? 0x8000 : 0x8800;
    bool unsigned_mode = (lcdc & 0x10) != 0;

    // The window becomes active once WY has been reached this frame
    // and stays active for the rest of the frame.
    int line = scanline;
    if (window_enabled && line >= wy) window_shown = true;

    bool window_on_line = window_shown && window_enabled && (wx <= 166);
    int window_x_start = static_cast<int>(wx) - 7;  // may be negative

    int y = (line + scy) & 0xFF;
    int bg_tile_row = y / 8;
    int bg_tile_y = y % 8;

    int win_y = window_line;
    int win_tile_row = win_y / 8;
    int win_tile_y = win_y % 8;

    // Count the window line once per scanline that renders it.
    bool window_used = false;

    if (cgb) line_bg_attr.fill(0);

    for (int x = 0; x < 160; x++) {
        u8 color_id;

        if (window_on_line && x >= window_x_start) {
            // Window pixel
            int wx_rel = x - window_x_start;         // 0-based column
            int col = wx_rel / 8;
            int tile_x = wx_rel % 8;

            u16 tile_addr = static_cast<u16>(win_tilemap + win_tile_row * 32 + col);
            u8 tile_num = mmu.vramAt(0, tile_addr);
            if (cgb) {
                // The attribute byte lives in VRAM bank 1 at the same
                // map address: palette (bits 0-2), tile bank (bit 3),
                // flips (bits 5-6), BG-over-OBJ priority (bit 7).
                u8 attr = mmu.vramAt(1, tile_addr);
                int ty = (attr & 0x40) ? 7 - win_tile_y : win_tile_y;
                int tx = (attr & 0x20) ? 7 - tile_x : tile_x;
                color_id = tilePixel(mmu, (attr & 0x08) ? 1 : 0,
                                     tile_data, unsigned_mode, tile_num, ty, tx);
                line_bg_attr[static_cast<size_t>(x)] = static_cast<u8>(attr & 0x87);
            } else {
                color_id = tilePixel(mmu, 0, tile_data, unsigned_mode,
                                    tile_num, win_tile_y, tile_x);
            }
            window_used = true;
        } else if (bg_enabled) {
            int pixel_x = (x + scx) & 0xFF;
            int col = pixel_x / 8;
            int tile_x = pixel_x % 8;

            u16 tile_addr = static_cast<u16>(bg_tilemap + bg_tile_row * 32 + col);
            u8 tile_num = mmu.vramAt(0, tile_addr);
            if (cgb) {
                u8 attr = mmu.vramAt(1, tile_addr);
                int ty = (attr & 0x40) ? 7 - bg_tile_y : bg_tile_y;
                int tx = (attr & 0x20) ? 7 - tile_x : tile_x;
                color_id = tilePixel(mmu, (attr & 0x08) ? 1 : 0,
                                     tile_data, unsigned_mode, tile_num, ty, tx);
                line_bg_attr[static_cast<size_t>(x)] = static_cast<u8>(attr & 0x87);
            } else {
                color_id = tilePixel(mmu, 0, tile_data, unsigned_mode,
                                    tile_num, bg_tile_y, tile_x);
            }
        } else {
            color_id = 0;  // BG disabled: blank (DMG bit 0 gates both layers)
        }

        line_color_id[static_cast<size_t>(x)] = color_id;
    }

    if (window_used) window_line++;
}

void PPU::renderSprites(MMU& mmu) {
    u8 lcdc = mmu.read(0xFF40);
    bool cgb = mmu.isCGB();

    // Always clear the sprite buffers first: when sprites are disabled
    // the previous scanline's sprite data must not leak through.
    line_sprite_id.fill(0xFF);
    line_sprite_flags.fill(0);

    if (!(lcdc & 0x02)) return;  // sprites disabled

    bool height_16 = (lcdc & 0x04) != 0;
    int sprite_height = height_16 ? 16 : 8;
    int line = scanline;

    // Hardware serves at most 10 sprites per scanline (first 10 in OAM
    // order that overlap the line).
    struct Candidate {
        int x;
        int oam_index;
        u8 tile;
        u8 flags;
        int row;
    };
    Candidate candidates[10];
    int num_candidates = 0;

    for (int i = 0; i < 40 && num_candidates < 10; i++) {
        u16 oam_addr = static_cast<u16>(0xFE00 + i * 4);
        int y = static_cast<int>(mmu.read(oam_addr)) - 16;
        int x = static_cast<int>(mmu.read(static_cast<u16>(oam_addr + 1))) - 8;
        u8 tile = mmu.read(static_cast<u16>(oam_addr + 2));
        u8 flags = mmu.read(static_cast<u16>(oam_addr + 3));

        if (line < y || line >= y + sprite_height) continue;

        int row = line - y;
        if (flags & 0x40) row = sprite_height - 1 - row;  // Y flip

        candidates[num_candidates].x = x;
        candidates[num_candidates].oam_index = i;
        candidates[num_candidates].tile = tile;
        candidates[num_candidates].flags = flags;
        candidates[num_candidates].row = row;
        num_candidates++;
    }

    if (num_candidates == 0) return;

    // Drawing priority among the selected sprites: DMG (and CGB in
    // DMG compatibility mode, OPRI bit 0 set) sorts by X coordinate,
    // ties broken by OAM index. Native CGB mode uses plain OAM order.
    if (!cgb || mmu.objPriorityByX()) {
        std::sort(candidates, candidates + num_candidates,
                  [](const Candidate& a, const Candidate& b) {
                      if (a.x != b.x) return a.x < b.x;
                      return a.oam_index < b.oam_index;
                  });
    }

    // Draw in priority order: the first sprite to emit a non-transparent
    // pixel claims it. ("BG over OBJ" is resolved later, in the scanline
    // composite, exactly like hardware does.)
    for (int i = 0; i < num_candidates; i++) {
        const Candidate& s = candidates[i];

        bool flip_x = (s.flags & 0x20) != 0;

        u8 tile_index = height_16 ? static_cast<u8>(s.tile & 0xFE) : s.tile;
        // CGB: OAM attribute bit 3 selects the sprite tile's VRAM bank.
        u8 bank = (cgb && (s.flags & 0x08)) ? 1 : 0;
        u16 tile_addr = static_cast<u16>(0x8000 + tile_index * 16 + s.row * 2);
        u8 byte1 = mmu.vramAt(bank, tile_addr);
        u8 byte2 = mmu.vramAt(bank, static_cast<u16>(tile_addr + 1));

        for (int px = 0; px < 8; px++) {
            int pixel_x = s.x + px;
            if (pixel_x < 0 || pixel_x >= 160) continue;

            int bit = flip_x ? px : (7 - px);
            u8 color_id = static_cast<u8>((((byte2 >> bit) & 1) << 1) | ((byte1 >> bit) & 1));
            if (color_id == 0) continue;  // transparent

            size_t idx = static_cast<size_t>(pixel_x);
            if (line_sprite_id[idx] != 0xFF) continue;  // claimed already

            line_sprite_id[idx] = color_id;
            line_sprite_flags[idx] = s.flags;
        }
    }
}

// ============================================
// SAVE STATE
// ============================================

void PPU::saveState(std::ofstream& file) const {
    u8 m = mode;
    file.write(reinterpret_cast<const char*>(&m), sizeof(m));
    file.write(reinterpret_cast<const char*>(&dots), sizeof(dots));
    file.write(reinterpret_cast<const char*>(&scanline), sizeof(scanline));
    file.write(reinterpret_cast<const char*>(&window_line), sizeof(window_line));
    u8 flags = static_cast<u8>((window_shown ? 1 : 0) | (stat_line ? 2 : 0) | (lcd_was_on ? 4 : 0));
    file.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
    file.write(reinterpret_cast<const char*>(framebuffer.data()), framebuffer.size());
}

void PPU::loadState(std::ifstream& file) {
    u8 m = 0;
    file.read(reinterpret_cast<char*>(&m), sizeof(m));
    mode = static_cast<Mode>(m & 0x03);
    file.read(reinterpret_cast<char*>(&dots), sizeof(dots));
    file.read(reinterpret_cast<char*>(&scanline), sizeof(scanline));
    file.read(reinterpret_cast<char*>(&window_line), sizeof(window_line));
    u8 flags = 0;
    file.read(reinterpret_cast<char*>(&flags), sizeof(flags));
    window_shown = (flags & 1) != 0;
    stat_line = (flags & 2) != 0;
    lcd_was_on = (flags & 4) != 0;
    file.read(reinterpret_cast<char*>(framebuffer.data()), framebuffer.size());
}
