/*
 * uart_command.h
 *
 *  UART 명령 파서 헤더
 */

#ifndef INC_UART_COMMAND_H_
#define INC_UART_COMMAND_H_

#include "main.h"
#include <stdint.h>

// 설정
#define UART_CMD_MAX_LENGTH     256
#define UART_CMD_MAX_ARGS       10

// 명령 구조체
typedef struct {
    char command[32];
    char *argv[UART_CMD_MAX_ARGS];
    int argc;
} UartCommand_t;

// 전송 방식
typedef enum {
    CMD_TRANSPORT_UART,
    CMD_TRANSPORT_USB_CDC
} CmdTransport_t;

// 함수 프로토타입
void uart_command_init(UART_HandleTypeDef *huart);
void usb_cdc_command_init(void);  // USB CDC 초기화
void uart_command_task(void);
void uart_send_response(const char *format, ...);
void uart_send_error(int code, const char *message);
void uart_command_rx_callback(void);
void parse_and_execute_command(char *cmd_line);
void set_command_transport(CmdTransport_t transport);  // 전송 방식 설정
CmdTransport_t get_command_transport(void);  // 현재 전송 방식 반환

#endif /* INC_UART_COMMAND_H_ */
