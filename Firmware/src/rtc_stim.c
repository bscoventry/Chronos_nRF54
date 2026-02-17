/*
 * RTC-driven stimulation: one wake per period from LFCLK, then HFCLK + TIMER
 * for the biphasic burst. Ultra-low-power: CPU sleeps between pulses.
 */
#include <nrfx_rtc.h>
#include <hal/nrf_rtc.h>
#include <hal/nrf_clock.h>
#include <zephyr/kernel.h>
#include "rtc_stim.h"
#include "timer.h"
#include "config.h"

#define RTC_STIM_INST_IDX 0
#define LFCLK_FREQ_HZ 32768u
/* RTC prescaler 0: one tick = 1/32768 s */
#define RTC_PRESCALER 0

static nrfx_rtc_t rtc_inst = NRFX_RTC_INSTANCE(RTC_STIM_INST_IDX);
static uint32_t rtc_period_ticks;

static void rtc_handler(nrfx_rtc_int_type_t int_type)
{
	if (int_type != NRFX_RTC_INT_COMPARE0) {
		return;
	}

	/* Ensure HFCLK is running for SPI and TIMER */
	NRF_CLOCK_S->TASKS_HFCLKSTART = 1;
	while (NRF_CLOCK_S->EVENTS_HFCLKSTARTED == 0) {
		/* spin */
	}
	NRF_CLOCK_S->EVENTS_HFCLKSTARTED = 0;

	/* Start of pulse: same as timer COMPARE0 (GPIO + DAC1 SPI) */
	timer_do_event0();
	/* Run one biphasic period via TIMER (COMPARE1/2/3); timer disables itself after COMPARE3 */
	timer_start_one_shot_biphasic();

	/* nrfx disables compare channel after event; re-arm for next period (counter was cleared by SHORT) */
	(void)nrfx_rtc_cc_set(&rtc_inst, 0, rtc_period_ticks, true);
}

void rtc_stim_start_lfclk(void)
{
	NRF_CLOCK_S->LFCLKSRC = (CLOCK_LFCLKSRC_SRC_LFRC << CLOCK_LFCLKSRC_SRC_Pos);
	NRF_CLOCK_S->TASKS_LFCLKSTART = 1;
	while (NRF_CLOCK_S->EVENTS_LFCLKSTARTED == 0) {
		/* spin */
	}
	NRF_CLOCK_S->EVENTS_LFCLKSTARTED = 0;
}

void rtc_stim_init(uint16_t frequency_hz)
{
	if (frequency_hz == 0) {
		return;
	}
	/* Period in us; RTC ticks at 32768 Hz → period_ticks = period_us * 32768 / 1e6 */
	uint32_t period_us = 1000000u / frequency_hz;
	uint32_t period_ticks = (uint32_t)((uint64_t)period_us * LFCLK_FREQ_HZ / 1000000u);
	if (period_ticks == 0) {
		period_ticks = 1;
	}
	rtc_period_ticks = period_ticks;

	nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG;
	config.prescaler = RTC_PRESCALER;
	nrfx_err_t err = nrfx_rtc_init(&rtc_inst, &config, rtc_handler);
	if (err != NRFX_SUCCESS) {
		return;
	}
	nrfx_rtc_tick_enable(&rtc_inst, false);
	nrfx_rtc_overflow_enable(&rtc_inst, false);
	/* Set compare channel 0 and enable interrupt */
	err = nrfx_rtc_cc_set(&rtc_inst, 0, period_ticks, true);
	if (err != NRFX_SUCCESS) {
		return;
	}
	/* Clear on compare so period repeats every period_ticks (nRF53: RTC_SHORTS_COMPARE0_CLEAR_Msk) */
	rtc_inst.p_reg->SHORTS = RTC_SHORTS_COMPARE0_CLEAR_Msk;
	nrf_rtc_task_trigger(rtc_inst.p_reg, NRF_RTC_TASK_CLEAR);
	nrfx_rtc_enable(&rtc_inst);
}
