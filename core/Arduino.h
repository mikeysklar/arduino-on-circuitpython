// SPDX-FileCopyrightText: 2026 Mikey Sklar for Adafruit Industries
// SPDX-License-Identifier: MIT
//
// Arduino.h: a small Arduino-compatible API for sketches compiled into a
// CircuitPython native module (.mpy). Pins, time and Serial are implemented on
// top of board, digitalio, analogio, pwmio and time (see ard_mp.c).
//
// The sketch only runs while CircuitPython calls setup() and loop(). There are
// no interrupts, hardware timers or background tasks of its own.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t byte;
typedef bool boolean;
typedef uint16_t word;

#define HIGH 0x1
#define LOW  0x0

#define INPUT          0x0
#define OUTPUT         0x1
#define INPUT_PULLUP   0x2
#define INPUT_PULLDOWN 0x3

#define LSBFIRST 0
#define MSBFIRST 1

#define CHANGE  1
#define FALLING 2
#define RISING  3

// Pin numbers below 0x100 map to board.D<n>. LED_BUILTIN maps to board.LED,
// A<n> to board.A<n>.
#define LED_BUILTIN 0x100
#define A0 0x200
#define A1 0x201
#define A2 0x202
#define A3 0x203
#define A4 0x204
#define A5 0x205
#define A6 0x206
#define A7 0x207

#define PI         3.1415926535897932384626433832795
#define HALF_PI    1.5707963267948966192313216916398
#define TWO_PI     6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define EULER      2.718281828459045235360287471352

#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

#define lowByte(w)  ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))
#define bit(b) (1UL << (b))
#define bitRead(value, b) (((value) >> (b)) & 0x01)
#define bitSet(value, b) ((value) |= (1UL << (b)))
#define bitClear(value, b) ((value) &= ~(1UL << (b)))
#define bitWrite(value, b, bitvalue) ((bitvalue) ? bitSet(value, b) : bitClear(value, b))

// No flash/RAM split to manage here.
#define PROGMEM
#define F(s) (s)
#define PSTR(s) (s)
#define pgm_read_byte(addr)  (*(const uint8_t *)(addr))
#define pgm_read_word(addr)  (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_float(addr) (*(const float *)(addr))
#define pgm_read_ptr(addr)   (*(void *const *)(addr))

#define digitalPinToInterrupt(p) (p)

void pinMode(unsigned int pin, unsigned int mode);
void digitalWrite(unsigned int pin, unsigned int val);
int digitalRead(unsigned int pin);
int analogRead(unsigned int pin);
void analogWrite(unsigned int pin, int val);
void analogReadResolution(int bits);
void analogWriteResolution(int bits);
void analogWriteFreq(uint32_t freq);

unsigned long millis(void);
unsigned long micros(void);
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield(void);

// The sketch never runs concurrently with anything, so these have nothing to do.
static inline void interrupts(void) {
}
static inline void noInterrupts(void) {
}

// Not available: report it at compile time rather than misbehave at run time.
#define ARD_UNSUPPORTED(what) __attribute__((error(what " is not supported when running under CircuitPython")))
void attachInterrupt(unsigned int pin, void (*fn)(void), int mode) ARD_UNSUPPORTED("attachInterrupt()");
void detachInterrupt(unsigned int pin) ARD_UNSUPPORTED("detachInterrupt()");
void tone(unsigned int pin, unsigned int frequency, unsigned long duration) ARD_UNSUPPORTED("tone()");
void noTone(unsigned int pin) ARD_UNSUPPORTED("noTone()");
unsigned long pulseIn(unsigned int pin, unsigned int state, unsigned long timeout) ARD_UNSUPPORTED("pulseIn()");

#ifdef __cplusplus
} // extern "C"

template <class T, class L>
static inline auto min(const T &a, const L &b) -> decltype((b < a) ? b : a) {
    return (b < a) ? b : a;
}
template <class T, class L>
static inline auto max(const T &a, const L &b) -> decltype((b < a) ? b : a) {
    return (a < b) ? b : a;
}
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

long map(long x, long in_min, long in_max, long out_min, long out_max);
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);

void shiftOut(unsigned int dataPin, unsigned int clockPin, uint8_t bitOrder, uint8_t val);
uint8_t shiftIn(unsigned int dataPin, unsigned int clockPin, uint8_t bitOrder);

void tone(unsigned int pin, unsigned int frequency) ARD_UNSUPPORTED("tone()");
unsigned long pulseIn(unsigned int pin, unsigned int state) ARD_UNSUPPORTED("pulseIn()");

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

// Subset of Arduino's Print class.
class Print {
public:
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size);
    size_t write(const char *str) {
        return str ? write((const uint8_t *)str, strlen(str)) : 0;
    }
    size_t write(const char *buffer, size_t size) {
        return write((const uint8_t *)buffer, size);
    }

    size_t print(const char str[]) {
        return write(str);
    }
    size_t print(char c) {
        return write((uint8_t)c);
    }
    size_t print(unsigned char n, int base = DEC) {
        return print((unsigned long)n, base);
    }
    size_t print(int n, int base = DEC) {
        return print((long)n, base);
    }
    size_t print(unsigned int n, int base = DEC) {
        return print((unsigned long)n, base);
    }
    size_t print(long n, int base = DEC);
    size_t print(unsigned long n, int base = DEC);
    size_t print(double n, int digits = 2);

    size_t println(void) {
        return write("\r\n");
    }
    template <typename T>
    size_t println(T value) {
        size_t n = print(value);
        return n + println();
    }
    template <typename T>
    size_t println(T value, int format) {
        size_t n = print(value, format);
        return n + println();
    }

private:
    size_t printNumber(unsigned long n, uint8_t base);
};

// Serial goes to the CircuitPython console (USB CDC / REPL).
class HardwareSerial : public Print {
public:
    constexpr HardwareSerial() {
    }
    void begin(unsigned long baud) {
        (void)baud;
    }
    void begin(unsigned long baud, int config) {
        (void)baud;
        (void)config;
    }
    void end(void) {
    }
    int available(void) {
        return 0;
    }
    int read(void) {
        return -1;
    }
    int peek(void) {
        return -1;
    }
    void flush(void) {
    }
    operator bool() {
        return true;
    }
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;
};

extern HardwareSerial Serial;

#endif // __cplusplus
