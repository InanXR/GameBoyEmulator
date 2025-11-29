#include "joypad.h"

Joypad::Joypad() 
    : buttons(0x00)
    , select_reg(0x00)
{
}

void Joypad::setButton(Button button, bool pressed) {
    if (pressed) {
        buttons |= button;   // Set bit (1 = pressed)
    } else {
        buttons &= ~button;  // Clear bit (0 = not pressed)
    }
}

void Joypad::write(u8 value) {
    // Only bits 4-5 are writable (select button group)
    select_reg = value & 0x30;
}

u8 Joypad::read(u8 select) {
    u8 result = 0xCF;  // Upper bits always set
    
    // Check which button group is selected
    bool select_action = !(select & 0x20);  // Bit 5: 0 = select action buttons
    bool select_dpad   = !(select & 0x10);  // Bit 4: 0 = select d-pad
    
    if (select_action) {
        // Read action buttons (A, B, Start, Select)
        if (buttons & BTN_A)      result &= ~0x01;  // Bit 0
        if (buttons & BTN_B)      result &= ~0x02;  // Bit 1
        if (buttons & BTN_SELECT) result &= ~0x04;  // Bit 2
        if (buttons & BTN_START)  result &= ~0x08;  // Bit 3
    }
    
    if (select_dpad) {
        // Read d-pad buttons (Right, Left, Up, Down)
        if (buttons & BTN_RIGHT)  result &= ~0x01;  // Bit 0
        if (buttons & BTN_LEFT)   result &= ~0x02;  // Bit 1
        if (buttons & BTN_UP)     result &= ~0x04;  // Bit 2
        if (buttons & BTN_DOWN)   result &= ~0x08;  // Bit 3
    }
    
    return result;
}
