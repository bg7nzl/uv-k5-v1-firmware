#include "app/dsb_tx.h"

#include "ARMCM0.h"
#include "bsp/dp32g030/irq.h"
#include "bsp/dp32g030/syscon.h"
#include "bsp/dp32g030/timerbase.h"
#include "driver/bk4819.h"

#define DSB_TX_SAMPLE_RATE_HZ 4000u
#define DSB_TX_TIMER_DIV      0u
#define SYSTEM_CLOCK_HZ       48000000u

typedef struct {
	bool active;
	bool suppress_carrier;
	bool timer_running;
	uint8_t max_bias;
	uint8_t carrier_bias;
	uint8_t pa_gain;
	uint16_t initial_pa_reg;
	uint32_t frequency;
} DSB_State_t;

static volatile DSB_State_t gDsbState;

static bool DSB_TX_TimerStart(const uint32_t sample_rate_hz)
{
	const uint32_t divider = DSB_TX_TIMER_DIV + 1u;
	const uint32_t ticks = SYSTEM_CLOCK_HZ / (divider * sample_rate_hz);
	uint16_t low_load;

	if (sample_rate_hz == 0u || ticks == 0u || ticks > 0x10000u)
		return false;

	low_load = (uint16_t)(ticks - 1u);

	SYSCON_DEV_CLK_GATE = (SYSCON_DEV_CLK_GATE & ~SYSCON_DEV_CLK_GATE_TIMER_BASE0_MASK) | SYSCON_DEV_CLK_GATE_TIMER_BASE0_BITS_ENABLE;

	TIMER_BASE0_EN = 0u;
	TIMER_BASE0_DIV = DSB_TX_TIMER_DIV;
	TIMER_BASE0_LOW_LOAD = low_load;
	TIMER_BASE0_IF = TIMER_BASE_IF_LOW;
	TIMER_BASE0_IE = TIMER_BASE_IE_LOW;
	NVIC_ClearPendingIRQ((IRQn_Type)DP32_TIMER_BASE0_IRQn);
	NVIC_EnableIRQ((IRQn_Type)DP32_TIMER_BASE0_IRQn);
	TIMER_BASE0_EN = TIMER_BASE_EN_LOW;

	return true;
}

static void DSB_TX_TimerStop(void)
{
	TIMER_BASE0_EN = 0u;
	TIMER_BASE0_IE = 0u;
	TIMER_BASE0_IF = TIMER_BASE_IF_LOW | TIMER_BASE_IF_HIGH;
	NVIC_DisableIRQ((IRQn_Type)DP32_TIMER_BASE0_IRQn);
}

static uint8_t DSB_TX_ScaleAmplitude(const uint16_t raw)
{
	if (raw > 0x7F00u)
		return 255u;

	return (uint8_t)(raw >> 7);
}

static void DSB_TX_ApplyEnvelope(void)
{
	const uint8_t amp = DSB_TX_ScaleAmplitude(BK4819_GetVoiceAmplitudeOut());
	uint8_t bias;

	if (gDsbState.suppress_carrier) {
		bias = (uint8_t)(((uint16_t)amp * gDsbState.max_bias) / 255u);
	} else {
		const uint8_t range = gDsbState.max_bias - gDsbState.carrier_bias;
		bias = gDsbState.carrier_bias + (uint8_t)(((uint16_t)range * amp) / 255u);
	}

	BK4819_WriteRegister(BK4819_REG_36, ((uint16_t)bias << 8) | gDsbState.pa_gain);
}

bool DSB_TX_Start(const uint32_t frequency, const bool suppress_carrier)
{
	if (gDsbState.active)
		DSB_TX_Stop();

	const uint16_t pa = BK4819_ReadRegister(BK4819_REG_36);
	const uint8_t max_bias = (uint8_t)(pa >> 8);

	gDsbState.initial_pa_reg = pa;
	gDsbState.pa_gain = (uint8_t)(pa & 0x00FFu);
	gDsbState.max_bias = (max_bias == 0u) ? 64u : max_bias;
	gDsbState.carrier_bias = suppress_carrier ? 0u : (gDsbState.max_bias / 3u);
	gDsbState.suppress_carrier = suppress_carrier;
	gDsbState.frequency = frequency;
	gDsbState.timer_running = DSB_TX_TimerStart(DSB_TX_SAMPLE_RATE_HZ);
	if (!gDsbState.timer_running)
		return false;

	gDsbState.active = true;

	BK4819_WriteRegister(BK4819_REG_36, ((uint16_t)gDsbState.carrier_bias << 8) | gDsbState.pa_gain);
	return true;
}

void DSB_TX_Stop(void)
{
	if (!gDsbState.active)
		return;

	DSB_TX_TimerStop();
	gDsbState.timer_running = false;
	gDsbState.active = false;
	BK4819_WriteRegister(BK4819_REG_36, gDsbState.initial_pa_reg);
}

bool DSB_TX_IsActive(void)
{
	return gDsbState.active;
}

static void DSB_TX_Process(void)
{
	if (!gDsbState.active)
		return;

	DSB_TX_ApplyEnvelope();
}

void HandlerTIMER_BASE0_Real(void)
{
	if ((TIMER_BASE0_IF & TIMER_BASE_IF_LOW) == 0u)
		return;

	TIMER_BASE0_IF = TIMER_BASE_IF_LOW;
	DSB_TX_Process();
}
