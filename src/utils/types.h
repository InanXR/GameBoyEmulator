#pragma once
#include <cstdint>

// ============================================
// TYPE DEFINITIONS
// ============================================
// Common type definitions for clarity and portability
// Using fixed-width integer types for predictable behavior

// Unsigned types
using u8  = uint8_t;   // 8-bit unsigned (0-255)
using u16 = uint16_t;  // 16-bit unsigned (0-65535)
using u32 = uint32_t;  // 32-bit unsigned
using u64 = uint64_t;  // 64-bit unsigned

// Signed types
using i8  = int8_t;    // 8-bit signed (-128 to 127)
using i16 = int16_t;   // 16-bit signed
using i32 = int32_t;   // 32-bit signed
using i64 = int64_t;   // 64-bit signed


// ============================================
// BIT MANIPULATION MACROS
// ============================================
// Essential macros for working with bits and flags

// Get a bit mask for position n (0-7)
#define BIT(n) (1 << (n))

// Set bit n in a byte to 1
#define SET_BIT(byte, bit) ((byte) |= BIT(bit))

// Clear bit n in a byte to 0
#define CLEAR_BIT(byte, bit) ((byte) &= ~BIT(bit))

// Test if bit n is set (returns true/false)
#define TEST_BIT(byte, bit) (((byte) & BIT(bit)) != 0)

// Toggle bit n (flip between 0 and 1)
#define TOGGLE_BIT(byte, bit) ((byte) ^= BIT(bit))

// Get specific bits from a value
#define GET_BITS(value, mask) ((value) & (mask))

// Check if a value is between two numbers (inclusive)
#define IN_RANGE(val, min, max) ((val) >= (min) && (val) <= (max))
