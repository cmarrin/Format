/*-------------------------------------------------------------------------
    This source file is a part of Clover

    For the latest info, see http:www.marrin.org/Clover

    Copyright (c) 2018-2024, Chris Marrin
    All rights reserved.

    Use of this source code is governed by the MIT license that can be
    found in the LICENSE file.
-------------------------------------------------------------------------*/

#include "Format.h"

#include <assert.h>
#include <string.h>
#include <stdarg.h>

using namespace fmt;

enum class Flag {
    leftJustify = 0x01,
    plus = 0x02,
    space = 0x04,
    alt = 0x08,
    zeroPad = 0x10,
};

static bool isFlag(uint8_t flags, Flag flag) { return (flags & static_cast<uint8_t>(flag)) != 0; }
static void setFlag(uint8_t& flags, Flag flag) { flags |= static_cast<uint8_t>(flag); }

enum class Capital { Yes, No };

static constexpr uint32_t MaxIntegerBufferSize = 24; // Big enough for a 64 bit integer in octal

int32_t
fmt::printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    return fmt::vprintf(fmt, args);
}

int32_t
fmt::format(char* s, uint16_t n, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    return fmt::vformat(s, n, fmt, args);
}

int32_t
fmt::vprintf(const char* fmt, va_list args)
{
    NativePrintArgs f(fmt, args);
    return doprintf(&f);
}

int32_t
fmt::vformat(char* s, uint16_t n, const char* fmt, va_list args)
{
    NativeFormatArgs f(s, n, fmt, args);
    return doprintf(&f);
}

bool
toNumber(FormatterArgs* f, uint32_t& fmt, uint32_t& n)
{
    n = 0;
    bool haveNumber = false;
    while (1) {
        uint8_t c = f->getChar(fmt);
        if (c < '0' || c > '9') {
            return haveNumber;
        }
        n = n * 10 + c - '0';
        fmt += 1;
        haveNumber = true;
    }
}

enum class Signed { Yes, No };
enum class FloatType { Float, Exp, Shortest };
 
static void handleFlags(FormatterArgs* f, uint32_t& fmt, uint8_t& flags)
{
    while (1) {
        switch (f->getChar(fmt)) {
        case '-': setFlag(flags, Flag::leftJustify); break;
        case '+': setFlag(flags, Flag::plus); break;
        case ' ': setFlag(flags, Flag::space); break;
        case '#': setFlag(flags, Flag::alt); break;
        case '0': setFlag(flags, Flag::zeroPad); break;
        default: return;
        }
        ++fmt;
    }
}

static int32_t handleWidth(FormatterArgs* f, uint32_t& fmt)
{
    if (f->getChar(fmt) == '*') {
        ++fmt;
        return int32_t(f->getArg(Type::i16));
    }
    
    uint32_t n;
    return toNumber(f, fmt, n) ? static_cast<int32_t>(n) : -1;
}

enum class Length { None, H, HH, L, LL, J, Z, T };

static Length handleLength(FormatterArgs* f, uint32_t& fmt)
{
    Length length = Length::None;
    if (f->getChar(fmt) == 'h') {
        ++fmt;
        if (f->getChar(fmt) == 'h') {
            ++fmt;
            length = Length::HH;
        } else {
            length = Length::H;
        }
    } else if (f->getChar(fmt) == 'l') {
        ++fmt;
        if (f->getChar(fmt) == 'l') {
            ++fmt;
            length = Length::LL;
        } else {
            length = Length::L;
        }
    } else if (f->getChar(fmt) == 'j') {
        length = Length::J;
    } else if (f->getChar(fmt) == 'z') {
        length = Length::Z;
    } else if (f->getChar(fmt) == 't') {
        length = Length::T;
    } else {
        return length;
    }
    return length;
}

// 8 and 16 bit integers are upcast by the caller to 32 bit. Ignore the length field
static int32_t getInteger(Length length, FormatterArgs* f)
{
    return int32_t(f->getArg(Type::i16));
}

static char* intToString(uint64_t value, char* buf, size_t size, uint8_t base = 10, Capital cap = Capital::No)
{
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    
    char hexBase = (cap == Capital::Yes) ? 'A' : 'a';
    char* p = buf + size;
    *--p = '\0';
    
    while (value) {
        uint8_t digit = value % base;
        *--p = (digit > 9) ? (digit - 10 + hexBase) : (digit + '0');
        value /= base;
    }
    return p;
}

static int32_t outInteger(FormatterArgs* f, uintmax_t value, Signed sign, int32_t width, int32_t precision, uint8_t flags, uint8_t base, Capital cap)
{
    uint32_t size = 0;
    if (sign == Signed::Yes) {
        intmax_t signedValue = value;
        if (signedValue < 0) {
            value = -signedValue;
            f->putChar('-');
            size = 1;
            width--;
        }
    }
    
    if (isFlag(flags, Flag::alt) && base != 10) {
        f->putChar('0');
        size++;
        width--;
        if (base == 16) {
            f->putChar((cap == Capital::Yes) ? 'X' : 'x');
            size++;
            width--;
        }
    }
    
    char buf[MaxIntegerBufferSize];
    char* p = intToString(static_cast<uint64_t>(value), buf, MaxIntegerBufferSize, base, cap);
    size += static_cast<uint32_t>(p - buf);

    int32_t pad = static_cast<int32_t>(width) - static_cast<int32_t>(strlen(p));
    
    char padChar = isFlag(flags, Flag::zeroPad) ? '0' : ' ';
    
    while (pad > 0) {
        f->putChar(padChar);
        size++;
        pad--;
    }
    
    for ( ; *p; ++p) {
        f->putChar(*p);
    }

    return size;
}

static int32_t outString(FormatterArgs* f, uintptr_t p, int32_t width, int32_t precision, uint8_t flags)
{
    // FIXME: Handle flags.leftJustify
    // FIXME: Handle width
    // FIXME: Handle precision
    
    int32_t size = 0;
    while (true) {
        uint8_t c = f->getStringChar(p++);
        if (c == '\0') {
            break;
        }
        f->putChar(c);
        ++size;
    }
    
    if (width > size) {
        width -= size;
        while (width--) {
            f->putChar(' ');
        }
    }

    return size;
}

// Unsupported features:
//
//     'n' specifier - returns number of characters written so far
//     'a', 'A' specifiers - prints hex floats
//     'L' length - long double
//     'l' length for 'c' and 's' specifiers - wide characters
//
// 
 
int32_t
fmt::doprintf(FormatterArgs* f)
{
    uint8_t flags = 0;
    int32_t size = 0;
    uint32_t fmt = 0;
    uint8_t c;
    
    while ((c = f->getChar(fmt)) != '\0') {
        if (c != '%') {
            f->putChar(c);
            fmt++;
            size++;
            continue;
        }
        
        fmt++;
        
        // We have a format, do the optional part
        handleFlags(f, fmt, flags);
        int32_t width = handleWidth(f, fmt);
        int32_t precision = -1;
        if (f->getChar(fmt) == '.') {
            precision = handleWidth(f, ++fmt);
        }
        Length length = handleLength(f, fmt);
        
        // Handle the specifier
        switch (f->getChar(fmt))
        {
        case 'd':
        case 'i':
            size += outInteger(f, getInteger(length, f), Signed::Yes, width, precision, flags, 10, Capital::No);
            break;
        case 'u':
            size += outInteger(f, getInteger(length, f), Signed::No, width, precision, flags, 10, Capital::No);
            break;
        case 'o':
            size += outInteger(f, getInteger(length, f), Signed::No, width, precision, flags, 8, Capital::No);
            break;
        case 'x':
        case 'X':
            size += outInteger(f, getInteger(length, f), Signed::No, width, precision, flags, 16, (f->getChar(fmt) == 'X') ? Capital::Yes : Capital::No);
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G': {
            Capital cap = Capital::No;
            FloatType type = FloatType::Shortest;
            switch(f->getChar(fmt))
            {
            case 'f': cap = Capital::No; type = FloatType::Float; break;
            case 'F': cap = Capital::Yes; type = FloatType::Float; break;
            case 'e': cap = Capital::No; type = FloatType::Exp; break;
            case 'E': cap = Capital::Yes; type = FloatType::Exp; break;
            case 'g': cap = Capital::No; type = FloatType::Shortest; break;
            case 'G': cap = Capital::Yes; type = FloatType::Shortest; break;
            }

            char buf[20];
            toString(buf, intToFloat(int32_t(f->getArg(Type::flt))), width, (precision < 0) ? 6 : precision);
            for (int i = 0; buf[i] != '\0'; ++i) {
                f->putChar(buf[i]);
            }
            //size += outFloat(gen, flt::Float::fromArg(va_arg(va.value, flt::Float::arg_type)), width, precision, flags, cap, type);
            break;
        }
        case 'c':
            // Chars are passed in as uint32
            f->putChar(static_cast<char>(f->getArg(Type::i8)));
            size++;
            break;
        case 'b': {
            // Booleans are passed in as ArgType
            const char* s = f->getArg(Type::i8) ? "true" : "false";
            for (int i = 0; s[i] != '\0'; ++i) {
                f->putChar(s[i]);
            }
            break;
        }
        case 's':
            size += outString(f, f->getArg(Type::str), width, precision, flags);
            break;
        case 'p':
            size += outInteger(f, f->getArg(Type::ptr), Signed::No, width, precision, flags, 16, Capital::No);
            break;
        default:
            f->putChar(f->getChar(fmt++));
            size++;
            break;
        }
        ++fmt;
    }
    
    f->end();
    return size;
}
