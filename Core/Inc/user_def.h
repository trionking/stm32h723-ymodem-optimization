/*
 * user_def.h
 *
 *  Created on: Oct 1, 2025
 *      Author: SIDO
 */

#ifndef INC_USER_DEF_H_
#define INC_USER_DEF_H_



#include "main.h"

void led_out(uint8_t ld_val);
void rel_out(uint8_t rel_val);
uint8_t read_dip(void);

void test_gpio(void);
void init_sdmmc(void);
int mount_fatfs(void);
void sound_mon_test(void);

void sound_mon_select(uint8_t channel);

void init_proc(void);
void run_proc(void);

// SPI 테스트 함수
void spi_test_basic(uint8_t slave_id);
void spi_test_data(uint8_t slave_id);
void spi_test_multi_slave(void);
void spi_test_error(uint8_t slave_id);
void spi_test_all(uint8_t slave_id);
void spi_test_play(uint8_t slave_id, uint8_t channel);
void spi_test_stop(uint8_t slave_id, uint8_t channel);

// SD 카드용 DMA-safe 버퍼 (RAM_D1_DMA, 32KB)
extern uint8_t sdmmc1_buffer[32768];

// ============================================================================
// UART2 DMA TX용 함수 (논블럭 printf)
// ============================================================================

#define DMA_TX_BUFFER_SIZE  512

// UART2 TX Queue 및 DMA 상태
extern volatile uint8_t g_uart2_tx_busy;
extern volatile uint8_t g_ymodem_active;  // Y-MODEM 처리 중 플래그

// UART2 DMA TX 함수
void init_UART2_DMA_TX(void);
void UART2_Process_TX_Queue(void);
void UART2_TX_Complete_Callback(void);

#endif /* INC_USER_DEF_H_ */
