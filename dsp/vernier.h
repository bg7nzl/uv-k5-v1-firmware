/* Copyright 2025 bg7nzl
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef DSP_VERNIER_H
#define DSP_VERNIER_H

#include <stdint.h>

typedef struct {
	uint16_t xtal_trim;
	uint16_t pll_comp;
	int16_t  error_mhz;
} VernierResult_t;

VernierResult_t VERNIER_Solve(int32_t delta_f_mhz, uint32_t alpha_mhz);
uint32_t        VERNIER_ComputeAlpha(uint32_t f_carrier_hz);

#endif
