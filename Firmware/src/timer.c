#include <nrfx_timer.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <hal/nrf_gpio.h>
#include "timer.h"
#include "spi.h"
#include "config.h"

#if defined(CONFIG_SOC_NRF5340_CPUAPP)
/* Re-apply P1.00/P1.01 MCUSEL to App before each use; network/pinctrl can overwrite. */
static void p1_mcu_select_app_sync(void)
{
#if defined(NRF_P1)
	NRF_GPIO_Type *p1 = NRF_P1;
#elif defined(NRF_GPIO1)
	NRF_GPIO_Type *p1 = NRF_GPIO1;
#else
#error "Need NRF_P1 or NRF_GPIO1 for P1 MCUSEL"
#endif
	uint32_t msk = 3u << 16;
	uint32_t app = 1u << 16;
	uint32_t c0 = p1->PIN_CNF[0] & ~msk;
	uint32_t c1 = p1->PIN_CNF[1] & ~msk;
	p1->PIN_CNF[0] = c0 | app;
	p1->PIN_CNF[1] = c1 | app;
}
#endif

static uint32_t timer_freq_hz = 0;  
static uint32_t main_event_time = 0;
static uint32_t event1_time = 0;
static uint32_t event2_time = 0;
static uint32_t event3_time = 0;

static atomic_t counter;            // test variable to record how many times the timer handler has been called 
static atomic_t error;
static atomic_t event1_error_max;
static atomic_t event2_error_max;
static atomic_t event3_error_max;
static atomic_t event0_error_counter;
static atomic_t event0_error_max;
static uint32_t prev_main_event_time = 0;
static nrfx_timer_t measurement_timer = NRFX_TIMER_INSTANCE(1); // Use a separate timer for measurements
static nrfx_timer_t timer_inst = NRFX_TIMER_INSTANCE(TIMER_INST_IDX);; // Timer instance for the main timer
static uint32_t current_period_us = DEFAULT_STIM_PERIOD;
static uint32_t current_pulse_width_us = DEFAULT_PULSE_WIDTH;
static void timer_handler(nrf_timer_event_t event_type, void * p_context);

void get_error_data(error_data *data) {
    data->event1_max = atomic_get(&event1_error_max);
    data->event2_max = atomic_get(&event2_error_max);
    data->event3_max = atomic_get(&event3_error_max);
    data->event0_error = atomic_get(&event0_error_counter);
    data->event0_max = atomic_get(&event0_error_max);
    data->myerror = atomic_get(&error);
    data->mycounter = atomic_get(&counter);
}

void update_stim_frequency(uint16_t frequency_hz) {
    if (frequency_hz == 0) {
        printf("Invalid frequency: 0 Hz\n");
        return;
    }
    
    // Calculate period in microseconds from frequency in Hz
    uint32_t period_us = 1000000 / frequency_hz;
    current_period_us = period_us;
    
    // Convert to timer ticks
    uint32_t period_ticks = nrfx_timer_us_to_ticks(&timer_inst, period_us);

    //LEE ADDING CODE *************************************************************************************************************************
    //Clear the TIMER to stop missing compare events
    nrfx_timer_disable(&timer_inst);
    nrfx_timer_clear(&timer_inst);
    //LEE DONE ADDING CODE*************************************************************************************************************************

    // Update channel 0 compare value
    // Note: We keep the SHORT to clear on compare to maintain periodic operation
    nrfx_timer_extended_compare(&timer_inst, NRF_TIMER_CC_CHANNEL0, period_ticks, 
        NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);

    //LEE STILL ADDING CODE*************************************************************************************************************************
    //Start the timer again
    nrfx_timer_enable(&timer_inst);
    //LEE DONE ADDING CODE*************************************************************************************************************************

    // Also update the measurement timer expectations if needed
    if (MEASURE_TIMER == 1) {
        // Reset error counters when frequency changes
        atomic_set(&event0_error_counter, 0);
        atomic_set(&event0_error_max, 0);
        atomic_set(&counter,0);            
        atomic_set(&error,0);
        atomic_set(&event1_error_max,0);
        atomic_set(&event2_error_max,0);
        atomic_set(&event3_error_max,0);
    }
    
    printf("Timer frequency updated to %u Hz (period: %lu us, ticks: %lu)\n", 
           frequency_hz, period_us, period_ticks);
}

void update_pulse_width(uint16_t pulse_width_us) {
    if (pulse_width_us == 0) {
        printf("Invalid pulse width: 0 us\n");
        return;
    }
    
    current_pulse_width_us = pulse_width_us;
    
    // Calculate new positions for channels 1 and 3
    // Channel 1: pulse_width after channel 0 (end of first pulse)
    uint32_t channel1_ticks = nrfx_timer_us_to_ticks(&timer_inst, pulse_width_us);
    
    // Channel 2 stays at its current position
    uint32_t channel2_us = pulse_width_us + SWITCH_PERIOD;
    uint32_t channel2_ticks = nrfx_timer_us_to_ticks(&timer_inst, channel2_us);
    
    // Channel 3: pulse_width after channel 2 (end of second pulse)
    uint32_t channel3_us = channel2_us + pulse_width_us;
    uint32_t channel3_ticks = nrfx_timer_us_to_ticks(&timer_inst, channel3_us);
    
    // Update the compare values
    nrfx_timer_compare(&timer_inst, NRF_TIMER_CC_CHANNEL1, channel1_ticks, true);
    nrfx_timer_compare(&timer_inst, NRF_TIMER_CC_CHANNEL3, channel3_ticks, true);
    
    // Also need to make sure channel 2 is still at the right position
    nrfx_timer_compare(&timer_inst, NRF_TIMER_CC_CHANNEL2, channel2_ticks, true);
    
    printf("Pulse width updated to %u us (ticks: %lu)\n", pulse_width_us, channel1_ticks);
    printf("Channel 1 at %u us, Channel 2 at %lu us, Channel 3 at %lu us\n", 
           pulse_width_us, channel2_us, channel3_us);
}

void timer_init(void)
{
    atomic_set(&counter, 0);
    atomic_set(&error, 0);
    uint32_t base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(timer_inst.p_reg);
    timer_freq_hz = base_frequency;
    printf("Timer frequency: %lu Hz\n", timer_freq_hz);

    nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(base_frequency);
    config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    config.p_context = &timer_inst;
    nrfx_err_t status = nrfx_timer_init(&timer_inst, &config, timer_handler);
    nrfx_timer_clear(&timer_inst);
    if (status != NRFX_SUCCESS) {
        printf("Timer initialization failed with error: %d\n", status);
    }

#if defined(CONFIG_BT)
    /* BLE mode: continuous timer, period on CC0, clear on compare */
    current_pulse_width_us = DEFAULT_PULSE_WIDTH;
    uint32_t event1_ticks = nrfx_timer_us_to_ticks(&timer_inst, DEFAULT_PULSE_WIDTH);
    uint32_t event2_ticks = nrfx_timer_us_to_ticks(&timer_inst,
        (DEFAULT_PULSE_WIDTH + SWITCH_PERIOD));
    uint32_t event3_ticks = nrfx_timer_us_to_ticks(&timer_inst,
        (2 * DEFAULT_PULSE_WIDTH + SWITCH_PERIOD));
    nrfx_timer_extended_compare(&timer_inst, NRF_TIMER_CC_CHANNEL0,
        nrfx_timer_us_to_ticks(&timer_inst, DEFAULT_STIM_PERIOD),
        NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, true);
    nrfx_timer_extended_compare(&timer_inst, NRF_TIMER_CC_CHANNEL1, event1_ticks, 0, true);
    nrfx_timer_extended_compare(&timer_inst, NRF_TIMER_CC_CHANNEL2, event2_ticks, 0, true);
    nrfx_timer_extended_compare(&timer_inst, NRF_TIMER_CC_CHANNEL3, event3_ticks, 0, true);
    nrfx_timer_enable(&timer_inst);
    printf("Timer status: %s (BLE continuous)\n",
        nrfx_timer_is_enabled(&timer_inst) ? "enabled" : "disabled");
#else
    /* RTC low-power mode: timer not started; started per period from rtc_stim */
    current_pulse_width_us = CONFIG_PULSE_WIDTH_US;
    printf("Timer status: one-shot (RTC-driven)\n");
#endif
}

#if !defined(CONFIG_BT)
void timer_do_event0(void)
{
#if defined(CONFIG_SOC_NRF5340_CPUAPP)
    p1_mcu_select_app_sync();
#endif
    /* First pulse (DAC1): same as timer COMPARE0 */
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, 0));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, 1));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(0, 13));
    nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, 3));
    {
        uint8_t phase1_tx[] = {0xFF, 0xAA};
        spi_write_dac1(phase1_tx, dac1_buf_rx);
    }
}

void timer_start_one_shot_biphasic(void)
{
    nrfx_timer_disable(&timer_inst);
    nrfx_timer_clear(&timer_inst);
    uint32_t e1 = nrfx_timer_us_to_ticks(&timer_inst, current_pulse_width_us);
    uint32_t e2 = nrfx_timer_us_to_ticks(&timer_inst,
        current_pulse_width_us + SWITCH_PERIOD);
    uint32_t e3 = nrfx_timer_us_to_ticks(&timer_inst,
        2 * current_pulse_width_us + SWITCH_PERIOD);
    nrfx_timer_compare(&timer_inst, NRF_TIMER_CC_CHANNEL1, e1, true);
    nrfx_timer_compare(&timer_inst, NRF_TIMER_CC_CHANNEL2, e2, true);
    nrfx_timer_compare(&timer_inst, NRF_TIMER_CC_CHANNEL3, e3, true);
    nrfx_timer_enable(&timer_inst);
}
#endif

nrfx_timer_t measurement_timer_init() {
    nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(NRF_TIMER_BASE_FREQUENCY_GET(measurement_timer.p_reg));
    config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    nrfx_err_t err = nrfx_timer_init(&measurement_timer, &config, NULL); // No handler needed
    nrfx_timer_enable(&measurement_timer);
    return measurement_timer;
}

static void timer_handler(nrf_timer_event_t event_type, void * p_context)
{
#if defined(CONFIG_SOC_NRF5340_CPUAPP)
	p1_mcu_select_app_sync();
#endif
    // Get reference to timer
    atomic_inc(&counter);
    //printf("Time handler count: %i \n", counter);
    nrfx_timer_t *timer_inst = (nrfx_timer_t *)p_context;
    uint32_t current_time;
    uint32_t current_max;
    uint32_t my_error;
    uint32_t elapsed1_ticks;
    
    switch(event_type) {
        case NRF_TIMER_EVENT_COMPARE0:
            if(MEASURE_TIMER == 1){
                current_time = nrfx_timer_capture(&measurement_timer, NRF_TIMER_CC_CHANNEL0);
                    
                if (prev_main_event_time > 0) {
                    // Calculate actual interval duration
                    uint32_t interval_ticks = current_time - prev_main_event_time;
                    uint32_t expected_ticks = nrfx_timer_us_to_ticks(&measurement_timer, DEFAULT_STIM_PERIOD);
                    uint32_t event0_error = abs(interval_ticks - expected_ticks);
                    
                    // Update statistics
                    atomic_add(&event0_error_counter, event0_error);
                    
                    // Track maximum error
                    current_max = atomic_get(&event0_error_max);
                    if (event0_error > current_max) {
                        atomic_set(&event0_error_max, event0_error);
                    }
                }
                prev_main_event_time = current_time;
                // Capture timestamp when main event occurs (after timer reset)
                main_event_time = nrfx_timer_capture(timer_inst, NRF_TIMER_CC_CHANNEL4);
            }

            // First pulse (DAC1): 1.00=0, 1.01=0, 0.13=0; 1.03=1 before & during TX
            nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, 0));
            nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, 1));
            nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(0, 13));
            nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, 3));
            {
                uint8_t phase1_tx[] = {0xFF, 0xAA};
                spi_write_dac1(phase1_tx, dac1_buf_rx);
            }
            break;
            
        case NRF_TIMER_EVENT_COMPARE1:
            if(MEASURE_TIMER == 1){
                // Capture timestamp when event 1 occurs
                current_time = nrfx_timer_capture(timer_inst, NRF_TIMER_CC_CHANNEL4);
                // Calculate elapsed time from main event
                elapsed1_ticks = current_time - main_event_time;
                my_error = abs(elapsed1_ticks-DEFAULT_PULSE_WIDTH*timer_freq_hz/1000000);
                atomic_add(&error,my_error);
                current_max = atomic_get(&event1_error_max);
                if (my_error > current_max) {atomic_set(&event1_error_max, my_error);}
            }
        
            // Interphase 10 us: 1.03=0, 1.00=1, 1.01=1
            nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, 3));
            nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, 0));
            nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, 1));
            break;
            
        case NRF_TIMER_EVENT_COMPARE2:
            if(MEASURE_TIMER == 1){
                // Capture timestamp when event 2 occurs
                current_time = nrfx_timer_capture(timer_inst, NRF_TIMER_CC_CHANNEL4);
                // Calculate elapsed time from event 1
                my_error = abs(elapsed1_ticks-SWITCH_PERIOD*timer_freq_hz/1000000);
                atomic_add(&error, my_error);
                current_max = atomic_get(&event2_error_max);
                if (my_error > current_max) {atomic_set(&event2_error_max, my_error);}
            }
            
            // Second pulse (DAC2): 1.00=0, 1.01=0, 0.13=1; DAC2 TX
            nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, 0));
            nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, 1));
            //nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(0, 13));
            nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, 3));
            {
                uint8_t phase2_tx[] = {0x00, 0x56};
                spi_write_dac1(phase2_tx, dac2_buf_rx);
            }
            break;
            
        case NRF_TIMER_EVENT_COMPARE3:
            if(MEASURE_TIMER == 1){
                // Capture timestamp when event 3 occurs
                current_time = nrfx_timer_capture(timer_inst, NRF_TIMER_CC_CHANNEL4);
                // Calculate elapsed time from event 2
                my_error = abs(elapsed1_ticks-DEFAULT_PULSE_WIDTH*timer_freq_hz/1000000);
                atomic_add(&error,my_error);
                current_max = atomic_get(&event3_error_max);
                if (my_error > current_max) {atomic_set(&event3_error_max, my_error);}
            }

            /* Between biphasic pairs: 1.03=0, 0.13=0, 1.00=1, 1.01=1 */
            nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(1, 3));
            nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(0, 13));
            nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, 0));
            nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(1, 1));

#if !defined(CONFIG_BT)
            /* RTC mode: one shot per period; disable timer until next RTC wake */
            nrfx_timer_disable(timer_inst);
#endif
            break;
    }
}