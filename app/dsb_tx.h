#ifndef APP_DSB_TX_H
#define APP_DSB_TX_H

#include <stdbool.h>
#include <stdint.h>

bool DSB_TX_Start(uint32_t frequency, bool suppress_carrier);
void DSB_TX_Stop(void);
bool DSB_TX_IsActive(void);

#endif
