/*
 * spi_protocol.c
 *
 *  Created on: Oct 26, 2025
 *      Author: Audio Mux Project
 *
 *  SPI 통신 프로토콜 구현
 */

#include "spi_protocol.h"
#include <string.h>
#include <stdio.h>

/* SPI 전송 버퍼 (RAM_D1_DMA - DMA 접근 최적화, 캐시 OFF) */
uint8_t spi_tx_buffer[SPI_SLAVE_COUNT][SPI_BUFFER_SIZE]
    __attribute__((section(".ram_d1_dma")))
    __attribute__((aligned(32)));

/* Slave 설정 (CS 및 RDY 핀 매핑) */
static const SPI_SlaveConfig_t slave_config[SPI_SLAVE_COUNT] = {
    {OT_SPI1_NSS1_GPIO_Port, OT_SPI1_NSS1_Pin, IN_SPI1_RDY1_GPIO_Port, IN_SPI1_RDY1_Pin},  // Slave 0
    {OT_SPI1_NSS2_GPIO_Port, OT_SPI1_NSS2_Pin, IN_SPI1_RDY2_GPIO_Port, IN_SPI1_RDY2_Pin},  // Slave 1
    {OT_SPI1_NSS3_GPIO_Port, OT_SPI1_NSS3_Pin, IN_SPI1_RDY3_GPIO_Port, IN_SPI1_RDY3_Pin}   // Slave 2
};

/* SPI 핸들 (전역 변수, main.c에서 extern 선언됨) */
static SPI_HandleTypeDef *hspi_protocol = NULL;

/* DMA 전송 완료 플래그 및 현재 Slave ID */
static volatile uint8_t spi_dma_busy = 0;
static volatile uint8_t spi_current_slave = 0xFF;  // 0xFF = 없음

/**
 * @brief  SPI 프로토콜 초기화
 */
void spi_protocol_init(SPI_HandleTypeDef *hspi)
{
    hspi_protocol = hspi;

    /* 모든 CS 핀을 HIGH로 설정 (비선택 상태) */
    for (uint8_t i = 0; i < SPI_SLAVE_COUNT; i++) {
        HAL_GPIO_WritePin(slave_config[i].cs_port, slave_config[i].cs_pin, GPIO_PIN_SET);
    }

    printf("SPI Protocol: Initialized\r\n");
}

/**
 * @brief  Slave 선택 (CS 핀 LOW)
 */
void spi_select_slave(uint8_t slave_id)
{
    if (slave_id >= SPI_SLAVE_COUNT) {
        return;
    }

    HAL_GPIO_WritePin(slave_config[slave_id].cs_port, slave_config[slave_id].cs_pin, GPIO_PIN_RESET);

    /* CS falling edge 후 100us 지연 (Slave 준비 시간 + 동기화)
     * STM32H723 @ 550MHz: 약 55,000 사이클 = 100us
     * 안전하게 11000 루프 (각 루프는 약 5 사이클)
     */
    for (volatile int i = 0; i < 11000; i++);
}

/**
 * @brief  Slave 선택 해제 (CS 핀 HIGH)
 */
void spi_deselect_slave(uint8_t slave_id)
{
    if (slave_id >= SPI_SLAVE_COUNT) {
        return;
    }

    /* 전송 완료 후 짧은 지연 */
    for (volatile int i = 0; i < 100; i++);

    HAL_GPIO_WritePin(slave_config[slave_id].cs_port, slave_config[slave_id].cs_pin, GPIO_PIN_SET);
}

/**
 * @brief  RDY 핀 상태 확인 (Active Low)
 * @note   LOW = Ready (1), HIGH = Busy (0)
 */
uint8_t spi_check_ready(uint8_t slave_id)
{
    if (slave_id >= SPI_SLAVE_COUNT) {
        return 0;
    }

    GPIO_PinState state = HAL_GPIO_ReadPin(slave_config[slave_id].rdy_port, slave_config[slave_id].rdy_pin);
    return (state == GPIO_PIN_RESET) ? 1 : 0;  // Active Low: LOW = ready (1), HIGH = busy (0)
}

/**
 * @brief  명령 패킷 전송
 */
HAL_StatusTypeDef spi_send_command(uint8_t slave_id, uint8_t channel, uint8_t cmd, uint16_t param)
{
    SPI_CommandPacket_t packet;
    HAL_StatusTypeDef status;

    if (slave_id >= SPI_SLAVE_COUNT || hspi_protocol == NULL) {
        return HAL_ERROR;
    }

    /* 명령 패킷 구성 (5바이트 - slave_id 제거됨) */
    packet.header = SPI_CMD_HEADER;
    packet.channel = channel;
    packet.cmd = cmd;
    packet.param_h = (param >> 8) & 0xFF;
    packet.param_l = param & 0xFF;

    /* 디버그: 패킷 내용 출력 (CS LOW 이전에 출력) */
    printf("[DEBUG] Sending to Slave%d: %02X %02X %02X %02X %02X (%u bytes)\r\n",
           slave_id, packet.header, packet.channel, packet.cmd, packet.param_h, packet.param_l,
           sizeof(SPI_CommandPacket_t));

    /* 패킷 전송 - CS LOW부터 HIGH까지 최소 시간 유지 */
    spi_select_slave(slave_id);
    status = HAL_SPI_Transmit(hspi_protocol, (uint8_t*)&packet, sizeof(SPI_CommandPacket_t), 100);
    spi_deselect_slave(slave_id);

    /* 디버그: 전송 결과 출력 (CS HIGH 이후) */
    printf("[DEBUG] SPI result: %s (status=%d)\r\n", (status == HAL_OK) ? "OK" : "FAILED", status);

    if (status == HAL_OK) {
        printf("SPI: Sent cmd=0x%02X to Slave%d Ch%d param=%u\r\n", cmd, slave_id, channel, param);
    } else {
        printf("SPI: Failed to send command (error %d)\r\n", status);
    }

    return status;
}

/**
 * @brief  데이터 패킷 전송 (DMA 사용)
 */
HAL_StatusTypeDef spi_send_data_dma(uint8_t slave_id, uint8_t channel, uint16_t *samples, uint16_t num_samples)
{
    HAL_StatusTypeDef status;
    uint8_t *tx_buf;
    SPI_DataPacketHeader_t *header;

    if (slave_id >= SPI_SLAVE_COUNT || hspi_protocol == NULL || samples == NULL || num_samples == 0) {
        return HAL_ERROR;
    }

    /* DMA 사용 중이면 에러 */
    if (spi_dma_busy) {
        printf("SPI: DMA busy\r\n");
        return HAL_BUSY;
    }

    /* 전송 버퍼 가져오기 */
    tx_buf = spi_tx_buffer[slave_id];

    /* 데이터 패킷 헤더 구성 (4바이트 - slave_id 제거됨) */
    header = (SPI_DataPacketHeader_t*)tx_buf;
    header->header = SPI_DATA_HEADER;
    header->channel = channel;
    header->length_h = (num_samples >> 8) & 0xFF;
    header->length_l = num_samples & 0xFF;

    /* 오디오 데이터 복사 (헤더 이후) */
    memcpy(tx_buf + sizeof(SPI_DataPacketHeader_t), samples, num_samples * 2);

    /* 전송 크기: 헤더(4) + 오디오 데이터(num_samples * 2) */
    uint32_t tx_size = sizeof(SPI_DataPacketHeader_t) + (num_samples * 2);

    /* CS 선택 */
    spi_select_slave(slave_id);

    /* DMA 전송 시작 */
    spi_current_slave = slave_id;  // 현재 Slave 기록
    spi_dma_busy = 1;

    /* 디버깅: SPI 상태 확인 */
    printf("SPI: Starting DMA: slave=%d, size=%lu, SPI_State=%d\r\n",
           slave_id, tx_size, hspi_protocol->State);

    status = HAL_SPI_Transmit_DMA(hspi_protocol, tx_buf, tx_size);

    if (status != HAL_OK) {
        spi_dma_busy = 0;
        spi_current_slave = 0xFF;  // 초기화
        spi_deselect_slave(slave_id);
        printf("SPI: Failed to start DMA (error %d), SPI_State=%d, ErrorCode=0x%lx\r\n",
               status, hspi_protocol->State, hspi_protocol->ErrorCode);
        return status;
    }

    return HAL_OK;
}

/**
 * @brief  DMA 전송 완료 대기
 */
HAL_StatusTypeDef spi_wait_dma_complete(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();

    printf("[DMA] Waiting for completion (busy=%d)...\r\n", spi_dma_busy);

    while (spi_dma_busy) {
        if ((HAL_GetTick() - start_tick) > timeout_ms) {
            uint32_t elapsed = HAL_GetTick() - start_tick;
            printf("SPI: DMA timeout after %lums (busy still=%d)\r\n", elapsed, spi_dma_busy);
            return HAL_TIMEOUT;
        }
    }

    uint32_t elapsed = HAL_GetTick() - start_tick;
    printf("[DMA] Completed in %lums\r\n", elapsed);

    return HAL_OK;
}

/**
 * @brief  SPI 전송 버퍼 가져오기
 */
uint8_t* spi_get_tx_buffer(uint8_t slave_id)
{
    if (slave_id >= SPI_SLAVE_COUNT) {
        return NULL;
    }
    return spi_tx_buffer[slave_id];
}

/**
 * @brief  SPI TX DMA 완료 콜백 (HAL에서 호출)
 */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    printf("[DMA] TxCpltCallback called: hspi=%p, hspi_protocol=%p\r\n", hspi, hspi_protocol);

    if (hspi == hspi_protocol) {
        printf("[DMA] Clearing busy flag, slave=%d\r\n", spi_current_slave);
        spi_dma_busy = 0;

        /* CS 해제 (DMA 완료 후) */
        if (spi_current_slave < SPI_SLAVE_COUNT) {
            spi_deselect_slave(spi_current_slave);
            spi_current_slave = 0xFF;  // 초기화
        }
    } else {
        printf("[DMA] ERROR: hspi mismatch!\r\n");
    }
}
