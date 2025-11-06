/*
 * ymodem.h
 *
 *  Y-MODEM 프로토콜 헤더
 */

#ifndef INC_YMODEM_H_
#define INC_YMODEM_H_

#include "main.h"
#include "ff.h"

// Y-MODEM 상수
#define YMODEM_SOH              0x01  // 128-byte block
#define YMODEM_STX              0x02  // 1024-byte block
#define YMODEM_EOT              0x04  // End of transmission
#define YMODEM_ACK              0x06  // Acknowledge
#define YMODEM_NAK              0x15  // Negative acknowledge
#define YMODEM_CAN              0x18  // Cancel
#define YMODEM_CRC16            0x43  // 'C' for CRC mode

#define YMODEM_PACKET_SIZE      1024
#define YMODEM_TIMEOUT_MS       5000   // 5초 (SD 쓰기 지연 대응)

// 재시도 설정
#define YMODEM_MAX_TIMEOUT_RETRIES  5   // 타임아웃 재시도 최대 횟수
#define YMODEM_MAX_NAK_RETRIES      10  // NAK 재시도 최대 횟수

// 결과 코드
typedef enum {
    YMODEM_OK = 0,
    YMODEM_ERROR,
    YMODEM_TIMEOUT,
    YMODEM_CANCELLED,
    YMODEM_CRC_ERROR
} YmodemResult_t;

// 함수 프로토타입
YmodemResult_t ymodem_receive(UART_HandleTypeDef *huart, const char *file_path);

#endif /* INC_YMODEM_H_ */
