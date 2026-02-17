/*
Author: Brandon S Coventry      Wisconsin Institute for Translational Neuroengineering
Date: 2026/02/12 Initialization
Purpose: Chronos firmware ported for the nRF54L15
Revision History: See Github history
Notes:
 - Includes the ability to turn bluetooth transmission on and off in config file. Set CONFIG_BT in config file to 0 to turn off BLE engine
 - Primary stimulation driver is still and interupt based routine. 
*/

/*Basic includes. Minimize to limit current draw from unused engines.*/
//Zephyr includes
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/irq.h> //Interrupt service routine handlers
#include <zephyr/device.h>  //Must import devicetree files
#include <zephyr/devicetree.h>
#include <soc.h>    //Standard System on chip imports
#include <zephyr/logging/log.h>   //Disable on compile

//nRFX imports. Note we are primarily runnig
#include <nrfx_spim.h>
#include <nrfx_timer.h>
#include <nrfx_rtc.h>
#include <zephyr/sys/atomic.h>
#include <hal/nrf_gpio.h>

//Chronos engine imports 
#include "BLE.h"  //BLE engine
#include "spi.h" //SPI to howland current source
#include "timer.h"  //Handles interrupt timing of stimulation
#include "rtc_stim.h"   //handles IRQ routines for stimulation
#include "config.h"   //Set compilation settings. Look here for CONFIG_BT

//Add UART Driver only if BLE is active. Note, UART is *virtual*, but still requires pin definitions. 
#if defined(CONFIG_BT)
#include <uart_async_adapter.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#endif

//Set up peripherial initialization
static void init_clock();   //Turn on stimulation timing engine
#if defined(CONFIG_BT)
BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.recycled         = recycled_cb,
#ifdef CONFIG_BT_NUS_SECURITY_ENABLED
	.security_changed = security_changed,
#endif
};
static struct bt_nus_cb nus_cb = {
	.received = bt_receive_cb,
};
#endif /* CONFIG_BT */

static void init_pins(void) {
    /*
    Note for readers on this use of gpio_cfg. The nRF54 has 3 seperate application cores with different purposes. Core 2 is for general SPI cfg.
    Mapping can be done by incrementing over pin count (ie if core 1 has 16 pins, pin 18 corresponds to core 2, pin 2), but this is unclear.
    Using the pin map operation explicitly declares core and pin location.
    */
    // Configure P2.5 as output (DAC1 CS)
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 5));
    nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(2, 5));  // Set high (inactive)
    
    // Configure P2.10 as output (DAC2 CS)
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 10));
    nrf_gpio_pin_set(NRF_GPIO_PIN_MAP(2, 10));  // Set high (inactive)
    
    // Switch GPIOs (DAC1/2 -> switch): all 0 at init
    //HCSS1
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 7));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(2, 7)); //set low (inactive)
    //HCSS2
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 9));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(2, 9)); //set low (inactive)
    //HCSS3
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 8));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(2, 8));
    //HCSS4
    nrf_gpio_cfg_output(NRF_GPIO_PIN_MAP(2, 0));
    nrf_gpio_pin_clear(NRF_GPIO_PIN_MAP(2, 0));
}

int main(void)
{
    //Begin with system initialization
    init_clock();
    init_pins();
    spi_init();
    timer_init();
    update_pulse_width(CONFIG_PULSE_WIDTH_US);
    update_dac1_amplitude(CONFIG_STIM_AMPLITUDE);
    update_dac2_amplitude(CONFIG_STIM_AMPLITUDE);
}
