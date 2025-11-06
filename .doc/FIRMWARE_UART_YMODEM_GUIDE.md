# 펌웨어 UART 명령 처리 및 Y-MODEM 구현 가이드

**작성일**: 2025-10-26
**대상**: STM32H723 Main Board 펌웨어
**프로토콜 버전**: 1.0

---

## 📋 목차
1. [개요](#1-개요)
2. [파일 구조](#2-파일-구조)
3. [UART 명령 파서 구현](#3-uart-명령-파서-구현)
4. [Y-MODEM 수신 구현](#4-y-modem-수신-구현)
5. [명령 핸들러 구현](#5-명령-핸들러-구현)
6. [통합 가이드](#6-통합-가이드)
7. [테스트 방법](#7-테스트-방법)

---

## 1. 개요

### 1.1 추가할 기능
- UART2를 통한 PC 통신
- 텍스트 기반 명령 파싱
- Y-MODEM 프로토콜 파일 수신
- SD 카드 파일 관리

### 1.2 메모리 요구사항
- UART RX 버퍼: 256 bytes
- 명령 버퍼: 256 bytes
- Y-MODEM 버퍼: 1024 bytes
- 총: 약 2 KB

---

## 2. 파일 구조

### 2.1 추가할 파일

```
Core/
├── Inc/
│   ├── uart_command.h      # UART 명령 파서 헤더
│   ├── ymodem.h           # Y-MODEM 프로토콜 헤더
│   └── command_handler.h  # 명령 핸들러 헤더
└── Src/
    ├── uart_command.c      # UART 명령 파서 구현
    ├── ymodem.c           # Y-MODEM 프로토콜 구현
    └── command_handler.c  # 명령 핸들러 구현
```

### 2.2 수정할 파일
- `Core/Src/user_def.c`: 메인 루프에 UART 처리 추가
- `Core/Src/stm32h7xx_it.c`: UART2 인터럽트 핸들러
- `Debug/Core/Src/subdir.mk`: 새 소스 파일 추가

---

## 3. UART 명령 파서 구현

### 3.1 uart_command.h

```c
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

// 함수 프로토타입
void uart_command_init(UART_HandleTypeDef *huart);
void uart_command_task(void);
void uart_send_response(const char *format, ...);
void uart_send_error(int code, const char *message);

#endif /* INC_UART_COMMAND_H_ */
```

### 3.2 uart_command.c

```c
/*
 * uart_command.c
 *
 *  UART 명령 파서 구현
 */

#include "uart_command.h"
#include "command_handler.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

// 전역 변수
static UART_HandleTypeDef *huart_cmd = NULL;
static char uart_rx_buffer[UART_CMD_MAX_LENGTH];
static uint16_t uart_rx_index = 0;
static uint8_t uart_rx_char;

// 초기화
void uart_command_init(UART_HandleTypeDef *huart)
{
    huart_cmd = huart;
    uart_rx_index = 0;
    memset(uart_rx_buffer, 0, sizeof(uart_rx_buffer));

    // 첫 문자 수신 시작 (인터럽트 모드)
    HAL_UART_Receive_IT(huart_cmd, &uart_rx_char, 1);
}

// UART RX 완료 콜백 (stm32h7xx_it.c에서 호출)
void uart_command_rx_callback(void)
{
    // 수신한 문자를 버퍼에 추가
    if (uart_rx_char == '\r' || uart_rx_char == '\n') {
        if (uart_rx_index > 0) {
            // 명령 종료
            uart_rx_buffer[uart_rx_index] = '\0';

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

// 응답 전송
void uart_send_response(const char *format, ...)
{
    char buffer[512];
    va_list args;

    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // UART로 전송
    HAL_UART_Transmit(huart_cmd, (uint8_t*)buffer, strlen(buffer), HAL_MAX_DELAY);
}

// 에러 응답
void uart_send_error(int code, const char *message)
{
    uart_send_response("ERR %d %s\r\n", code, message);
}

// 태스크 (폴링 모드용, 현재는 인터럽트 사용)
void uart_command_task(void)
{
    // 인터럽트 모드에서는 사용 안 함
}
```

---

## 4. Y-MODEM 수신 구현

### 4.1 ymodem.h

```c
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
#define YMODEM_TIMEOUT_MS       10000

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
```

### 4.2 ymodem.c

```c
/*
 * ymodem.c
 *
 *  Y-MODEM 프로토콜 구현
 */

#include "ymodem.h"
#include "uart_command.h"
#include <string.h>

// 내부 함수
static uint16_t crc16(const uint8_t *data, uint16_t length);
static HAL_StatusTypeDef receive_packet(UART_HandleTypeDef *huart, uint8_t *buffer,
                                        uint16_t *length, uint32_t timeout);

// Y-MODEM 수신 (파일 저장)
YmodemResult_t ymodem_receive(UART_HandleTypeDef *huart, const char *file_path)
{
    FIL file;
    FRESULT fres;
    uint8_t packet_buffer[YMODEM_PACKET_SIZE + 5];  // STX + BLK + ~BLK + DATA + CRC
    uint16_t packet_length;
    uint8_t packet_number = 0;
    uint32_t total_bytes = 0;
    YmodemResult_t result = YMODEM_OK;

    // 파일 열기
    fres = f_open(&file, file_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fres != FR_OK) {
        uart_send_error(405, "Failed to create file");
        return YMODEM_ERROR;
    }

    // 첫 번째 패킷 (파일 정보) 요청
    uint8_t start_char = YMODEM_CRC16;
    HAL_UART_Transmit(huart, &start_char, 1, 100);

    // 패킷 수신 루프
    while (1) {
        // 패킷 수신
        HAL_StatusTypeDef status = receive_packet(huart, packet_buffer,
                                                  &packet_length, YMODEM_TIMEOUT_MS);

        if (status != HAL_OK) {
            // 타임아웃 또는 에러
            uint8_t can = YMODEM_CAN;
            HAL_UART_Transmit(huart, &can, 1, 100);
            result = YMODEM_TIMEOUT;
            break;
        }

        uint8_t header = packet_buffer[0];

        if (header == YMODEM_EOT) {
            // 전송 완료
            uint8_t ack = YMODEM_ACK;
            HAL_UART_Transmit(huart, &ack, 1, 100);
            uart_send_response("INFO: Transfer complete (%lu bytes)\r\n", total_bytes);
            result = YMODEM_OK;
            break;
        }
        else if (header == YMODEM_CAN) {
            // 전송 취소
            uart_send_response("INFO: Transfer cancelled by sender\r\n");
            result = YMODEM_CANCELLED;
            break;
        }
        else if (header == YMODEM_SOH || header == YMODEM_STX) {
            // 데이터 패킷
            uint16_t data_size = (header == YMODEM_SOH) ? 128 : 1024;
            uint8_t blk_num = packet_buffer[1];
            uint8_t blk_num_inv = packet_buffer[2];

            // 블록 번호 확인
            if (blk_num != (uint8_t)(~blk_num_inv)) {
                // 블록 번호 오류
                uint8_t nak = YMODEM_NAK;
                HAL_UART_Transmit(huart, &nak, 1, 100);
                continue;
            }

            // CRC 확인
            uint16_t crc_received = (packet_buffer[3 + data_size] << 8) |
                                     packet_buffer[4 + data_size];
            uint16_t crc_calculated = crc16(&packet_buffer[3], data_size);

            if (crc_received != crc_calculated) {
                // CRC 오류
                uint8_t nak = YMODEM_NAK;
                HAL_UART_Transmit(huart, &nak, 1, 100);
                uart_send_response("INFO: CRC error, retrying...\r\n");
                continue;
            }

            // 첫 번째 패킷 (파일 정보)?
            if (packet_number == 0) {
                // 파일 정보 패킷 (무시하고 ACK)
                packet_number = 1;
                uint8_t ack = YMODEM_ACK;
                HAL_UART_Transmit(huart, &ack, 1, 100);

                // 다음 패킷 요청
                uint8_t start_char = YMODEM_CRC16;
                HAL_UART_Transmit(huart, &start_char, 1, 100);
                continue;
            }

            // 데이터 파일에 쓰기
            UINT bytes_written;
            fres = f_write(&file, &packet_buffer[3], data_size, &bytes_written);
            if (fres != FR_OK || bytes_written != data_size) {
                // 쓰기 에러
                uint8_t can = YMODEM_CAN;
                HAL_UART_Transmit(huart, &can, 1, 100);
                uart_send_error(405, "SD write error");
                result = YMODEM_ERROR;
                break;
            }

            total_bytes += bytes_written;
            packet_number++;

            // ACK 전송
            uint8_t ack = YMODEM_ACK;
            HAL_UART_Transmit(huart, &ack, 1, 100);

            // 진행률 출력 (매 100 패킷마다)
            if (packet_number % 100 == 0) {
                uart_send_response("INFO: Receiving... %lu bytes\r\n", total_bytes);
            }
        }
    }

    // 파일 닫기
    f_close(&file);

    return result;
}

// 패킷 수신
static HAL_StatusTypeDef receive_packet(UART_HandleTypeDef *huart, uint8_t *buffer,
                                         uint16_t *length, uint32_t timeout)
{
    uint32_t start_tick = HAL_GetTick();

    // 헤더 수신
    if (HAL_UART_Receive(huart, buffer, 1, timeout) != HAL_OK) {
        return HAL_TIMEOUT;
    }

    uint8_t header = buffer[0];

    if (header == YMODEM_EOT || header == YMODEM_CAN) {
        *length = 1;
        return HAL_OK;
    }

    // 데이터 크기 결정
    uint16_t data_size;
    if (header == YMODEM_SOH) {
        data_size = 128;
    } else if (header == YMODEM_STX) {
        data_size = 1024;
    } else {
        return HAL_ERROR;
    }

    // 나머지 수신: BLK(1) + ~BLK(1) + DATA(128/1024) + CRC(2)
    uint16_t remaining = 1 + 1 + data_size + 2;
    uint32_t elapsed = HAL_GetTick() - start_tick;
    uint32_t remaining_timeout = (timeout > elapsed) ? (timeout - elapsed) : 0;

    if (HAL_UART_Receive(huart, &buffer[1], remaining, remaining_timeout) != HAL_OK) {
        return HAL_TIMEOUT;
    }

    *length = 1 + remaining;
    return HAL_OK;
}

// CRC-16 계산
static uint16_t crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0;

    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;

        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }

    return crc;
}
```

---

## 5. 명령 핸들러 구현

### 5.1 command_handler.h

```c
/*
 * command_handler.h
 *
 *  명령 핸들러 헤더
 */

#ifndef INC_COMMAND_HANDLER_H_
#define INC_COMMAND_HANDLER_H_

#include "uart_command.h"

// 함수 프로토타입
void execute_command(UartCommand_t *cmd);

#endif /* INC_COMMAND_HANDLER_H_ */
```

### 5.2 command_handler.c

```c
/*
 * command_handler.c
 *
 *  명령 핸들러 구현
 */

#include "command_handler.h"
#include "uart_command.h"
#include "ymodem.h"
#include "audio_stream.h"
#include "ff.h"
#include <string.h>
#include <stdlib.h>

extern UART_HandleTypeDef huart2;

// 명령 실행
void execute_command(UartCommand_t *cmd)
{
    // HELLO 명령
    if (strcmp(cmd->command, "HELLO") == 0) {
        uart_send_response("OK AUDIO_MUX v1.00 STM32H723\r\n");
    }

    // STATUS 명령
    else if (strcmp(cmd->command, "STATUS") == 0) {
        uart_send_response("OK STATUS\r\n");
        // TODO: 각 채널 상태 출력
        uart_send_response("CH0: IDLE\r\n");
        uart_send_response("CH1: IDLE\r\n");
        // ... (생략)
    }

    // PLAY 명령
    else if (strcmp(cmd->command, "PLAY") == 0) {
        if (cmd->argc < 2) {
            uart_send_error(401, "Invalid arguments: PLAY requires 2 arguments");
            return;
        }

        int channel = atoi(cmd->argv[0]);
        char *path = cmd->argv[1];

        if (channel < 0 || channel > 5) {
            uart_send_error(402, "Invalid channel (must be 0~5)");
            return;
        }

        // 파일 로드 및 재생
        if (audio_load_file(channel, path, 0) == 0) {
            audio_play(channel);
            uart_send_response("OK Playing ch%d: %s\r\n", channel, path);
        } else {
            uart_send_error(404, "File not found or load failed");
        }
    }

    // STOP 명령
    else if (strcmp(cmd->command, "STOP") == 0) {
        if (cmd->argc < 1) {
            uart_send_error(401, "Invalid arguments: STOP requires 1 argument");
            return;
        }

        int channel = atoi(cmd->argv[0]);

        if (channel < 0 || channel > 5) {
            uart_send_error(402, "Invalid channel (must be 0~5)");
            return;
        }

        audio_stop(channel);
        uart_send_response("OK Stopped ch%d\r\n", channel);
    }

    // STOPALL 명령
    else if (strcmp(cmd->command, "STOPALL") == 0) {
        for (int i = 0; i < 6; i++) {
            audio_stop(i);
        }
        uart_send_response("OK All channels stopped\r\n");
    }

    // VOLUME 명령
    else if (strcmp(cmd->command, "VOLUME") == 0) {
        if (cmd->argc < 2) {
            uart_send_error(401, "Invalid arguments: VOLUME requires 2 arguments");
            return;
        }

        int channel = atoi(cmd->argv[0]);
        int volume = atoi(cmd->argv[1]);

        if (channel < 0 || channel > 5) {
            uart_send_error(402, "Invalid channel (must be 0~5)");
            return;
        }

        if (volume < 0 || volume > 100) {
            uart_send_error(401, "Invalid volume (must be 0~100)");
            return;
        }

        audio_set_volume(channel, volume);
        uart_send_response("OK Volume ch%d: %d\r\n", channel, volume);
    }

    // LS 명령
    else if (strcmp(cmd->command, "LS") == 0) {
        char *path = (cmd->argc > 0) ? cmd->argv[0] : "/audio";

        DIR dir;
        FILINFO fno;
        FRESULT res;

        res = f_opendir(&dir, path);
        if (res != FR_OK) {
            uart_send_error(404, "Directory not found");
            return;
        }

        uart_send_response("OK LS %s\r\n", path);

        while (1) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break;

            if (!(fno.fattrib & AM_DIR)) {
                uart_send_response("%s %luKB\r\n", fno.fname, fno.fsize / 1024);
            }
        }

        uart_send_response("END\r\n");
        f_closedir(&dir);
    }

    // DELETE 명령
    else if (strcmp(cmd->command, "DELETE") == 0) {
        if (cmd->argc < 1) {
            uart_send_error(401, "Invalid arguments: DELETE requires 1 argument");
            return;
        }

        char *path = cmd->argv[0];
        FRESULT res = f_unlink(path);

        if (res == FR_OK) {
            uart_send_response("OK Deleted %s\r\n", path);
        } else {
            uart_send_error(404, "File not found or delete failed");
        }
    }

    // UPLOAD 명령
    else if (strcmp(cmd->command, "UPLOAD") == 0) {
        if (cmd->argc < 2) {
            uart_send_error(401, "Invalid arguments: UPLOAD requires 2 arguments");
            return;
        }

        int channel = atoi(cmd->argv[0]);
        char *filename = cmd->argv[1];

        if (channel < 0 || channel > 5) {
            uart_send_error(402, "Invalid channel (must be 0~5)");
            return;
        }

        // 파일 경로 생성: /audio/ch<N>/<FILENAME>
        char file_path[128];
        snprintf(file_path, sizeof(file_path), "/audio/ch%d/%s", channel, filename);

        // 디렉토리 생성 (없으면)
        char dir_path[64];
        snprintf(dir_path, sizeof(dir_path), "/audio/ch%d", channel);
        f_mkdir(dir_path);

        // Y-MODEM 수신 시작
        uart_send_response("OK Ready for Y-MODEM\r\n");

        YmodemResult_t result = ymodem_receive(&huart2, file_path);

        if (result == YMODEM_OK) {
            uart_send_response("OK Upload complete %s\r\n", file_path);
        } else {
            uart_send_error(501, "Y-MODEM transfer failed");
        }
    }

    // RESET 명령
    else if (strcmp(cmd->command, "RESET") == 0) {
        uart_send_response("OK Resetting...\r\n");
        HAL_Delay(100);
        NVIC_SystemReset();
    }

    // 알 수 없는 명령
    else {
        uart_send_error(400, "Invalid command");
    }
}
```

---

## 6. 통합 가이드

### 6.1 user_def.c 수정

```c
// user_def.c

#include "uart_command.h"

extern UART_HandleTypeDef huart2;

void run_proc(void)
{
    // ... 기존 초기화 코드 ...

    // UART 명령 시스템 초기화
    uart_command_init(&huart2);

    printf("UART command interface ready\r\n");

    // 메인 루프
    while(1)
    {
        // 오디오 스트리밍 태스크
        audio_stream_task();

        // UART 명령 처리 (인터럽트 기반이므로 별도 호출 불필요)

        // LED 토글
        static uint32_t last_led_toggle = 0;
        if ((HAL_GetTick() - last_led_toggle) > 500) {
            last_led_toggle = HAL_GetTick();
            HAL_GPIO_TogglePin(OT_SYS_GPIO_Port, OT_SYS_Pin);
        }
    }
}
```

### 6.2 stm32h7xx_it.c 수정

```c
// stm32h7xx_it.c

extern UART_HandleTypeDef huart2;

// UART2 인터럽트 핸들러
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

// UART RX 완료 콜백
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        // uart_command.c의 콜백 호출
        extern void uart_command_rx_callback(void);
        uart_command_rx_callback();
    }
}
```

### 6.3 Makefile 수정

`Debug/Core/Src/subdir.mk` 파일에 추가:

```makefile
C_SRCS += \
../Core/Src/audio_stream.c \
../Core/Src/command_handler.c \
../Core/Src/main.c \
../Core/Src/spi_protocol.c \
../Core/Src/uart_command.c \
../Core/Src/user_def.c \
../Core/Src/wav_parser.c \
../Core/Src/ymodem.c

OBJS += \
./Core/Src/audio_stream.o \
./Core/Src/command_handler.o \
./Core/Src/main.o \
./Core/Src/spi_protocol.o \
./Core/Src/uart_command.o \
./Core/Src/user_def.o \
./Core/Src/wav_parser.o \
./Core/Src/ymodem.o

C_DEPS += \
./Core/Src/audio_stream.d \
./Core/Src/command_handler.d \
./Core/Src/main.d \
./Core/Src/spi_protocol.d \
./Core/Src/uart_command.d \
./Core/Src/user_def.d \
./Core/Src/wav_parser.d \
./Core/Src/ymodem.d
```

`Debug/objects.list` 파일에 추가:

```
"./Core/Src/command_handler.o"
"./Core/Src/uart_command.o"
"./Core/Src/ymodem.o"
```

---

## 7. 테스트 방법

### 7.1 UART 통신 테스트

시리얼 터미널(TeraTerm, PuTTY 등) 사용:

```
Settings:
- Port: COM3 (장치 관리자에서 확인)
- Baud: 115200
- Data: 8 bit
- Parity: None
- Stop: 1 bit
- Flow control: None
```

### 7.2 명령 테스트 시나리오

```
1. 연결 테스트
   >> HELLO
   << OK AUDIO_MUX v1.00 STM32H723

2. 상태 확인
   >> STATUS
   << OK STATUS
      CH0: IDLE
      ...

3. 파일 목록
   >> LS /audio
   << OK LS /audio
      test.wav 1024KB
      END

4. 재생 테스트
   >> PLAY 0 /audio/test.wav
   << OK Playing ch0: /audio/test.wav

5. 볼륨 조절
   >> VOLUME 0 75
   << OK Volume ch0: 75

6. 정지
   >> STOP 0
   << OK Stopped ch0
```

### 7.3 Y-MODEM 테스트

TeraTerm 사용:

```
1. 업로드 명령
   >> UPLOAD 0 test.wav
   << OK Ready for Y-MODEM

2. TeraTerm 메뉴: File → Transfer → YMODEM → Send
3. 파일 선택: test.wav
4. 전송 시작

5. 전송 완료
   << INFO: Receiving... 102400 bytes
   << INFO: Transfer complete (102400 bytes)
   << OK Upload complete /audio/ch0/test.wav
```

---

## 8. 디버깅 팁

### 8.1 UART 통신 문제

**증상**: 응답이 없음
**해결**:
1. UART2 핀 연결 확인 (TX↔RX 교차)
2. 보드레이트 일치 확인 (115200)
3. `HAL_UART_Receive_IT` 호출 확인
4. 인터럽트 활성화 확인

### 8.2 Y-MODEM 전송 실패

**증상**: 전송 중 타임아웃
**해결**:
1. 블록 크기 확인 (1024 bytes)
2. CRC 모드 확인
3. 타임아웃 증가 (10초 → 30초)
4. PC 프로그램 Y-MODEM 설정 확인

### 8.3 파일 저장 실패

**증상**: SD 쓰기 에러
**해결**:
1. SD 카드 마운트 상태 확인
2. 디스크 용량 확인
3. 파일 경로 유효성 확인
4. FatFs 에러 코드 확인

---

## 부록 A: 메모리 배치

Y-MODEM 버퍼는 크기가 크므로 적절한 메모리 영역에 배치:

```c
// ymodem.c

// RAM_D1_CACHE2 영역 사용 (일반 애플리케이션 데이터)
__attribute__((section(".ram_d1_cache2")))
__attribute__((aligned(32)))
static uint8_t ymodem_packet_buffer[YMODEM_PACKET_SIZE + 5];
```

---

## 부록 B: 확장 가능한 명령 추가

새 명령 추가 방법:

1. `PC_UART_PROTOCOL.md`에 명령 정의
2. `command_handler.c`의 `execute_command()` 함수에 추가:

```c
else if (strcmp(cmd->command, "NEWCMD") == 0) {
    // 인수 확인
    if (cmd->argc < 1) {
        uart_send_error(401, "Invalid arguments");
        return;
    }

    // 명령 처리
    // ...

    uart_send_response("OK Command executed\r\n");
}
```

---

**문서 버전**: 1.0
**최종 수정일**: 2025-10-26
**작성자**: Firmware Engineer
