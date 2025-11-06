/*
 * spi_protocol.h
 *
 *  Created on: Oct 26, 2025
 *      Author: Audio Mux Project
 *
 *  SPI 통신 프로토콜 - Master 측 (STM32H723)
 */

#ifndef INC_SPI_PROTOCOL_H_
#define INC_SPI_PROTOCOL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* 프로토콜 상수 */
#define SPI_CMD_HEADER      0xC0    // 명령 패킷 헤더
#define SPI_DATA_HEADER     0xDA    // 데이터 패킷 헤더

#define SPI_CMD_PLAY        0x01    // 재생 시작
#define SPI_CMD_STOP        0x02    // 재생 정지
#define SPI_CMD_VOLUME      0x03    // 볼륨 조절
#define SPI_CMD_STATUS      0x04    // 상태 요청
#define SPI_CMD_RESET       0xFF    // 채널 리셋

#define SPI_SLAVE_COUNT     3       // Slave 보드 개수
#define SPI_CHANNEL_PER_SLAVE 2     // 각 Slave당 채널 수

#define SPI_BUFFER_SIZE     4100    // 데이터 패킷 크기 (헤더 4바이트 + 2048샘플*2바이트)

/* 명령 패킷 구조체 (5 바이트) - slave_id 제거됨
 * 각 슬레이브는 독립적인 CS 핀을 가지므로 slave_id 불필요 */
typedef struct __attribute__((packed)) {
    uint8_t header;         // 0xC0
    uint8_t channel;        // 0=DAC1, 1=DAC2
    uint8_t cmd;            // 명령 코드
    uint8_t param_h;        // 파라미터 상위 바이트
    uint8_t param_l;        // 파라미터 하위 바이트
} SPI_CommandPacket_t;

/* 데이터 패킷 구조체 (가변 길이) - slave_id 제거됨 */
typedef struct __attribute__((packed)) {
    uint8_t header;         // 0xDA
    uint8_t channel;        // 0=DAC1, 1=DAC2
    uint8_t length_h;       // 샘플 수 상위 바이트
    uint8_t length_l;       // 샘플 수 하위 바이트
    // 이후 오디오 데이터 (length * 2 바이트)
} SPI_DataPacketHeader_t;

/* Slave 선택용 CS 핀 정보 */
typedef struct {
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    GPIO_TypeDef *rdy_port;
    uint16_t rdy_pin;
} SPI_SlaveConfig_t;

/* 함수 프로토타입 */

/**
 * @brief  SPI 프로토콜 초기화
 * @param  hspi: SPI 핸들 포인터
 * @retval None
 */
void spi_protocol_init(SPI_HandleTypeDef *hspi);

/**
 * @brief  Slave 선택 (CS 핀 LOW)
 * @param  slave_id: Slave ID (0~2)
 * @retval None
 */
void spi_select_slave(uint8_t slave_id);

/**
 * @brief  Slave 선택 해제 (CS 핀 HIGH)
 * @param  slave_id: Slave ID (0~2)
 * @retval None
 */
void spi_deselect_slave(uint8_t slave_id);

/**
 * @brief  RDY 핀 상태 확인
 * @param  slave_id: Slave ID (0~2)
 * @retval 1: 준비됨 (HIGH), 0: 준비 안 됨 (LOW)
 */
uint8_t spi_check_ready(uint8_t slave_id);

/**
 * @brief  명령 패킷 전송
 * @param  slave_id: Slave ID (0~2)
 * @param  channel: 채널 번호 (0=DAC1, 1=DAC2)
 * @param  cmd: 명령 코드
 * @param  param: 파라미터 (16비트)
 * @retval HAL_OK: 성공, 기타: 에러
 */
HAL_StatusTypeDef spi_send_command(uint8_t slave_id, uint8_t channel, uint8_t cmd, uint16_t param);

/**
 * @brief  데이터 패킷 전송 (DMA 사용)
 * @param  slave_id: Slave ID (0~2)
 * @param  channel: 채널 번호 (0=DAC1, 1=DAC2)
 * @param  samples: 오디오 샘플 버퍼 (16비트)
 * @param  num_samples: 샘플 수
 * @retval HAL_OK: 성공, 기타: 에러
 */
HAL_StatusTypeDef spi_send_data_dma(uint8_t slave_id, uint8_t channel, uint16_t *samples, uint16_t num_samples);

/**
 * @brief  DMA 전송 완료 대기
 * @param  timeout_ms: 타임아웃 (밀리초)
 * @retval HAL_OK: 성공, HAL_TIMEOUT: 타임아웃
 */
HAL_StatusTypeDef spi_wait_dma_complete(uint32_t timeout_ms);

/**
 * @brief  SPI 전송 버퍼 가져오기 (내부 사용)
 * @param  slave_id: Slave ID (0~2)
 * @retval 버퍼 포인터
 */
uint8_t* spi_get_tx_buffer(uint8_t slave_id);

#ifdef __cplusplus
}
#endif

#endif /* INC_SPI_PROTOCOL_H_ */
