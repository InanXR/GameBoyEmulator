#pragma once
#include "../utils/types.h"
#include <fstream>

// Forward declaration
class MMU;

/**
 * Sharp LR35902 CPU Emulation
 * 
 * The GameBoy uses a modified Z80 processor with some differences:
 * - 8-bit CPU running at 4.194304 MHz
 * - Some Z80 instructions removed, some added
 * - Different flag register behavior
 */
class CPU {
public:
    // ============================================
    // 8-BIT REGISTERS
    // ============================================
    u8 A;  // Accumulator
    u8 B;  // General purpose
    u8 C;  // General purpose
    u8 D;  // General purpose
    u8 E;  // General purpose
    u8 F;  // Flags register
    u8 H;  // General purpose
    u8 L;  // General purpose
    
    // ============================================
    // 16-BIT REGISTERS
    // ============================================
    u16 SP;  // Stack Pointer
    u16 PC;  // Program Counter
    
    // ============================================
    // FLAG REGISTER (F) BIT POSITIONS
    // ============================================
    // F = [Z N H C 0 0 0 0]
    // Bit 7: Z - Zero flag
    // Bit 6: N - Subtraction flag (BCD)
    // Bit 5: H - Half Carry flag
    // Bit 4: C - Carry flag
    // Bits 3-0: Always 0
    
    enum Flags {
        FLAG_Z = 0x80,  // Zero flag (bit 7)
        FLAG_N = 0x40,  // Subtraction flag (bit 6)
        FLAG_H = 0x20,  // Half Carry flag (bit 5)
        FLAG_C = 0x10   // Carry flag (bit 4)
    };
    
    // ============================================
    // CPU STATE
    // ============================================
    bool halted;   // CPU halted (waiting for interrupt)
    bool stopped;  // CPU stopped (very low power mode)
    bool ime;      // Interrupt Master Enable flag
    bool ime_scheduled;  // IME enable scheduled for next instruction (EI delay)
    
    // Cycle counting for timing
    u32 cycles;
    
    // ============================================
    // PUBLIC METHODS
    // ============================================
    CPU();
    
    // Reset CPU to initial state (post-BIOS)
    void reset();
    
    // Execute one instruction
    void step(MMU& mmu);
    
    // ============================================
    // 16-BIT REGISTER PAIR ACCESS
    // ============================================
    // Combine two 8-bit registers into one 16-bit value
    
    u16 getAF() const { return (static_cast<u16>(A) << 8) | F; }
    u16 getBC() const { return (static_cast<u16>(B) << 8) | C; }
    u16 getDE() const { return (static_cast<u16>(D) << 8) | E; }
    u16 getHL() const { return (static_cast<u16>(H) << 8) | L; }
    
    void setAF(u16 val) { A = val >> 8; F = val & 0xF0; } // Lower 4 bits of F always 0
    void setBC(u16 val) { B = val >> 8; C = val & 0xFF; }
    void setDE(u16 val) { D = val >> 8; E = val & 0xFF; }
    void setHL(u16 val) { H = val >> 8; L = val & 0xFF; }
    
    // ============================================
    // FLAG MANIPULATION
    // ============================================
    
    bool getFlag(Flags flag) const { 
        return (F & flag) != 0; 
    }
    
    void setFlag(Flags flag, bool value) {
        if (value) {
            F |= flag;
        } else {
            F &= ~flag;
        }
    }
    
    // ============================================
    // OPCODE EXECUTION
    // ============================================
    
    // Execute main opcode (0x00-0xFF)
    void executeOpcode(u8 opcode, MMU& mmu);
    
    // Execute CB-prefixed opcode (0xCB 0x00-0xFF)
    void executeCBOpcode(u8 opcode, MMU& mmu);
    
    // Handle interrupt requests
    void handleInterrupts(MMU& mmu);
    
    // ============================================
    // SAVE STATE
    // ============================================
    void saveState(std::ofstream& file) const;
    void loadState(std::ifstream& file);
    
private:
    // ============================================
    // MEMORY ACCESS HELPERS
    // ============================================
    // These add cycle counts automatically
    
    u8 read8(MMU& mmu, u16 addr);
    void write8(MMU& mmu, u16 addr, u8 value);
    u16 read16(MMU& mmu, u16 addr);
    void write16(MMU& mmu, u16 addr, u16 value);
    
    // ============================================
    // STACK OPERATIONS
    // ============================================
    
    void push16(MMU& mmu, u16 value);
    u16 pop16(MMU& mmu);
    
    // ============================================
    // INSTRUCTION IMPLEMENTATIONS
    // ============================================
    // These will be defined in instructions.cpp
    
    // Control
    void NOP();
    void HALT();
    void STOP();
    void DI();
    void EI();
    
    // 8-bit loads
    void LD_r_r(u8& dest, u8 src);
    void LD_r_n(u8& dest, u8 n);
    void LD_r_HL(u8& dest, MMU& mmu);
    void LD_HL_r(MMU& mmu, u8 src);
    void LD_HL_n(MMU& mmu, u8 n);
    
    // 8-bit arithmetic
    void ADD_A_r(u8 value);
    void ADC_A_r(u8 value);
    void SUB_A_r(u8 value);
    void SBC_A_r(u8 value);
    void AND_A_r(u8 value);
    void OR_A_r(u8 value);
    void XOR_A_r(u8 value);
    void CP_A_r(u8 value);
    void INC_r(u8& reg);
    void DEC_r(u8& reg);
    
    // 16-bit arithmetic
    void ADD_HL_rr(u16 value);
    void INC_rr(u16& reg);
    void DEC_rr(u16& reg);
    
    // Jumps
    void JP_nn(u16 addr);
    void JP_cc_nn(bool condition, u16 addr);
    void JR_n(i8 offset);
    void JR_cc_n(bool condition, i8 offset);
    
    // Calls and returns
    void CALL_nn(MMU& mmu, u16 addr);
    void CALL_cc_nn(MMU& mmu, bool condition, u16 addr);
    void RET(MMU& mmu);
    void RET_cc(MMU& mmu, bool condition);
    void RETI(MMU& mmu);
    void RST(MMU& mmu, u8 vector);
    
    // Rotates and shifts
    void RLCA();
    void RLA();
    void RRCA();
    void RRA();
    void RLC_r(u8& reg);
    void RL_r(u8& reg);
    void RRC_r(u8& reg);
    void RR_r(u8& reg);
    void SLA_r(u8& reg);
    void SRA_r(u8& reg);
    void SRL_r(u8& reg);
    void SWAP_r(u8& reg);
    
    // Bit operations
    void BIT_b_r(u8 bit, u8 reg);
    void SET_b_r(u8 bit, u8& reg);
    void RES_b_r(u8 bit, u8& reg);
    
    // Miscellaneous
    void DAA();
    void CPL();
    void CCF();
    void SCF();
};
