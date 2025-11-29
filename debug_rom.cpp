#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>

int main() {
    std::ifstream file("roms/Super Mario Land 2 - 6 Golden Coins (USA, Europe) (Rev 2).gb", std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open ROM" << std::endl;
        return 1;
    }
    
    // Read ROM
    std::vector<unsigned char> rom((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    
    // Check bytes at 0x1FB
    std::cout << "Bytes at 0x1FB:" << std::endl;
    for (int i = 0; i < 10; i++) {
        int addr = 0x1FB + i;
        std::cout << "0x" << std::hex << std::setw(4) << std::setfill('0') << addr 
                  << ": 0x" << std::setw(2) << (int)rom[addr] << std::endl;
    }
    
    return 0;
}
