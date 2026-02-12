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
#include <zephyr/kernel.h>
//Add UART Driver only if BLE is active. Note, UART is *virtual*, but still requires pin definitions. 
#if defined(CONFIG_BT)
#include <uart_async_adapter.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#endif
int main(void)
{
        return 0;
}
