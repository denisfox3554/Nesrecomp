#ifndef CPU_H
#define CPU_H

/*
 * cpu.h — CPU 6502 state, flags, stack helpers
 *
 * Фокус: только регистры и флаги процессора.
 * Не знает про PPU, APU, mapper.
 * Если нужно что-то исправить в CPU — редактируй cpu_interp.c + этот файл.
 */

#include <stdint.h>

typedef struct {
    uint8_t  A, X, Y, SP;
    uint16_t PC;
    /* Флаги — отдельные байты для скорости (без битовых операций) */
    uint8_t  N, V, D, I, Z, C;
} CPU;

extern CPU cpu;

/* Установить флаги N и Z по значению */
#define SET_NZ(v) do { \
    cpu.N = ((v) >> 7) & 1; \
    cpu.Z = ((v) == 0) ? 1 : 0; \
} while(0)

/* Собрать байт флагов P */
static inline uint8_t get_P(void) {
    return (cpu.N << 7) | (cpu.V << 6) | 0x20
         | (cpu.D << 3) | (cpu.I << 2) | (cpu.Z << 1) | cpu.C;
}

/* Разобрать байт флагов P */
static inline void set_P(uint8_t p) {
    cpu.N = (p >> 7) & 1;
    cpu.V = (p >> 6) & 1;
    cpu.D = (p >> 3) & 1;
    cpu.I = (p >> 2) & 1;
    cpu.Z = (p >> 1) & 1;
    cpu.C =  p       & 1;
}

/*
 * Interpreter — cpu_interp.c
 * cpu_interp_step(): выполняет одну инструкцию, возвращает кол-во циклов
 * cpu_interp_run(addr): выполняет до RTS/RTI (fallback для dispatch)
 */
int  cpu_interp_step(void);
void cpu_interp_run(uint16_t entry);

/* BRK обработка (определена в memory.c, т.к. нужен доступ к памяти) */
void cpu_brk(void);

#endif /* CPU_H */
