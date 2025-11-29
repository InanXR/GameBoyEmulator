#include "cpu.h"
#include "../memory/mmu.h"
#include <iostream>
#include <iomanip>

/**
 * CPU INSTRUCTION IMPLEMENTATIONS
 * 
 * This file contains all 256 main opcodes and 256 CB-prefixed opcodes.
 * Total: 512 instructions
 * 
 * This is the LARGEST file in the emulator (~1500-2000 lines when complete).
 * For now, we implement the most common/essential ones to get started.
 */

void CPU::executeOpcode(u8 opcode, MMU& mmu) {
    // Opcode dispatch table
    switch (opcode) {
        // 0x00: NOP
        case 0x00:
            NOP();
            break;
            
        // 0x01: LD BC,nn
        case 0x01: {
            u16 nn = read16(mmu, PC);
            PC += 2;
            setBC(nn);
            break;
        }
        
        // 0x02: LD (BC),A
        case 0x02:
            write8(mmu, getBC(), A);
            break;
            
        // 0x03: INC BC
        case 0x03: {
            u16 bc = getBC();
            setBC(bc + 1);
            cycles += 4;  // 16-bit operations take extra cycle
            break;
        }
        
        // 0x04: INC B
        case 0x04:
            INC_r(B);
            break;
            
        // 0x05: DEC B
        case 0x05:
            DEC_r(B);
            break;
            
        // 0x06: LD B,n
        case 0x06:
            LD_r_n(B, read8(mmu, PC++));
            break;
            
        
        // 0x07: RLCA
        case 0x07:
            RLCA();
            break;
        
        // 0x08: LD (nn),SP
        case 0x08: {
            u16 addr = read16(mmu, PC);
            PC += 2;
            write16(mmu, addr, SP);
            break;
        }
        
        // 0x09: ADD HL,BC
        case 0x09:
            ADD_HL_rr(getBC());
            break;
        
        // 0x0A: LD A,(BC)
        case 0x0A:
            A = read8(mmu, getBC());
            break;
        
        // 0x0B: DEC BC
        case 0x0B: {
            setBC(getBC() - 1);
            cycles += 4;
            break;
        }
        
        // 0x0C: INC C
        case 0x0C:
            INC_r(C);
            break;
            
        // 0x0D: DEC C
        case 0x0D:
            DEC_r(C);
            break;
            
        // 0x0E: LD C,n
        case 0x0E:
            LD_r_n(C, read8(mmu, PC++));
            break;
        
        // 0x0F: RRCA
        case 0x0F:
            RRCA();
            break;
        
        // 0x10: STOP
        case 0x10:
            STOP();
            PC++;  // STOP is 2-byte instruction
            break;
        
        // 0x11: LD DE,nn
        case 0x11: {
            u16 nn = read16(mmu, PC);
            PC += 2;
            setDE(nn);
            break;
        }
        
        // 0x12: LD (DE),A
        case 0x12:
            write8(mmu, getDE(), A);
            break;
        
        // 0x13: INC DE
        case 0x13: {
            u16 de = getDE();
            setDE(de + 1);
            cycles += 4;
            break;
        }
        
        // 0x14: INC D
        case 0x14:
            INC_r(D);
            break;
        
        // 0x15: DEC D
        case 0x15:
            DEC_r(D);
            break;
        
        // 0x16: LD D,n
        case 0x16:
            LD_r_n(D, read8(mmu, PC++));
            break;
        
        // 0x17: RLA
        case 0x17:
            RLA();
            break;
        
        // 0x19: ADD HL,DE
        case 0x19:
            ADD_HL_rr(getDE());
            break;
        
        // 0x1A: LD A,(DE)
        case 0x1A:
            A = read8(mmu, getDE());
            break;
        
        // 0x1B: DEC DE
        case 0x1B: {
            setDE(getDE() - 1);
            cycles += 4;
            break;
        }
        
        // 0x1C: INC E
        case 0x1C:
            INC_r(E);
            break;
        
        // 0x1D: DEC E
        case 0x1D:
            DEC_r(E);
            break;
        
        // 0x1E: LD E,n
        case 0x1E:
            LD_r_n(E, read8(mmu, PC++));
            break;
        
        // 0x1F: RRA
        case 0x1F:
            RRA();
            break;
            
        // 0x18: JR n
        case 0x18: {
            i8 offset = static_cast<i8>(read8(mmu, PC++));
            JR_n(offset);
            break;
        }
        
        // 0x20: JR NZ,e
        case 0x20: {
            i8 offset = static_cast<i8>(read8(mmu, PC++));
            JR_cc_n(!getFlag(FLAG_Z), offset);
            break;
        }
        
        // 0x21: LD HL,nn
        case 0x21: {
            u16 nn = read16(mmu, PC);
            PC += 2;
            setHL(nn);
            break;
        }
        
        // 0x22: LD (HL+),A
        case 0x22: {
            write8(mmu, getHL(), A);
            setHL(getHL() + 1);
            break;
        }
        
        // 0x23: INC HL
        case 0x23: {
            setHL(getHL() + 1);
            cycles += 4;
            break;
        }
        
        
        // 0x24: INC H
        case 0x24:
            INC_r(H);
            break;
        
        // 0x25: DEC H
        case 0x25:
            DEC_r(H);
            break;
        
        // 0x26: LD H,n
        case 0x26:
            LD_r_n(H, read8(mmu, PC++));
            break;
        
        // 0x27: DAA (Decimal Adjust Accumulator)
        case 0x27:
            DAA();
            break;
        
        // 0x28: JR Z,e
        case 0x28: {
            i8 offset = static_cast<i8>(read8(mmu, PC++));
            JR_cc_n(getFlag(FLAG_Z), offset);
            break;
        }
        
        // 0x29: ADD HL,HL
        case 0x29:
            ADD_HL_rr( getHL());
            break;
        
        // 0x2A: LD A,(HL+)
        case 0x2A: {
            A = read8(mmu, getHL());
            setHL(getHL() + 1);
            break;
        }
        
        // 0x2B: DEC HL
        case 0x2B: {
            setHL(getHL() - 1);
            cycles += 4;
            break;
        }
        
        // 0x2C: INC L (THE ONE WE NEED!)
        case 0x2C:
            INC_r(L);
            break;
        
        // 0x2D: DEC L
        case 0x2D:
            DEC_r(L);
            break;
        
        // 0x2E: LD L,n
        case 0x2E:
            LD_r_n(L, read8(mmu, PC++));
            break;
        
        // 0x2F: CPL (Complement A)
        case 0x2F:
            A = ~A;
            setFlag(FLAG_N, true);
            setFlag(FLAG_H, true);
            break;
        
        // 0x30: JR NC,e
        case 0x30: {
            i8 offset = static_cast<i8>(read8(mmu, PC++));
            JR_cc_n(!getFlag(FLAG_C), offset);
            break;
        }
        
        // 0x31: LD SP,nn
        case 0x31: {
            SP = read16(mmu, PC);
            PC += 2;
            break;
        }
        
        // 0x32: LD (HL-),A
        case 0x32: {
            write8(mmu, getHL(), A);
            setHL(getHL() - 1);
            break;
        }
        
        // 0x33: INC SP
        case 0x33:
            SP++;
            cycles += 4;
            break;
        
        // 0x34: INC (HL)
        case 0x34: {
            u8 val = read8(mmu, getHL());
            INC_r(val);
            write8(mmu, getHL(), val);
            break;
        }
        
        // 0x35: DEC (HL)
        case 0x35: {
            u8 val = read8(mmu, getHL());
            DEC_r(val);
            write8(mmu, getHL(), val);
            break;
        }
        
        // 0x36: LD (HL),n
        case 0x36: {
            u8 n = read8(mmu, PC++);
            write8(mmu, getHL(), n);
            break;
        }
        
        // 0x37: SCF (Set Carry Flag)
        case 0x37:
            setFlag(FLAG_N, false);
            setFlag(FLAG_H, false);
            setFlag(FLAG_C, true);
            break;
        
        // 0x3F: CCF (Complement Carry Flag)
        case 0x3F:
            setFlag(FLAG_N, false);
            setFlag(FLAG_H, false);
            setFlag(FLAG_C, !getFlag(FLAG_C));
            break;
        
        // 0x38: JR C,e
        case 0x38: {
            i8 offset = static_cast<i8>(read8(mmu, PC++));
            JR_cc_n(getFlag(FLAG_C), offset);
            break;
        }
        
        // 0x39: ADD HL,SP
        case 0x39:
            ADD_HL_rr(SP);
            break;
        
        // 0x3A: LD A,(HL-)
        case 0x3A: {
            A = read8(mmu, getHL());
            setHL(getHL() - 1);
            break;
        }
        
        // 0x3B: DEC SP
        case 0x3B:
            SP--;
            cycles += 4;
            break;
        
        // 0x3C: INC A
        case 0x3C:
            INC_r(A);
            break;
        
        // 0x3D: DEC A
        case 0x3D:
            DEC_r(A);
            break;

        
        // ============================================
        // 0x40-0x7F: 8-BIT LOAD INSTRUCTIONS (LD r,r)
        // ============================================
        // LD B,r
        case 0x40: B = B; break;  // LD B,B
        case 0x41: B = C; break;  // LD B,C
        case 0x42: B = D; break;  // LD B,D
        case 0x43: B = E; break;  // LD B,E
        case 0x44: B = H; break;  // LD B,H
        case 0x45: B = L; break;  // LD B,L
        case 0x46: B = read8(mmu, getHL()); break;  // LD B,(HL)
        case 0x47: B = A; break;  // LD B,A
        
        // LD C,r
        case 0x48: C = B; break;  // LD C,B
        case 0x49: C = C; break;  // LD C,C
        case 0x4A: C = D; break;  // LD C,D
        case 0x4B: C = E; break;  // LD C,E
        case 0x4C: C = H; break;  // LD C,H
        case 0x4D: C = L; break;  // LD C,L
        case 0x4E: C = read8(mmu, getHL()); break;  // LD C,(HL)
        case 0x4F: C = A; break;  // LD C,A
        
        // LD D,r
        case 0x50: D = B; break;  // LD D,B
        case 0x51: D = C; break;  // LD D,C
        case 0x52: D = D; break;  // LD D,D
        case 0x53: D = E; break;  // LD D,E
        case 0x54: D = H; break;  // LD D,H
        case 0x55: D = L; break;  // LD D,L
        case 0x56: D = read8(mmu, getHL()); break;  // LD D,(HL)
        case 0x57: D = A; break;  // LD D,A
        
        // LD E,r
        case 0x58: E = B; break;  // LD E,B
        case 0x59: E = C; break;  // LD E,C
        case 0x5A: E = D; break;  // LD E,D
        case 0x5B: E = E; break;  // LD E,E
        case 0x5C: E = H; break;  // LD E,H
        case 0x5D: E = L; break;  // LD E,L
        case 0x5E: E = read8(mmu, getHL()); break;  // LD E,(HL)
        case 0x5F: E = A; break;  // LD E,A
        
        // LD H,r
        case 0x60: H = B; break;  // LD H,B
        case 0x61: H = C; break;  // LD H,C
        case 0x62: H = D; break;  // LD H,D
        case 0x63: H = E; break;  // LD H,E
        case 0x64: H = H; break;  // LD H,H
        case 0x65: H = L; break;  // LD H,L
        case 0x66: H = read8(mmu, getHL()); break;  // LD H,(HL)
        case 0x67: H = A; break;  // LD H,A
        
        // LD L,r
        case 0x68: L = B; break;  // LD L,B
        case 0x69: L = C; break;  // LD L,C
        case 0x6A: L = D; break;  // LD L,D
        case 0x6B: L = E; break;  // LD L,E
        case 0x6C: L = H; break;  // LD L,H
        case 0x6D: L = L; break;  // LD L,L
        case 0x6E: L = read8(mmu, getHL()); break;  // LD L,(HL)
        case 0x6F: L = A; break;  // LD L,A
        
        // LD (HL),r
        case 0x70: write8(mmu, getHL(), B); break;  // LD (HL),B
        case 0x71: write8(mmu, getHL(), C); break;  // LD (HL),C
        case 0x72: write8(mmu, getHL(), D); break;  // LD (HL),D
        case 0x73: write8(mmu, getHL(), E); break;  // LD (HL),E
        case 0x74: write8(mmu, getHL(), H); break;  // LD (HL),H
        case 0x75: write8(mmu, getHL(), L); break;  // LD (HL),L
        case 0x76: HALT(); break;  // HALT
        
        // LD A,r
        case 0x78: A = B; break;  // LD A,B (the one we kept hitting!)
        case 0x79: A = C; break;  // LD A,C
        case 0x7A: A = D; break;  // LD A,D
        case 0x7B: A = E; break;  // LD A,E
        case 0x7C: A = H; break;  // LD A,H
        case 0x7D: A = L; break;  // LD A,L
        case 0x7E: A = read8(mmu, getHL()); break;  // LD A,(HL)
        case 0x7F: A = A; break;  // LD A,A
        
        // ============================================
        // 0x80-0xBF: ALU OPERATIONS
        // ============================================
        // ADD A,r
        case 0x80: ADD_A_r(B); break;
        case 0x81: ADD_A_r(C); break;
        case 0x82: ADD_A_r(D); break;
        case 0x83: ADD_A_r(E); break;
        case 0x84: ADD_A_r(H); break;
        case 0x85: ADD_A_r(L); break;
        case 0x86: ADD_A_r(read8(mmu, getHL())); break;
        case 0x87: ADD_A_r(A); break;
        
        // ADC A,r
        case 0x88: ADC_A_r(B); break;
        case 0x89: ADC_A_r(C); break;
        case 0x8A: ADC_A_r(D); break;
        case 0x8B: ADC_A_r(E); break;
        case 0x8C: ADC_A_r(H); break;
        case 0x8D: ADC_A_r(L); break;
        case 0x8E: ADC_A_r(read8(mmu, getHL())); break;
        case 0x8F: ADC_A_r(A); break;
        
        // SUB r
        case 0x90: SUB_A_r(B); break;
        case 0x91: SUB_A_r(C); break;
        case 0x92: SUB_A_r(D); break;
        case 0x93: SUB_A_r(E); break;
        case 0x94: SUB_A_r(H); break;
        case 0x95: SUB_A_r(L); break;
        case 0x96: SUB_A_r(read8(mmu, getHL())); break;
        case 0x97: SUB_A_r(A); break;
        
        // SBC A,r
        case 0x98: SBC_A_r(B); break;
        case 0x99: SBC_A_r(C); break;
        case 0x9A: SBC_A_r(D); break;
        case 0x9B: SBC_A_r(E); break;
        case 0x9C: SBC_A_r(H); break;
        case 0x9D: SBC_A_r(L); break;
        case 0x9E: SBC_A_r(read8(mmu, getHL())); break;
        case 0x9F: SBC_A_r(A); break;
        
        // AND r
        case 0xA0: AND_A_r(B); break;
        case 0xA1: AND_A_r(C); break;
        case 0xA2: AND_A_r(D); break;
        case 0xA3: AND_A_r(E); break;
        case 0xA4: AND_A_r(H); break;
        case 0xA5: AND_A_r(L); break;
        case 0xA6: AND_A_r(read8(mmu, getHL())); break;
        case 0xA7: AND_A_r(A); break;
        
        // XOR r
        case 0xA8: XOR_A_r(B); break;
        case 0xA9: XOR_A_r(C); break;
        case 0xAA: XOR_A_r(D); break;
        case 0xAB: XOR_A_r(E); break;
        case 0xAC: XOR_A_r(H); break;
        case 0xAD: XOR_A_r(L); break;
        case 0xAE: XOR_A_r(read8(mmu, getHL())); break;
        
        // OR r
        case 0xB0: OR_A_r(B); break;
        case 0xB1: OR_A_r(C); break;  // This is the one we kept hitting!
        case 0xB2: OR_A_r(D); break;
        case 0xB3: OR_A_r(E); break;
        case 0xB4: OR_A_r(H); break;
        case 0xB5: OR_A_r(L); break;
        case 0xB6: OR_A_r(read8(mmu, getHL())); break;
        case 0xB7: OR_A_r(A); break;
        
        // CP r
        case 0xB8: CP_A_r(B); break;
        case 0xB9: CP_A_r(C); break;
        case 0xBA: CP_A_r(D); break;
        case 0xBB: CP_A_r(E); break;
        case 0xBC: CP_A_r(H); break;
        case 0xBD: CP_A_r(L); break;
        case 0xBE: CP_A_r(read8(mmu, getHL())); break;
        case 0xBF: CP_A_r(A); break;
        
        // ============================================
        // 0xC0-0xFF: CONTROL FLOW & STACK
        // ============================================
        
        // RET conditions
        case 0xC0: RET_cc(mmu, !getFlag(FLAG_Z)); break;  // RET NZ
        case 0xC8: RET_cc(mmu, getFlag(FLAG_Z)); break;   // RET Z
        case 0xD0: RET_cc(mmu, !getFlag(FLAG_C)); break;  // RET NC
        case 0xD8: RET_cc(mmu, getFlag(FLAG_C)); break;   // RET C
        
        // POP
        case 0xC1: setBC(pop16(mmu)); break;  // POP BC
        case 0xD1: setDE(pop16(mmu)); break;  // POP DE
        case 0xE1: setHL(pop16(mmu)); break;  // POP HL
        case 0xF1: setAF(pop16(mmu)); break;  // POP AF
        
        // JP conditions
        case 0xC2: {  // JP NZ,nn
            u16 addr = read16(mmu, PC);
            PC += 2;
            JP_cc_nn(!getFlag(FLAG_Z), addr);
            break;
        }
        case 0xCA: {  // JP Z,nn
            u16 addr = read16(mmu, PC);
            PC += 2;
            JP_cc_nn(getFlag(FLAG_Z), addr);
            break;
        }
        case 0xD2: {  // JP NC,nn
            u16 addr = read16(mmu, PC);
            PC += 2;
            JP_cc_nn(!getFlag(FLAG_C), addr);
            break;
        }
        case 0xDA: {  // JP C,nn
            u16 addr = read16(mmu, PC);
            PC += 2;
            JP_cc_nn(getFlag(FLAG_C), addr);
            break;
        }
        
        // 0xC3: JP nn (already implemented above)
        
        // CALL conditions
        case 0xC4: {  // CALL NZ,nn
            u16 addr = read16(mmu, PC);
            PC += 2;
            CALL_cc_nn(mmu, !getFlag(FLAG_Z), addr);
            break;
        }
        case 0xCC: {  // CALL Z,nn
            u16 addr = read16(mmu, PC);
            PC += 2;
            CALL_cc_nn(mmu, getFlag(FLAG_Z), addr);
            break;
        }
        case 0xD4: {  // CALL NC,nn
            u16 addr = read16(mmu, PC);
            PC += 2;
            CALL_cc_nn(mmu, !getFlag(FLAG_C), addr);
            break;
        }
        case 0xDC: {  // CALL C,nn
            u16 addr = read16(mmu, PC);
            PC += 2;
            CALL_cc_nn(mmu, getFlag(FLAG_C), addr);
            break;
        }
        
        // PUSH
        case 0xC5: push16(mmu, getBC()); break;  // PUSH BC
        case 0xD5: push16(mmu, getDE()); break;  // PUSH DE
        case 0xE5: push16(mmu, getHL()); break;  // PUSH HL
        case 0xF5: push16(mmu, getAF()); break;  // PUSH AF
        
        // ADD A,n
        case 0xC6: {
            u8 n = read8(mmu, PC++);
            ADD_A_r(n);
            break;
        }
        
        // RST (Reset/Call to fixed addresses)
        case 0xC7: RST(mmu, 0x00); break;
        
        // 0xC9: RET (unconditional - already implemented)
        
        // 0xCE: ADC A,n
        case 0xCE: {
            u8 n = read8(mmu, PC++);
            ADC_A_r(n);
            break;
        }
        
        case 0xCF: RST(mmu, 0x08); break;
        
        // 0xD6: SUB A,n
        case 0xD6: {
            u8 n = read8(mmu, PC++);
            SUB_A_r(n);
            break;
        }
        
        case 0xD7: RST(mmu, 0x10); break;

        case 0xDF: RST(mmu, 0x18); break;
        case 0xE7: RST(mmu, 0x20); break;
        case 0xEF: RST(mmu, 0x28); break;
        case 0xF7: RST(mmu, 0x30); break;
        case 0xFF: RST(mmu, 0x38); break;
        
        // 0xC9: RET (unconditional - already implemented)
        
        // 0xD9: RETI
        case 0xD9:
            RETI(mmu);
            break;
        
        // 0xDE: SBC A,n
        case 0xDE: {
            u8 n = read8(mmu, PC++);
            SBC_A_r(n);
            break;
        }
        
        // 0xE0: LDH (n),A (already implemented)
        // 0xE2: LD (C),A
        case 0xE2:
            write8(mmu, 0xFF00 + C, A);
            break;
        
        // 0xE6: AND n
        case 0xE6: {
            u8 n = read8(mmu, PC++);
            AND_A_r(n);
            break;
        }
        
        // 0xE8: ADD SP,e
        case 0xE8: {
            i8 offset = static_cast<i8>(read8(mmu, PC++));
            u16 result = SP + offset;
            
            setFlag(FLAG_Z, false);
            setFlag(FLAG_N, false);
            setFlag(FLAG_H, ((SP & 0x0F) + (offset & 0x0F)) > 0x0F);
            setFlag(FLAG_C, ((SP & 0xFF) + (offset & 0xFF)) > 0xFF);
            
            SP = result;
            cycles += 8;
            break;
        }
        
        // 0xE9: JP (HL)
        case 0xE9:
            PC = getHL();
            break;
        
        // 0xEA: LD (nn),A
        case 0xEA: {
            u16 addr = read16(mmu, PC);
            PC += 2;
            write8(mmu, addr, A);
            break;
        }
        
        // 0xEE: XOR n
        case 0xEE: {
            u8 n = read8(mmu, PC++);
            XOR_A_r(n);
            break;
        }
        
        // 0xF0: LDH A,(n) (already implemented)
        // 0xF2: LD A,(C)
        case 0xF2:
            A = read8(mmu, 0xFF00 + C);
            break;
        
        // Illegal/Unused Opcodes - Treat as NOP
        case 0xD3:
        case 0xDB:
        case 0xDD:
        case 0xE3:
        case 0xE4:
        case 0xEB:
        case 0xEC:
        case 0xED:
        case 0xF4:
        case 0xFC:
        case 0xFD:
            // Illegal opcodes act as NOP on real hardware
            break;
        
        // 0xF6: OR n
        case 0xF6: {
            u8 n = read8(mmu, PC++);
            OR_A_r(n);
            break;
        }
        
        // 0xF8: LD HL,SP+e
        case 0xF8: {
            i8 offset = static_cast<i8>(read8(mmu, PC++));
            u16 result = SP + offset;
            
            setFlag(FLAG_Z, false);
            setFlag(FLAG_N, false);
            setFlag(FLAG_H, ((SP & 0x0F) + (offset & 0x0F)) > 0x0F);
            setFlag(FLAG_C, ((SP & 0xFF) + (offset & 0xFF)) > 0xFF);
            
            setHL(result);
            cycles += 4;
            break;
        }
        
        // 0xF9: LD SP,HL
        case 0xF9:
            SP = getHL();
            cycles += 4;
            break;
        
        // 0xFA: LD A,(nn)
        case 0xFA: {
            u16 addr = read16(mmu, PC);
            PC += 2;
            A = read8(mmu, addr);
            break;
        }
        
        // 0xFB: EI (already implemented)
        // 0xFE: CP n (already implemented)
        
        // 0x3E: LD A,n
        case 0x3E:
            A = read8(mmu, PC++);
            break;
            
        // 0x77: LD (HL),A
        case 0x77:
            write8(mmu, getHL(), A);
            break;
            
        // 0xAF: XOR A
        case 0xAF:
            XOR_A_r(A);  // XOR A,A always sets A to 0 and Z flag
            break;
            
        // 0xC3: JP nn
        case 0xC3: {
            u16 addr = read16(mmu, PC);
            PC = addr;
            cycles += 4;
            break;
        }
        
        // 0xC9: RET
        case 0xC9:
            RET(mmu);
            cycles += 4;
            break;
            
        // 0xCD: CALL nn
        case 0xCD: {
            u16 addr = read16(mmu, PC);
            PC += 2;
            CALL_nn(mmu, addr);
            break;
        }
        
        // 0xCB: CB prefix (extended instruction set)
        case 0xCB: {
            u8 cb_opcode = read8(mmu, PC++);
            executeCBOpcode(cb_opcode, mmu);
            break;
        }
        
        // 0xE0: LDH (n),A - Load A into (0xFF00+n)
        case 0xE0: {
            u8 offset = read8(mmu, PC++);
            write8(mmu, 0xFF00 + offset, A);
            break;
        }
        
        // 0xF0: LDH A,(n) - Load (0xFF00+n) into A
        case 0xF0: {
            u8 offset = read8(mmu, PC++);
            A = read8(mmu, 0xFF00 + offset);
            break;
        }
        
        // 0xF3: DI - Disable interrupts
        case 0xF3:
            DI();
            break;
            
        // 0xFB: EI - Enable interrupts
        case 0xFB:
            EI();
            break;
            
        // 0xFE: CP n
        case 0xFE: {
            u8 n = read8(mmu, PC++);
            CP_A_r(n);
            break;
        }
        
        // ============================================
        // Unknown Opcodes (should not reach here with full implementation)
        // ============================================
        // For now, treat unknown opcodes as NOP with warning
        default:
            std::cerr << "WARNING: Unimplemented opcode 0x" 
                      << std::hex << std::setw(2) << std::setfill('0') << (int)opcode 
                      << " at PC 0x" << std::setw(4) << (PC - 1) << std::dec << std::endl;
            break;
    }
}

void CPU::executeCBOpcode(u8 opcode, MMU& mmu) {
    // CB-prefixed instructions (bit operations)
    // These are organized as:
    // 0x00-0x3F: Rotates and shifts
    // 0x40-0x7F: BIT (test bit)
    // 0x80-0xBF: RES (reset bit)
    // 0xC0-0xFF: SET (set bit)
    
    // Helper lambda to get register reference
    auto getReg = [&](int reg) -> u8& {
        switch (reg) {
            case 0: return B;
            case 1: return C;
            case 2: return D;
            case 3: return E;
            case 4: return H;
            case 5: return L;
            case 7: return A;
            default: {
                static u8 temp = 0;
                return temp; // Shouldn't happen
            }
        }
    };
    
    // Rotate/Shift operations (0x00-0x3F)
    if (opcode < 0x40) {
        int operation = (opcode >> 3) & 0x07;
        int reg = opcode & 0x07;
        
        if (reg == 6) {  // (HL)
            u8 val = read8(mmu, getHL());
            switch (operation) {
                case 0: RLC_r(val); break;  // RLC
                case 1: RRC_r(val); break;  // RRC
                case 2: RL_r(val); break;   // RL
                case 3: RR_r(val); break;   // RR
                case 4: SLA_r(val); break;  // SLA
                case 5: SRA_r(val); break;  // SRA
                case 6: SWAP_r(val); break; // SWAP
                case 7: SRL_r(val); break;  // SRL
            }
            write8(mmu, getHL(), val);
            cycles += 8;  // (HL) operations take 16 total: 8 from read/write + 8 extra
        } else {
            u8& r = getReg(reg);
            switch (operation) {
                case 0: RLC_r(r); break;
                case 1: RRC_r(r); break;
                case 2: RL_r(r); break;
                case 3: RR_r(r); break;
                case 4: SLA_r(r); break;
                case 5: SRA_r(r); break;
                case 6: SWAP_r(r); break;
                case 7: SRL_r(r); break;
            }
        }
        return;
    }
    
    // BIT operations (0x40-0x7F)
    if (opcode >= 0x40 && opcode <= 0x7F) {
        // BIT b,r - Test bit b in register r
        u8 bit = static_cast<u8>((opcode >> 3) & 0x07);  // Extract bit number (0-7)
        int reg = opcode & 0x07;          // Extract register (0-7)
        
        u8 value = 0;
        switch (reg) {
            case 0: value = B; break;
            case 1: value = C; break;
            case 2: value = D; break;
            case 3: value = E; break;
            case 4: value = H; break;
            case 5: value = L; break;
            case 6: 
                value = read8(mmu, getHL()); 
                cycles += 4;  // BIT (HL) takes 12 total: 8 from read + 4 extra
                break;
            case 7: value = A; break;
        }
        
        BIT_b_r(bit, value);
        return;
    }
    
    // RES operations (0x80-0xBF)
    if (opcode >= 0x80 && opcode <= 0xBF) {
        u8 bit = static_cast<u8>((opcode >> 3) & 0x07);
        int reg = opcode & 0x07;
        
        if (reg == 6) {
            u8 val = read8(mmu, getHL());
            RES_b_r(bit, val);
            write8(mmu, getHL(), val);
            cycles += 8;  // RES (HL) takes 16 total
        } else {
            RES_b_r(bit, getReg(reg));
        }
        return;
    }
    
    // SET operations (0xC0-0xFF)
    if (opcode >= 0xC0) {
        u8 bit = static_cast<u8>((opcode >> 3) & 0x07);
        int reg = opcode & 0x07;
        
        if (reg == 6) {
            u8 val = read8(mmu, getHL());
            SET_b_r(bit, val);
            write8(mmu, getHL(), val);
            cycles += 8;  // SET (HL) takes 16 total
        } else {
            SET_b_r(bit, getReg(reg));
        }
        return;
    }
    
    // BIT operations (0x40-0x7F)
    if (opcode >= 0x40 && opcode <= 0x7F) {
        // BIT b,r - Test bit b in register r
        u8 bit = static_cast<u8>((opcode >> 3) & 0x07);  // Extract bit number (0-7)
        int reg = opcode & 0x07;          // Extract register (0-7)
        
        u8 value = 0;
        switch (reg) {
            case 0: value = B; break;
            case 1: value = C; break;
            case 2: value = D; break;
            case 3: value = E; break;
            case 4: value = H; break;
            case 5: value = L; break;
            case 6: value = read8(mmu, getHL()); break;
            case 7: value = A; break;
        }
        
        BIT_b_r(bit, value);
        return;
    }
    
    // RES operations (0x80-0xBF)
    if (opcode >= 0x80 && opcode <= 0xBF) {
        u8 bit = static_cast<u8>((opcode >> 3) & 0x07);
        int reg = opcode & 0x07;
        
        if (reg == 6) {
            u8 val = read8(mmu, getHL());
            RES_b_r(bit, val);
            write8(mmu, getHL(), val);
        } else {
            RES_b_r(bit, getReg(reg));
        }
        return;
    }
    
    // SET operations (0xC0-0xFF)
    if (opcode >= 0xC0) {
        u8 bit = static_cast<u8>((opcode >> 3) & 0x07);
        int reg = opcode & 0x07;
        
        if (reg == 6) {
            u8 val = read8(mmu, getHL());
            SET_b_r(bit, val);
            write8(mmu, getHL(), val);
        } else {
            SET_b_r(bit, getReg(reg));
        }
        return;
    }

    std::cerr << "WARNING: Unimplemented CB opcode 0xCB 0x" 
              << std::hex << std::setw(2) << std::setfill('0') << (int)opcode 
              << std::dec << std::endl;
}

// ============================================
// ROTATE AND SHIFT HELPERS
// ============================================

void CPU::RLCA() {
    bool carry = (A & 0x80) != 0;
    A = (A << 1) | (carry ? 1 : 0);
    
    setFlag(FLAG_Z, false);  // RLCA always clears Z
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::RLC_r(u8& reg) {
    bool carry = (reg & 0x80) != 0;
    reg = (reg << 1) | (carry ? 1 : 0);
    
    setFlag(FLAG_Z, reg == 0);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

// ============================================
// BIT OPERATIONS
// ============================================

void CPU::BIT_b_r(u8 bit, u8 reg) {
    bool bit_set = (reg & (1 << bit)) != 0;
    
    setFlag(FLAG_Z, !bit_set);  // Z set if bit is 0
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, true);       // H always set for BIT
    // C unchanged
}

void CPU::SET_b_r(u8 bit, u8& reg) {
    reg |= (1 << bit);
}

void CPU::RES_b_r(u8 bit, u8& reg) {
    reg &= ~(1 << bit);
}

// ============================================
// MISCELLANEOUS
// ============================================

void CPU::DAA() {
    // Decimal Adjust Accumulator (for BCD arithmetic)
    // CRITICAL: Must handle addition and subtraction cases differently!
    u8 correction = 0;
    bool carry = getFlag(FLAG_C);
    
    if (!getFlag(FLAG_N)) {
        // After ADDITION
        if (getFlag(FLAG_H) || (A & 0x0F) > 9) {
            correction |= 0x06;
        }
        if (carry || A > 0x99) {
            correction |= 0x60;
            carry = true;
        }
        A += correction;
    } else {
        // After SUBTRACTION  
        if (getFlag(FLAG_H)) {
            correction |= 0x06;
        }
        if (carry) {
            correction |= 0x60;
        }
        A -= correction;
    }
    
    setFlag(FLAG_Z, A == 0);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

void CPU::CPL() {
    A = ~A;  // Complement (invert all bits)
    setFlag(FLAG_N, true);
    setFlag(FLAG_H, true);
}

void CPU::CCF() {
    // Complement Carry Flag
    setFlag(FLAG_C, !getFlag(FLAG_C));
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
}

void CPU::SCF() {
    // Set Carry Flag
    setFlag(FLAG_C, true);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, false);
}
