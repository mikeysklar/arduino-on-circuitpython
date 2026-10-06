// SPDX-FileCopyrightText: 2026 Mikey Sklar for Adafruit Industries
// SPDX-License-Identifier: MIT
//
// C half of the Arduino compat core: the module entry point, and the Arduino
// pin/time/print functions implemented by calling CircuitPython's board,
// digitalio, analogio, pwmio and time modules through the native module
// function table.

#include "py/dynruntime.h"

// Must match Arduino.h.
#define ARD_INPUT          0x0
#define ARD_OUTPUT         0x1
#define ARD_INPUT_PULLUP   0x2
#define ARD_INPUT_PULLDOWN 0x3
#define ARD_LED_BUILTIN    0x100
#define ARD_A0             0x200
#define ARD_MODE_UNSET     0xff

// Provided by the sketch translation unit (sketch_all.cpp).
void ard_ctors(void);
void ard_call_setup(void);
void ard_call_loop(void);

enum { ARD_PIN_DIO, ARD_PIN_AIN, ARD_PIN_PWM };

typedef struct {
    unsigned int id;
    uint8_t kind;
    uint8_t mode;
    mp_obj_t obj;
} ard_pin_t;

#define ARD_MAX_PINS 48

// Native module bss must not be static (mpy_ld only reaches it through the GOT).
ard_pin_t ard_pins[ARD_MAX_PINS];
size_t ard_n_pins;
// Every object created here is also appended to this list, which is stored in
// the module globals, so the GC can always see it.
mp_obj_t ard_keep;
mp_obj_t ard_board;
mp_obj_t ard_getattr;
mp_obj_t ard_sleep;
mp_obj_t ard_monotonic_ns;
mp_obj_t ard_start_ns;
int ard_read_bits;
int ard_write_bits;
uint32_t ard_pwm_freq;

static mp_obj_t keep(mp_obj_t o) {
    mp_obj_list_append(ard_keep, o);
    return o;
}

static mp_obj_t import(qstr name) {
    return keep(mp_import_name(name, mp_const_none, MP_OBJ_NEW_SMALL_INT(0)));
}

static mp_obj_t call(mp_obj_t fun, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    return mp_call_function_n_kw(fun, n_args, n_kw, args);
}

static mp_obj_t call_method0(mp_obj_t obj, qstr name) {
    return call(mp_load_attr(obj, name), 0, 0, NULL);
}

static size_t fmt_uint(char *buf, unsigned int n) {
    char tmp[10];
    size_t len = 0;
    do {
        tmp[len++] = '0' + n % 10;
        n /= 10;
    } while (n);
    for (size_t i = 0; i < len; i++) {
        buf[i] = tmp[len - 1 - i];
    }
    return len;
}

// The board pin object for an Arduino pin number.
static mp_obj_t board_pin(unsigned int id) {
    if (id == ARD_LED_BUILTIN) {
        return mp_load_attr(ard_board, MP_QSTR_LED);
    }
    char name[12];
    unsigned int n = id;
    name[0] = 'D';
    if (id >= ARD_A0) {
        name[0] = 'A';
        n = id - ARD_A0;
    }
    size_t len = 1 + fmt_uint(name + 1, n);
    mp_obj_t args[3] = { ard_board, mp_obj_new_str(name, len), mp_const_none };
    mp_obj_t pin = call(ard_getattr, 3, 0, args);
    if (pin == mp_const_none && id == 13) {
        // Many examples hardcode 13 as "the LED" (it is on an Uno).
        args[1] = MP_OBJ_NEW_QSTR(MP_QSTR_LED);
        pin = call(ard_getattr, 3, 0, args);
        if (pin != mp_const_none) {
            mp_printf(&mp_plat_print, "arduino: this board has no D13, using board.LED for pin 13\n");
        }
        args[1] = mp_obj_new_str(name, len);
    }
    if (pin == mp_const_none) {
        mp_obj_t msg_args[1] = { args[1] };
        mp_obj_t fmt = mp_obj_new_str("pin {} is not on this board", 27);
        mp_raise_type_arg(&mp_type_ValueError, call(mp_load_attr(fmt, MP_QSTR_format), 1, 0, msg_args));
    }
    return pin;
}

static void pin_release(ard_pin_t *p) {
    if (p->obj != MP_OBJ_NULL) {
        call_method0(p->obj, MP_QSTR_deinit);
        p->obj = MP_OBJ_NULL;
    }
}

// Find or create the CircuitPython object for a pin, as the given kind.
static ard_pin_t *pin_get(unsigned int id, int kind) {
    ard_pin_t *p = NULL;
    for (size_t i = 0; i < ard_n_pins; i++) {
        if (ard_pins[i].id == id) {
            p = &ard_pins[i];
            break;
        }
    }
    if (p != NULL && p->obj != MP_OBJ_NULL && p->kind == kind) {
        return p;
    }
    if (p == NULL) {
        if (ard_n_pins == ARD_MAX_PINS) {
            mp_raise_ValueError(MP_ERROR_TEXT("too many pins in use"));
        }
        p = &ard_pins[ard_n_pins++];
        p->id = id;
        p->obj = MP_OBJ_NULL;
    }
    pin_release(p);
    mp_obj_t pin = board_pin(id);
    p->kind = kind;
    p->mode = ARD_MODE_UNSET;
    if (kind == ARD_PIN_DIO) {
        mp_obj_t cls = mp_load_attr(import(MP_QSTR_digitalio), MP_QSTR_DigitalInOut);
        p->obj = keep(call(cls, 1, 0, &pin));
    } else if (kind == ARD_PIN_AIN) {
        mp_obj_t cls = mp_load_attr(import(MP_QSTR_analogio), MP_QSTR_AnalogIn);
        p->obj = keep(call(cls, 1, 0, &pin));
    } else {
        mp_obj_t cls = mp_load_attr(import(MP_QSTR_pwmio), MP_QSTR_PWMOut);
        mp_obj_t args[3] = { pin, MP_OBJ_NEW_QSTR(MP_QSTR_frequency), mp_obj_new_int(ard_pwm_freq) };
        p->obj = keep(call(cls, 1, 1, args));
    }
    return p;
}

static void dio_set_mode(ard_pin_t *p, unsigned int mode) {
    mp_obj_t digitalio = import(MP_QSTR_digitalio);
    mp_obj_t direction = mp_load_attr(digitalio, MP_QSTR_Direction);
    if (mode == ARD_OUTPUT) {
        mp_store_attr(p->obj, MP_QSTR_direction, mp_load_attr(direction, MP_QSTR_OUTPUT));
    } else {
        mp_store_attr(p->obj, MP_QSTR_direction, mp_load_attr(direction, MP_QSTR_INPUT));
        mp_obj_t pull = mp_const_none;
        if (mode == ARD_INPUT_PULLUP) {
            pull = mp_load_attr(mp_load_attr(digitalio, MP_QSTR_Pull), MP_QSTR_UP);
        } else if (mode == ARD_INPUT_PULLDOWN) {
            pull = mp_load_attr(mp_load_attr(digitalio, MP_QSTR_Pull), MP_QSTR_DOWN);
        }
        mp_store_attr(p->obj, MP_QSTR_pull, pull);
    }
    p->mode = mode;
}

void pinMode(unsigned int pin, unsigned int mode) {
    dio_set_mode(pin_get(pin, ARD_PIN_DIO), mode);
}

void digitalWrite(unsigned int pin, unsigned int val) {
    ard_pin_t *p = pin_get(pin, ARD_PIN_DIO);
    if (p->mode == ARD_INPUT || p->mode == ARD_INPUT_PULLUP) {
        // Classic Arduino: writing HIGH to an input turns on its pull-up.
        dio_set_mode(p, val ? ARD_INPUT_PULLUP : ARD_INPUT);
        return;
    }
    if (p->mode != ARD_OUTPUT) {
        dio_set_mode(p, ARD_OUTPUT);
    }
    mp_store_attr(p->obj, MP_QSTR_value, val ? mp_const_true : mp_const_false);
}

int digitalRead(unsigned int pin) {
    ard_pin_t *p = pin_get(pin, ARD_PIN_DIO);
    if (p->mode == ARD_MODE_UNSET) {
        dio_set_mode(p, ARD_INPUT);
    }
    return mp_obj_is_true(mp_load_attr(p->obj, MP_QSTR_value));
}

int analogRead(unsigned int pin) {
    // AVR convention: analogRead(0) means A0.
    if (pin < 8) {
        pin += ARD_A0;
    }
    ard_pin_t *p = pin_get(pin, ARD_PIN_AIN);
    // AnalogIn.value is always 16 bits.
    unsigned int v = mp_obj_get_int(mp_load_attr(p->obj, MP_QSTR_value));
    return v >> (16 - ard_read_bits);
}

void analogWrite(unsigned int pin, int val) {
    ard_pin_t *p = pin_get(pin, ARD_PIN_PWM);
    int top = (1 << ard_write_bits) - 1;
    if (val < 0) {
        val = 0;
    } else if (val > top) {
        val = top;
    }
    unsigned int duty = (unsigned int)val * 65535u / (unsigned int)top;
    mp_store_attr(p->obj, MP_QSTR_duty_cycle, mp_obj_new_int(duty));
}

void analogReadResolution(int bits) {
    if (bits >= 1 && bits <= 16) {
        ard_read_bits = bits;
    }
}

void analogWriteResolution(int bits) {
    if (bits >= 1 && bits <= 16) {
        ard_write_bits = bits;
    }
}

void analogWriteFreq(uint32_t freq) {
    ard_pwm_freq = freq;
}

// Time since the sketch was loaded (Arduino counts from reset, which is when
// its sketch starts), keeping the low 32 bits like Arduino.
static unsigned long ticks(unsigned int divisor) {
    mp_obj_t ns = mp_binary_op(MP_BINARY_OP_SUBTRACT, call(ard_monotonic_ns, 0, 0, NULL), ard_start_ns);
    return mp_obj_get_int_truncated(mp_binary_op(MP_BINARY_OP_FLOOR_DIVIDE, ns, mp_obj_new_int(divisor)));
}

unsigned long millis(void) {
    return ticks(1000000);
}

unsigned long micros(void) {
    return ticks(1000);
}

// time.sleep runs CircuitPython's background tasks and lets Ctrl-C through.
// The firmware may use a different float ABI than this module (RP2350 is
// softfp, dynruntime.mk builds armv7emsp as hard), so no float crosses the
// function table: CircuitPython does the division itself.
void delay(unsigned long ms) {
    mp_obj_t secs = mp_binary_op(MP_BINARY_OP_TRUE_DIVIDE, mp_obj_new_int_from_uint(ms), mp_obj_new_int(1000));
    call(ard_sleep, 1, 0, &secs);
}

void delayMicroseconds(unsigned int us) {
    unsigned long start = micros();
    while (micros() - start < us) {
    }
}

void yield(void) {
    delay(0);
}

void ard_write(const uint8_t *buf, size_t len) {
    mp_plat_print.print_strn(mp_plat_print.data, (const char *)buf, len);
}

void *ard_malloc(size_t n) {
    return m_malloc(n);
}

void ard_free(void *p) {
    m_free(p);
}

void ard_core_ready(void) {
}

// The compiler emits calls to these even when the sketch never names them.
#undef memset
#undef memmove
#undef memcpy
void *memset(void *s, int c, size_t n) {
    return mp_fun_table.memset_(s, c, n);
}
void *memmove(void *dest, const void *src, size_t n) {
    return mp_fun_table.memmove_(dest, src, n);
}
void *memcpy(void *dest, const void *src, size_t n) {
    return mp_fun_table.memmove_(dest, src, n);
}
size_t strlen(const char *s) {
    const char *p = s;
    while (*p) {
        p++;
    }
    return p - s;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++) {
        if (*x != *y) {
            return *x - *y;
        }
    }
    return 0;
}

static mp_obj_t setup(void) {
    ard_call_setup();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(setup_obj, setup);

static mp_obj_t loop(void) {
    ard_call_loop();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(loop_obj, loop);

mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    MP_DYNRUNTIME_INIT_ENTRY

    ard_keep = mp_obj_new_list(0, NULL);
    mp_store_global(MP_QSTR__ard_keep, ard_keep);
    ard_n_pins = 0;
    ard_read_bits = 10;
    ard_write_bits = 8;
    ard_pwm_freq = 1000;
    ard_board = import(MP_QSTR_board);
    ard_getattr = keep(mp_load_global(MP_QSTR_getattr));
    mp_obj_t time = import(MP_QSTR_time);
    ard_sleep = keep(mp_load_attr(time, MP_QSTR_sleep));
    ard_monotonic_ns = keep(mp_load_attr(time, MP_QSTR_monotonic_ns));
    ard_start_ns = keep(call(ard_monotonic_ns, 0, 0, NULL));

    // Global C++ constructors, which mpy_ld does not run.
    ard_ctors();

    mp_store_global(MP_QSTR_setup, MP_OBJ_FROM_PTR(&setup_obj));
    mp_store_global(MP_QSTR_loop, MP_OBJ_FROM_PTR(&loop_obj));

    MP_DYNRUNTIME_INIT_EXIT
}
