#include "joypad.h"

Joypad::Joypad()
    : buttons(0x00)
    , select_reg(0x00)
    , interrupt_requested(false)
{
}

void Joypad::reset() {
    buttons = 0x00;
    select_reg = 0x00;
    interrupt_requested = false;
}

void Joypad::setButton(Button button, bool pressed) {
    bool was_pressed = (buttons & button) != 0;
    if (pressed) {
        buttons |= button;
        // Any 0 -> 1 transition requests the joypad interrupt.
        if (!was_pressed) {
            interrupt_requested = true;
        }
    } else {
        buttons &= static_cast<u8>(~button);
    }
}

void Joypad::write(u8 value) {
    // Only bits 4-5 are writable (select button group)
    select_reg = value & 0x30;
}

u8 Joypad::read() const {
    // Bits 6-7 always read as 1. A button reads 0 when pressed and
    // its group is selected (bit 5 = action, bit 4 = d-pad; 0 = selected).
    u8 result = 0xC0 | (select_reg & 0x30);

    bool select_action = !(select_reg & 0x20);
    bool select_dpad   = !(select_reg & 0x10);

    u8 low = 0x0F;
    if (select_action) {
        if (buttons & BTN_A)      low &= ~0x01;
        if (buttons & BTN_B)      low &= ~0x02;
        if (buttons & BTN_SELECT) low &= ~0x04;
        if (buttons & BTN_START)  low &= ~0x08;
    }
    if (select_dpad) {
        if (buttons & BTN_RIGHT) low &= ~0x01;
        if (buttons & BTN_LEFT)  low &= ~0x02;
        if (buttons & BTN_UP)    low &= ~0x04;
        if (buttons & BTN_DOWN)  low &= ~0x08;
    }
    return result | low;
}

bool Joypad::consumeInterruptRequest() {
    bool req = interrupt_requested;
    interrupt_requested = false;
    return req;
}
