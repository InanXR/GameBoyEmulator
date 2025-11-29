#include "cpu.h"
#include "../memory/mmu.h"
#include <iostream>

/**
 * CPU Implementation
 * Sharp LR35902 (modified Z80)
 */

CPU::CPU() {
    reset();
}

void CPU::reset() {
    // GameBoy initial state after BIOS execution
    // These are the register values after the boot ROM finishes
    A = 0x01;   // GameBoy model identifier
    F = 0xB0;   // Flags: Z=1, N=0, H=1, C=1
    B = 0x00;
    C = 0x13;
    D = 0x00;
    E = 0xD8;
    H = 0x01;
    L = 0x4D;
    
    SP = 0xFFFE;  // Stack pointer at top of RAM
    PC = 0x0100;  // Start of cartridge ROM (skip BIOS)
    
    halted = false;
    stopped = false;
    ime = false;  // Interrupts disabled initially
    ime_scheduled = false;  // No pending IME enable
    cycles = 0;
}

void CPU::step(MMU& mmu) {
    // Handle scheduled IME enable (from previous EI instruction)
    if (ime_scheduled) {
        ime = true;
        ime_scheduled = false;
    }
    
    // If CPU is halted, wait for interrupt
    if (halted) {
        cycles += 4;  // Still count cycles while halted
        
        // Check if any interrupt is pending
        u8 IF = mmu.read(0xFF0F);  // Interrupt Flag
        u8 IE = mmu.read(0xFFFF);  // Interrupt Enable
        
        if ((IF & IE & 0x1F) != 0) {
            halted = false;  // Wake up from halt
        }
        return;
    }
    
    // Fetch opcode at PC
    u8 opcode = read8(mmu, PC++);
    
    // Execute the instruction
    executeOpcode(opcode, mmu);
    
    // Handle interrupts
    handleInterrupts(mmu);
}

// ============================================
// MEMORY ACCESS (with cycle counting)
// ============================================

u8 CPU::read8(MMU& mmu, u16 addr) {
    cycles += 4;  // Memory read takes 4 cycles (1 M-cycle)
    return mmu.read(addr);
}

void CPU::write8(MMU& mmu, u16 addr, u8 value) {
    cycles += 4;  // Memory write takes 4 cycles
    mmu.write(addr, value);
}

u16 CPU::read16(MMU& mmu, u16 addr) {
    // GameBoy is little-endian: low byte first, high byte second
    u8 lo = read8(mmu, addr);
    u8 hi = read8(mmu, addr + 1);
    return (static_cast<u16>(hi) << 8) | lo;
}

void CPU::write16(MMU& mmu, u16 addr, u16 value) {
    // Write low byte first, then high byte (little-endian)
    write8(mmu, addr, value & 0xFF);
    write8(mmu, addr + 1, value >> 8);
}

// ============================================
// STACK OPERATIONS
// ============================================

void CPU::push16(MMU& mmu, u16 value) {
    SP -= 2;
    write16(mmu, SP, value);
}

u16 CPU::pop16(MMU& mmu) {
    u16 value = read16(mmu, SP);
    SP += 2;
    return value;
}

// ============================================
// INTERRUPT HANDLING
// ============================================

void CPU::handleInterrupts(MMU& mmu) {
    // Only process interrupts if IME is enabled
    if (!ime) return;
    
    u8 IF = mmu.read(0xFF0F);  // Interrupt Flag register
    u8 IE = mmu.read(0xFFFF);  // Interrupt Enable register
    
    // Check which interrupts are both flagged and enabled
    u8 triggered = IF & IE & 0x1F;
    
    if (triggered == 0) return;  // No interrupts to handle
    
    // Process interrupts in priority order:
    // VBlank > LCD > Timer > Serial > Joypad
    for (int i = 0; i < 5; i++) {
        if (triggered & (1 << i)) {
            // Service this interrupt
            halted = false;  // Wake from HALT if halted
            ime = false;     // Disable further interrupts
            
            // Clear the interrupt flag
            mmu.write(0xFF0F, IF & ~(1 << i));
            
            // Push current PC to stack
            push16(mmu, PC);
            
            // Jump to interrupt vector
            // VBlank: 0x40, LCD: 0x48, Timer: 0x50, Serial: 0x58, Joypad: 0x60
            PC = 0x0040 + (i * 0x08);
            
            cycles += 20;  // Interrupt handling overhead (5 M-cycles)
            break;  // Only service one interrupt per step
        }
    }
}

// ============================================
// BASIC INSTRUCTION IMPLEMENTATIONS
// ============================================

void CPU::NOP() {
    // Do nothing - 4 cycles already counted by read8
}

void CPU::HALT() {
    halted = true;
    // CPU enters low-power mode until interrupt occurs
}

void CPU::STOP() {
    stopped = true;
    // Very low power mode - requires button press to resume
    // For now, treat similar to HALT
}

void CPU::DI() {
    ime = false;  // Disable interrupts
}

void CPU::EI() {
    // IME is enabled after the NEXT instruction (1 instruction delay)
    ime_scheduled = true;
}

// ============================================
// 8-BIT LOADS
// ============================================

void CPU::LD_r_r(u8& dest, u8 src) {
    dest = src;
}

void CPU::LD_r_n(u8& dest, u8 n) {
    dest = n;
}

void CPU::LD_r_HL(u8& dest, MMU& mmu) {
    dest = read8(mmu, getHL());
}

void CPU::LD_HL_r(MMU& mmu, u8 src) {
    write8(mmu, getHL(), src);
}

void CPU::LD_HL_n(MMU& mmu, u8 n) {
    write8(mmu, getHL(), n);
}

// ============================================
// 8-BIT ARITHMETIC
// ============================================

void CPU::ADD_A_r(u8 value) {
    u16 result = A + value;
    
    // Half carry: check if bit 3 -> 4 carried
    setFlag(FLAG_H, ((A & 0x0F) + (value & 0x0F)) > 0x0F);
    
    // Carry: check if result > 255
    setFlag(FLAG_C, result > 0xFF);
    
    A = result & 0xFF;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);  // Addition, not subtraction
}

void CPU::ADC_A_r(u8 value) {
    u8 carry = getFlag(FLAG_C) ? 1 : 0;
    u16 result = A + value + carry;
    
    setFlag(FLAG_H, ((A & 0x0F) + (value & 0x0F) + carry) > 0x0F);
    setFlag(FLAG_C, result > 0xFF);
    
    A = result & 0xFF;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);
}

void CPU::SUB_A_r(u8 value) {
    // Half carry (borrow): check if lower nibble borrows
    setFlag(FLAG_H, (A & 0x0F) < (value & 0x0F));
    
    // Carry (borrow): check if A < value
    setFlag(FLAG_C, A < value);
    
    A -= value;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, true);  // Subtraction flag
}

void CPU::SBC_A_r(u8 value) {
    u8 carry = getFlag(FLAG_C) ? 1 : 0;
    
    // Calculate half-carry (borrow from bit 4)
    // Must check if lower nibble of A is less than (lower nibble of value + carry)
    setFlag(FLAG_H, (A & 0x0F) < ((value & 0x0F) + carry));
    
    // Calculate carry (borrow) - check if A < (value + carry)
    setFlag(FLAG_C, A < (value + carry));
    
    A = A - value - carry;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, true);
}

void CPU::AND_A_r(u8 value) {
    A &= value;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, true);   // Always set for AND
    setFlag(FLAG_C, false);
}

void CPU::OR_A_r(u8 value) {
    A |= value;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, false);
}

void CPU::XOR_A_r(u8 value) {
    A ^= value;
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, false);
}

void CPU::CP_A_r(u8 value) {
    // Compare (SUB without storing result)
    setFlag(FLAG_Z, A == value);
    setFlag(FLAG_N, true);
    setFlag(FLAG_H, (A & 0x0F) < (value & 0x0F));
    setFlag(FLAG_C, A < value);
}

void CPU::INC_r(u8& reg) {
    // Half carry: check if incrementing bit 3 causes carry to bit 4
    setFlag(FLAG_H, (reg & 0x0F) == 0x0F);
    
    reg++;
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    // Carry flag unaffected
}

void CPU::DEC_r(u8& reg) {
    // Half carry (borrow): check if decrementing bit 4 causes borrow from bit 3
    setFlag(FLAG_H, (reg & 0x0F) == 0);
    
    reg--;
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, true);
    // Carry flag unaffected
}

// ============================================
// JUMP INSTRUCTIONS
// ============================================

void CPU::JP_nn(u16 addr) {
    PC = addr;
}

void CPU::JP_cc_nn(bool condition, u16 addr) {
    if (condition) {
        PC = addr;
        cycles += 4;  // Extra cycle for taken branch
    }
}

void CPU::JR_n(i8 offset) {
    PC += offset;
    cycles += 4;  // JR always takes extra cycle
}

void CPU::JR_cc_n(bool condition, i8 offset) {
    if (condition) {
        PC += offset;
        cycles += 4;  // Extra cycle for taken branch
    }
}

// ============================================
// CALL AND RETURN
// ============================================

void CPU::CALL_nn(MMU& mmu, u16 addr) {
    push16(mmu, PC);
    PC = addr;
}

void CPU::CALL_cc_nn(MMU& mmu, bool condition, u16 addr) {
    if (condition) {
        push16(mmu, PC);
        PC = addr;
        cycles += 12;  // Extra cycles for taken call
    }
}

void CPU::RET(MMU& mmu) {
    PC = pop16(mmu);
}

void CPU::RET_cc(MMU& mmu, bool condition) {
    if (condition) {
        PC = pop16(mmu);
        cycles += 12;  // Extra cycles for taken return
    }
}

void CPU::RETI(MMU& mmu) {
    PC = pop16(mmu);
    ime = true;  // Re-enable interrupts
}

void CPU::RST(MMU& mmu, u8 vector) {
    push16(mmu, PC);
    PC = vector;  // Jump to fixed vector (0x00, 0x08, 0x10, ..., 0x38)
}

// ============================================
// 16-BIT ARITHMETIC
// ============================================

void CPU::ADD_HL_rr(u16 value) {
    u32 result = getHL() + value;
    
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, ((getHL() & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF);
    setFlag(FLAG_C, result > 0xFFFF);
    
    setHL(result & 0xFFFF);
    cycles += 4;
}

// ============================================
// ROTATE OPERATIONS
// ============================================

void CPU::RRCA() {
    bool carry = (A & 0x01) != 0;
    A = (A >> 1) | (carry ? 0x80 : 0);
    
    setFlag(FLAG_Z, false);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::RLA() {
    bool old_carry = getFlag(FLAG_C);
    bool new_carry = (A & 0x80) != 0;
    
    A = (A << 1) | (old_carry ? 1 : 0);
    
    setFlag(FLAG_Z, false);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, new_carry);
}

void CPU::RRA() {
    bool old_carry = getFlag(FLAG_C);
    bool new_carry = (A & 0x01) != 0;
    
    A = (A >> 1) | (old_carry ? 0x80 : 0);
    
    setFlag(FLAG_Z, false);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, new_carry);
}

// ============================================
// CB PREFIX ROTATE/SHIFT OPERATIONS
// ============================================

void CPU::RRC_r(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    reg = (reg >> 1) | (carry ? 0x80 : 0);
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::RL_r(u8& reg) {
    bool old_carry = getFlag(FLAG_C);
    bool new_carry = (reg & 0x80) != 0;
    
    reg = (reg << 1) | (old_carry ? 1 : 0);
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, new_carry);
}

void CPU::RR_r(u8& reg) {
    bool old_carry = getFlag(FLAG_C);
    bool new_carry = (reg & 0x01) != 0;
    
    reg = (reg >> 1) | (old_carry ? 0x80 : 0);
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, new_carry);
}

void CPU::SLA_r(u8& reg) {
    bool carry = (reg & 0x80) != 0;
    reg <<= 1;
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::SRA_r(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    bool msb = (reg & 0x80);
    reg = (reg >> 1) | msb;  // Keep MSB (arithmetic shift)
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::SRL_r(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    reg >>= 1;  // Logical shift (MSB becomes 0)
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::SWAP_r(u8& reg) {
    reg = ((reg & 0x0F) << 4) | ((reg & 0xF0) >> 4);
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, false);
}

// ============================================
// SAVE STATE
// ============================================

void CPU::saveState(std::ofstream& file) const {
    // Save all registers
    file.write(reinterpret_cast<const char*>(&A), sizeof(A));
    file.write(reinterpret_cast<const char*>(&F), sizeof(F));
    file.write(reinterpret_cast<const char*>(&B), sizeof(B));
    file.write(reinterpret_cast<const char*>(&C), sizeof(C));
    file.write(reinterpret_cast<const char*>(&D), sizeof(D));
    file.write(reinterpret_cast<const char*>(&E), sizeof(E));
    file.write(reinterpret_cast<const char*>(&H), sizeof(H));
    file.write(reinterpret_cast<const char*>(&L), sizeof(L));
    file.write(reinterpret_cast<const char*>(&SP), sizeof(SP));
    file.write(reinterpret_cast<const char*>(&PC), sizeof(PC));
    
    // Save state flags
    file.write(reinterpret_cast<const char*>(&halted), sizeof(halted));
    file.write(reinterpret_cast<const char*>(&stopped), sizeof(stopped));
    file.write(reinterpret_cast<const char*>(&ime), sizeof(ime));
    file.write(reinterpret_cast<const char*>(&ime_scheduled), sizeof(ime_scheduled));
    file.write(reinterpret_cast<const char*>(&cycles), sizeof(cycles));
}

void CPU::loadState(std::ifstream& file) {
    // Load all registers
    file.read(reinterpret_cast<char*>(&A), sizeof(A));
    file.read(reinterpret_cast<char*>(&F), sizeof(F));
    file.read(reinterpret_cast<char*>(&B), sizeof(B));
    file.read(reinterpret_cast<char*>(&C), sizeof(C));
    file.read(reinterpret_cast<char*>(&D), sizeof(D));
    file.read(reinterpret_cast<char*>(&E), sizeof(E));
    file.read(reinterpret_cast<char*>(&H), sizeof(H));
    file.read(reinterpret_cast<char*>(&L), sizeof(L));
    file.read(reinterpret_cast<char*>(&SP), sizeof(SP));
    file.read(reinterpret_cast<char*>(&PC), sizeof(PC));
    
    // Load state flags
    file.read(reinterpret_cast<char*>(&halted), sizeof(halted));
    file.read(reinterpret_cast<char*>(&stopped), sizeof(stopped));
    file.read(reinterpret_cast<char*>(&ime), sizeof(ime));
    file.read(reinterpret_cast<char*>(&ime_scheduled), sizeof(ime_scheduled));
    file.read(reinterpret_cast<char*>(&cycles), sizeof(cycles));
}

