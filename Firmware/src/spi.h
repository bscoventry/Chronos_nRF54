#ifndef SPI_H
#define SPI_H
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <hal/nrf_gpio.h>

#define DAC1_CS_PIN NRF_GPIO_PIN_MAP(2, 5)  // P2.05
#define DAC2_CS_PIN NRF_GPIO_PIN_MAP(2, 10)  // P2.10
#define DAC_TX_LEN			2   //2bytes
#define DAC_RX_LEN			2

#define SPIM_INST_IDX 1
#define MOSI_PIN NRF_GPIO_PIN_MAP(2, 2)
#define MISO_PIN NRF_GPIO_PIN_MAP(2, 4)
#define SCK_PIN NRF_GPIO_PIN_MAP(2, 1)   //2.01

void cs_select(uint32_t pin_number);
void cs_deselect(uint32_t pin_number);
void spi_write_dac1(uint8_t *tx_data, uint8_t *rx_data);
void spi_write_dac2(uint8_t *tx_data, uint8_t *rx_data);
void spi_init();
void update_dac1_amplitude(uint16_t amplitude);
void update_dac2_amplitude(uint16_t amplitude);

extern uint8_t dac1_buf_rx[DAC_RX_LEN];
extern uint8_t dac1_buf_tx[DAC_TX_LEN];
extern uint8_t dac2_buf_rx[DAC_RX_LEN];
extern uint8_t dac2_buf_tx[DAC_TX_LEN];
#endif
