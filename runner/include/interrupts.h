#ifndef INTERRUPTS_H
#define INTERRUPTS_H

/*
 * interrupts.h — объявления прерываний NES
 *
 * Реализованы в runner.c (устанавливают флаги g_nmi_pending / g_irq_pending).
 * Вызываются из:
 *   ppu.c        -> nes_nmi()  (VBlank на scanline 241, dot 1)
 *   mapper.c     -> nes_irq()  (MMC3 scanline counter)
 *   cpu_interp.c -> nes_irq()  (BRK инструкция)
 *   runner.c     -> nes_reset() (при старте)
 *
 * Сами прерывания обрабатываются в главном цикле runner_run():
 * проверяем флаги перед каждой инструкцией CPU и пушим PC+P на стек.
 */

void nes_nmi(void);    /* Non-Maskable Interrupt — VBlank */
void nes_irq(void);    /* Maskable IRQ — mapper, APU frame */
void nes_reset(void);  /* Сброс — читает вектор $FFFC */

/* dispatch таблица из сгенерированного кода (или stub) */
void call_by_address(uint16_t addr);

#endif /* INTERRUPTS_H */
