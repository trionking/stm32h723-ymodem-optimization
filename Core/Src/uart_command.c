/*
 * uart_command.c
 *
 *  UART 명령 파서 구현
 */

#include "uart_command.h"
#include "command_handler.h"
#include "ansi_colors.h"
#include "usbd_cdc_if.h"  // USB CDC 전송 함수
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// 전역 변수
static UART_HandleTypeDef *huart_cmd = NULL;
static char uart_rx_buffer[UART_CMD_MAX_LENGTH];
static uint16_t uart_rx_index = 0;
static uint8_t uart_rx_char;
static CmdTransport_t current_transport = CMD_TRANSPORT_UART;  // 기본값: UART

// 초기화
void uart_command_init(UART_HandleTypeDef *huart)
{
    huart_cmd = huart;
    uart_rx_index = 0;
    memset(uart_rx_buffer, 0, sizeof(uart_rx_buffer));

    printf("[DEBUG] UART command init: starting interrupt RX\r\n");

    // NVIC 상태 확인
    uint32_t nvic_enabled = NVIC_GetEnableIRQ(USART2_IRQn);
    uint32_t nvic_priority = NVIC_GetPriority(USART2_IRQn);
    printf("[DEBUG] USART2 NVIC: enabled=%lu, priority=%lu\r\n", nvic_enabled, nvic_priority);

    // 첫 문자 수신 시작 (인터럽트 모드)
    HAL_StatusTypeDef status = HAL_UART_Receive_IT(huart_cmd, &uart_rx_char, 1);

    if (status == HAL_OK) {
        printf("[DEBUG] UART command init: interrupt RX started successfully\r\n");
    } else {
        printf("[ERROR] UART command init: interrupt RX failed, status=%d\r\n", status);
    }

    // UART 레지스터 상태 확인
    printf("[DEBUG] USART2->CR1 = 0x%08lX (RXNEIE bit should be set)\r\n", huart_cmd->Instance->CR1);
}

// UART RX 완료 콜백 (stm32h7xx_it.c에서 호출)
void uart_command_rx_callback(void)
{
    static uint32_t rx_count = 0;
    rx_count++;

    // 수신한 문자를 버퍼에 추가
    if (uart_rx_char == '\r' || uart_rx_char == '\n') {
        if (uart_rx_index > 0) {
            // 명령 종료
            uart_rx_buffer[uart_rx_index] = '\0';

            printf("[DEBUG] RX callback: received command '%s' (count=%lu)\r\n", uart_rx_buffer, rx_count);

            // 명령 파싱 및 처리
            parse_and_execute_command(uart_rx_buffer);

            // 버퍼 클리어
            uart_rx_index = 0;
        }
    }
    else if (uart_rx_index < UART_CMD_MAX_LENGTH - 1) {
        uart_rx_buffer[uart_rx_index++] = uart_rx_char;
    }
    else {
        // 버퍼 오버플로우
        uart_rx_index = 0;
        uart_send_error(400, "Command too long");
    }

    // 다음 문자 수신
    HAL_UART_Receive_IT(huart_cmd, &uart_rx_char, 1);
}

// 명령 파싱
void parse_and_execute_command(char *cmd_line)
{
    UartCommand_t cmd;
    char *token;

    // 초기화
    memset(&cmd, 0, sizeof(cmd));
    cmd.argc = 0;

    // 첫 번째 토큰 (명령어)
    token = strtok(cmd_line, " ");
    if (token == NULL) {
        return;  // 빈 명령
    }

    strncpy(cmd.command, token, sizeof(cmd.command) - 1);

    // 나머지 토큰 (인수)
    while ((token = strtok(NULL, " ")) != NULL && cmd.argc < UART_CMD_MAX_ARGS) {
        cmd.argv[cmd.argc++] = token;
    }

    // 명령 실행
    execute_command(&cmd);
}

// 전송 방식 설정
void set_command_transport(CmdTransport_t transport)
{
    current_transport = transport;
    printf("[DEBUG] Command transport set to %s\r\n",
           transport == CMD_TRANSPORT_UART ? "UART" : "USB CDC");
}

// 현재 전송 방식 반환
CmdTransport_t get_command_transport(void)
{
    return current_transport;
}

// USB CDC 초기화
void usb_cdc_command_init(void)
{
    uart_rx_index = 0;
    memset(uart_rx_buffer, 0, sizeof(uart_rx_buffer));
    current_transport = CMD_TRANSPORT_USB_CDC;
    printf("[DEBUG] USB CDC command interface initialized\r\n");
}

// 응답 전송
void uart_send_response(const char *format, ...)
{
    char buffer[512];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    size_t len = strlen(buffer);

    // 전송 방식에 따라 다르게 전송
    if (current_transport == CMD_TRANSPORT_USB_CDC) {
        // USB CDC로 전송 - UserTxBufferHS에 복사하여 전송
        extern USBD_HandleTypeDef hUsbDeviceHS;
        extern uint8_t UserTxBufferHS[APP_TX_DATA_SIZE];

        USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceHS.pClassData;
        uint8_t result;
        uint32_t retry_count = 0;
        const uint32_t max_retries = 200;  // 최대 200회 재시도

        // 이전 전송 완료 대기
        while (hcdc->TxState != 0 && retry_count < max_retries) {
            retry_count++;
            // Busy-wait (인터럽트가 처리될 시간 제공)
            for (volatile int i = 0; i < 10000; i++);
        }

        if (retry_count >= max_retries) {
            printf("[ERROR] CDC_Transmit: previous transmission timeout (retries=%lu)\r\n", retry_count);
            return;
        }

        // UserTxBufferHS에 데이터 복사 (버퍼 수명 문제 해결)
        if (len > APP_TX_DATA_SIZE) {
            len = APP_TX_DATA_SIZE;
        }
        memcpy(UserTxBufferHS, buffer, len);

        // 전송 (비동기 방식 - 완료를 기다리지 않음)
        result = CDC_Transmit_HS(UserTxBufferHS, len);
        if (result == USBD_BUSY) {
            // Busy 상태면 잠시 후 재시도
            HAL_Delay(1);
            result = CDC_Transmit_HS(UserTxBufferHS, len);
        }

        if (result != USBD_OK && result != USBD_BUSY) {
            printf("[ERROR] CDC_Transmit failed: %d\r\n", result);
        }
    } else {
        // UART로 전송
        if (huart_cmd != NULL) {
            HAL_UART_Transmit(huart_cmd, (uint8_t*)buffer, len, HAL_MAX_DELAY);
        }
    }
}

// 에러 응답
void uart_send_error(int code, const char *message)
{
    uart_send_response(ANSI_RED "ERR" ANSI_RESET " %d %s\r\n", code, message);
}

// 태스크 (폴링 모드용, 현재는 인터럽트 사용)
void uart_command_task(void)
{
    // 인터럽트 모드에서는 사용 안 함
}
