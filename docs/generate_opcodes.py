"""
GameBoy Opcode Generator
Reads Opcodes.json and generates complete instructions.cpp
"""
import json

def main():
    # Read the opcode JSON
    with open('Opcodes.json', 'r') as f:
        opcodes_data = json.load(f)
    
    unprefixed = opcodes_data['unprefixed']
    cbprefixed = opcodes_data['cbprefixed']
    
    # Generate all 256 main opcodes
    print("Generating all 256 main opcodes...")
    
    opcode_count = 0
    
    for hex_str, opcode_info in sorted(unprefixed.items()):
        opcode = int(hex_str, 16)
        mnemonic = opcode_info['mnemonic']
        operands = opcode_info.get('operands', [])
        
        # Build operand names
        operand_names = [op['name'] for op in operands]
        operand_str = ','.join(operand_names)
        
        print(f"// 0x{opcode:02X}: {mnemonic} {operand_str}")
        print(f"case 0x{opcode:02X}:")
        
        # Generate implementation based on mnemonic and operands
        impl = generate_implementation(mnemonic, operand_names, operands)
        print(f"    {impl}")
        print(f"    break;\n")
        
        opcode_count += 1
    
    print(f"\n✅ Generated {opcode_count} opcodes!")
    print(f"💡 Now copy-paste these into instructions.cpp")

def generate_implementation(mnemonic, operands, operand_details):
    """Generate C++ code for each opcode"""
    
    # Map of mnemonics to simple implementations
    if mnemonic == "NOP":
        return "NOP();"
    
    elif mnemonic == "HALT":
        return "HALT();"
    
    elif mnemonic == "STOP":
        return "STOP(); PC++;"  # STOP has 2-byte encoding
    
    elif mnemonic == "DI":
        return "DI();"
    
    elif mnemonic == "EI":
        return "EI();"
    
    elif mnemonic == "LD" and len(operands) == 2:
        dest, src = operands
        # Handle different LD variants
        if src == "n8":
            return f"{dest} = read8(mmu, PC++);"
        elif src == "n16":
            return f"set{dest}(read16(mmu, PC)); PC += 2;"
        elif src in ['A', 'B', 'C', 'D', 'E', 'H', 'L']:
            return f"{dest} = {src};"
        elif dest in ['BC', 'DE', 'HL'] and not any(op.get('immediate') == False for op in operand_details if op['name'] == dest):
            return f"{dest} = read8(mmu, get{src}());" if src in ['BC', 'DE', 'HL'] else f"set{dest}({src});"
        else:
            return f"// TODO: LD {dest},{src}"
   
    elif mnemonic == "INC":
        reg = operands[0]
        if reg in ['A', 'B', 'C', 'D', 'E', 'H', 'L']:
            return f"INC_r({reg});"
        elif reg in ['BC', 'DE', 'HL', 'SP']:
            return f"set{reg}(get{reg}() + 1); cycles += 4;" if reg != 'SP' else "SP++; cycles += 4;"
        return f"// TODO: INC {reg}"
    
    elif mnemonic == "DEC":
        reg = operands[0]
        if reg in ['A', 'B', 'C', 'D', 'E', 'H', 'L']:
            return f"DEC_r({reg});"
        elif reg in ['BC', 'DE', 'HL', 'SP']:
            return f"set{reg}(get{reg}() - 1); cycles += 4;" if reg != 'SP' else "SP--; cycles += 4;"
        return f"// TODO: DEC {reg}"
    
    else:
        # For complex cases, just add TODO
        op_str = ','.join(operands)
        return f"// TODO: {mnemonic} {op_str}"

if __name__ == "__main__":
    main()
