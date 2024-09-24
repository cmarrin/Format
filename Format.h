/*-------------------------------------------------------------------------
    This source file is a part of Clover
    For the latest info, see https://github.com/cmarrin/Clover
    Copyright (c) 2021-2024, Chris Marrin
    All rights reserved.
    Use of this source code is governed by the MIT license that can be
    found in the LICENSE file.
-------------------------------------------------------------------------*/

// Print - formatted print and create string

#pragma once

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef ARDUINO
    #include <Arduino.h>
    #include <EEPROM.h>
    
    static inline void putChar(uint8_t c) { Serial.print(char(c)); }
    static inline void toString(char* s, float val, int8_t width = 0, uint8_t precision = 0)
    {
        dtostrf(val, width, precision, s);
    }
#else
    static inline void putChar(uint8_t c) { ::putchar(c); }
    static inline void toString(char* s, float val, int8_t width = 0, uint8_t precision = 0)
    {
        snprintf(s, 20, "%*.*g", width, precision, val);
    }
#endif

static inline uint32_t floatToInt(float f) { return *(reinterpret_cast<int32_t*>(&f)); }
static inline float intToFloat(uint32_t i) { return *(reinterpret_cast<float*>(&i)); }

namespace fmt {

enum class Type { i8, i16, i32, flt, str, ptr };

class FormatterArgs
{
  public:
    virtual ~FormatterArgs() { }
    
    virtual uint8_t getChar(uint32_t i) const = 0;
    virtual void putChar(uint8_t c) = 0;
    virtual uint8_t getStringChar(uintptr_t p) const = 0;
    virtual uintptr_t getArg(Type type) = 0;
    virtual void end() { }
};

class NativePrintArgs : public FormatterArgs
{
  public:
    NativePrintArgs(const char* fmt, va_list args)
        : _fmt(fmt)
    {
        va_copy(_args, args);
    }
        
    virtual ~NativePrintArgs() { }
    virtual uint8_t getChar(uint32_t i) const override { return _fmt[i]; }
    virtual void putChar(uint8_t c) override { ::putChar(c); }
    virtual uintptr_t getArg(Type type) override
    {
        if (type == Type::flt) {
            double d = va_arg(_args, double);
            return floatToInt(float(d));
        }
        if (type == Type::str) {
            return uintptr_t(va_arg(_args, const char*));
        }
        return va_arg(_args, uint32_t);
    }

    // For native, p is the actual pointer to the char
    virtual uint8_t getStringChar(uintptr_t p) const override
    {
        const char* addr = reinterpret_cast<const char*>(p);
        return *addr;
    }

  private:
    const char* _fmt;
    va_list _args;
};

class NativeFormatArgs : public NativePrintArgs
{
  public:
    NativeFormatArgs(char* buf, uint16_t n, const char* fmt, va_list args)
        : NativePrintArgs(fmt, args)
        , _buf(buf)
        , _size(n)
        , _index(0)
    { }
    
    virtual ~NativeFormatArgs() { }

    virtual void putChar(uint8_t c) override
    {
        if (_index < _size - 1) {
            _buf[_index++] = c;
        }
    }

    virtual void end() override { putChar('\0'); }

  private:
    char* _buf;
    uint16_t _size;
    uint16_t _index;
};

int32_t doprintf(FormatterArgs*);

int32_t printf(const char* fmt, ...);
int32_t format(char* s, uint16_t n, const char* fmt, ...);
int32_t vprintf(const char* fmt, va_list args);
int32_t vformat(char* s, uint16_t n, const char* fmt, va_list args);

}
