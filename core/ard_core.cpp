// SPDX-FileCopyrightText: 2026 Mikey Sklar for Adafruit Industries
// SPDX-License-Identifier: MIT
//
// C++ half of the Arduino compat core. ardbuild.py compiles this in the same
// translation unit as the sketch, so there is one set of inline functions and
// vtables, and one constructor function (ard_ctors) for ard_mp.c to call.

#include "Arduino.h"

extern "C" {
void ard_write(const uint8_t *buf, size_t len);
void *ard_malloc(size_t n);
void ard_free(void *p);
void ard_core_ready(void);

// Global destructors never run: the module lives until the VM resets.
void *__dso_handle = 0;
int __cxa_atexit(void (*fn)(void *), void *arg, void *dso) {
    (void)fn;
    (void)arg;
    (void)dso;
    return 0;
}
void __cxa_pure_virtual(void) {
    while (1) {
    }
}
}

void *operator new(size_t n) {
    return ard_malloc(n);
}
void *operator new[](size_t n) {
    return ard_malloc(n);
}
void operator delete(void *p) noexcept {
    ard_free(p);
}
void operator delete[](void *p) noexcept {
    ard_free(p);
}
void operator delete(void *p, size_t) noexcept {
    ard_free(p);
}
void operator delete[](void *p, size_t) noexcept {
    ard_free(p);
}

HardwareSerial Serial;

size_t HardwareSerial::write(uint8_t c) {
    ard_write(&c, 1);
    return 1;
}

size_t HardwareSerial::write(const uint8_t *buffer, size_t size) {
    ard_write(buffer, size);
    return size;
}

size_t Print::write(const uint8_t *buffer, size_t size) {
    size_t n = 0;
    while (size--) {
        n += write(*buffer++);
    }
    return n;
}

size_t Print::printNumber(unsigned long n, uint8_t base) {
    char buf[8 * sizeof(long) + 1];
    char *str = &buf[sizeof(buf) - 1];
    *str = '\0';
    if (base < 2) {
        base = 10;
    }
    do {
        char c = n % base;
        n /= base;
        *--str = c < 10 ? c + '0' : c + 'A' - 10;
    } while (n);
    return write(str);
}

size_t Print::print(long n, int base) {
    if (base == 0) {
        return write((uint8_t)n);
    }
    if (base == 10 && n < 0) {
        return print('-') + printNumber(-(unsigned long)n, 10);
    }
    return printNumber(n, base);
}

size_t Print::print(unsigned long n, int base) {
    if (base == 0) {
        return write((uint8_t)n);
    }
    return printNumber(n, base);
}

// Same algorithm as Arduino's Print::printFloat.
size_t Print::print(double number, int digits) {
    if (isnan(number)) {
        return print("nan");
    }
    if (isinf(number)) {
        return print("inf");
    }
    if (number > 4294967040.0 || number < -4294967040.0) {
        return print("ovf");
    }
    size_t n = 0;
    if (number < 0.0) {
        n += print('-');
        number = -number;
    }
    double rounding = 0.5;
    for (int i = 0; i < digits; ++i) {
        rounding /= 10.0;
    }
    number += rounding;
    unsigned long int_part = (unsigned long)number;
    double remainder = number - (double)int_part;
    n += print(int_part);
    if (digits > 0) {
        n += print('.');
    }
    while (digits-- > 0) {
        remainder *= 10.0;
        unsigned int to_print = (unsigned int)remainder;
        n += print(to_print);
        remainder -= to_print;
    }
    return n;
}

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

static uint32_t ard_rand_state = 2463534242u;

static uint32_t ard_rand_next(void) {
    // xorshift32
    uint32_t x = ard_rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ard_rand_state = x;
    return x;
}

long random(long howbig) {
    if (howbig <= 0) {
        return 0;
    }
    return ard_rand_next() % howbig;
}

long random(long howsmall, long howbig) {
    if (howsmall >= howbig) {
        return howsmall;
    }
    return random(howbig - howsmall) + howsmall;
}

void randomSeed(unsigned long seed) {
    if (seed != 0) {
        ard_rand_state = seed;
    }
}

void shiftOut(unsigned int dataPin, unsigned int clockPin, uint8_t bitOrder, uint8_t val) {
    for (uint8_t i = 0; i < 8; i++) {
        if (bitOrder == LSBFIRST) {
            digitalWrite(dataPin, val & 1);
            val >>= 1;
        } else {
            digitalWrite(dataPin, (val & 128) != 0);
            val <<= 1;
        }
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}

uint8_t shiftIn(unsigned int dataPin, unsigned int clockPin, uint8_t bitOrder) {
    uint8_t value = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        digitalWrite(clockPin, HIGH);
        if (bitOrder == LSBFIRST) {
            value |= digitalRead(dataPin) << i;
        } else {
            value |= digitalRead(dataPin) << (7 - i);
        }
        digitalWrite(clockPin, LOW);
    }
    return value;
}

// Guarantees this translation unit has a constructor function, so ard_ctors
// always exists for ard_mp.c to call even when the sketch has no globals
// with constructors.
static struct ArdCoreInit {
    ArdCoreInit() {
        ard_core_ready();
    }
} ard_core_init;
