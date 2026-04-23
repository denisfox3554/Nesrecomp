/* stub_full.c — интерпретаторный режим, без generated кода */
#include "runner.h"

void call_by_address(uint16_t addr) {
    cpu_interp_run(addr);
}
