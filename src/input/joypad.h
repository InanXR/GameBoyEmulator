#pragma once
#include "utils/types.h"

/**
 * GameBoy Joypad Input Handler
 * 
 * GameBoy has 8 buttons:
 * - D-Pad: Up, Down, Left, Right
 * - Action: A, B, Start, Select
 * 
 * Mapped to keyboard:
 * - Arrow Keys → D-Pad
 * - Z → A
 * - X → B  
 * - Enter → Start
 * - Shift → Select
 */
class Joypad {
public:
    Joypad();
    
    // Button states
    enum Button {
        BTN_A      = 0x01,
        BTN_B      = 0x02,
        BTN_SELECT = 0x04,
        BTN_START  = 0x08,
        BTN_RIGHT  = 0x10,
        BTN_LEFT   = 0x20,
        BTN_UP     = 0x40,
        BTN_DOWN   = 0x80
    };
    
    // Set button state (true = pressed)
    void setButton(Button button, bool pressed);
    
    // Read joypad register (0xFF00)
    u8 read(u8 select);
    
    // Write to joypad register (sets which button group to read)
    void write(u8 value);
    
    // Get current button states (for debugging)
    u8 getButtonStates() const { return buttons; }
    
private:
    u8 buttons;     // Current button states (1 = pressed)
    u8 select_reg;  // Which button group is selected
};
