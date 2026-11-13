#pragma once
#include "common/types.h"

/**
 * GameBoy Joypad Input Handler
 *
 * GameBoy has 8 buttons:
 * - D-Pad: Up, Down, Left, Right
 * - Action: A, B, Start, Select
 *
 * Keyboard mapping:
 * - Arrow Keys  -> D-Pad
 * - Z          -> A
 * - X          -> B
 * - Enter      -> Start
 * - Right Shift-> Select
 *
 * Button transitions also request the Joypad interrupt (IF bit 4),
 * which aborts STOP mode on real hardware.
 */
class Joypad {
public:
    Joypad();

    // Power-cycle: clear button state and the pending interrupt.
    void reset();

    // Button states (bit flags)
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

    // Set button state (true = pressed). A newly pressed button raises
    // the joypad interrupt request (consumed via consumeInterruptRequest).
    void setButton(Button button, bool pressed);

    // Read joypad register result for the current select bits.
    u8 read() const;

    // Write to joypad register (sets which button group is read).
    void write(u8 value);

    // Get current button states (for debugging)
    u8 getButtonStates() const { return buttons; }

    // True if a button was pressed since the last call.
    bool consumeInterruptRequest();

private:
    u8 buttons;       // Current button states (1 = pressed)
    u8 select_reg;    // Which button group is selected (bits 4-5)
    bool interrupt_requested;
};
