/*
 * ymodem.c
 *
 *  Y-MODEM 프로토콜 구현 (UART 및 USB CDC 지원)
 */

#include "ymodem.h"
#include "uart_command.h"
#include "ansi_colors.h"
#include "usbd_cdc_if.h"  // USB CDC 지원
#include "user_def.h"     // sdmmc1_buffer 사용
#include "bsp_driver_sd.h"  // BSP_SD_GetCardState() 사용
#include <string.h>

// SD 카드 쓰기 최적화 설정
// SD 카드 수명 보호: 8KB 버퍼링 (512 배수 보장)
// Python은 Stop-and-Wait ARQ로 ACK를 30초 대기하므로 링 버퍼 오버플로우 없음
// 실제 측정 결과: 8KB가 32KB보다 빠름 (SD 카드 내부 처리 특성)
#define SD_WRITE_BUFFER_SIZE  8192  // 8KB = 512 * 16

// Y-MODEM 패킷 버퍼 (DMA-safe 메모리에 배치 - SD 카드 MDMA 일관성 보장)
// RAM_D2 사용 (SD MDMA 전용 영역, USB DMA와 공유)
__attribute__((section(".ram_d2")))
__attribute__((aligned(32)))
static uint8_t ymodem_packet_buffer[YMODEM_PACKET_SIZE + 5];  // STX + BLK + ~BLK + DATA + CRC

// 내부 함수
static uint16_t crc16(const uint8_t *data, uint16_t length);
static HAL_StatusTypeDef receive_packet(UART_HandleTypeDef *huart, uint8_t *buffer,
                                        uint16_t *length, uint32_t timeout);
static HAL_StatusTypeDef transmit_byte(UART_HandleTypeDef *huart, uint8_t data);

// Y-MODEM 수신 (파일 저장)
// huart가 NULL이면 USB CDC 사용
YmodemResult_t ymodem_receive(UART_HandleTypeDef *huart, const char *file_path)
{
    FIL file;
    FRESULT fres;
    uint8_t *packet_buffer = ymodem_packet_buffer;  // DMA-safe 글로벌 버퍼 사용
    uint16_t packet_length;
    uint8_t packet_number = 0;
    uint32_t total_bytes = 0;
    YmodemResult_t result = YMODEM_OK;
    bool using_cdc = (huart == NULL);

    // SD 카드 쓰기 버퍼링 (sdmmc1_buffer 재사용, 32KB 크기)
    uint32_t write_buffer_offset = 0;  // 현재 버퍼에 쌓인 데이터 크기

    // Y-MODEM 처리 시작 (UART TX 타이밍 보존을 위해)
    extern volatile uint8_t g_ymodem_active;
    g_ymodem_active = 1;

    // USB CDC 모드 활성화
    if (using_cdc) {
        CDC_Set_YModem_Mode(true);
    }

    // 디렉토리 생성 (파일 경로에서 추출)
    // 예: /audio/ch0/file.wav -> /audio 생성, /audio/ch0 생성
    char dir_path[256];
    strncpy(dir_path, file_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';

    char *last_slash = strrchr(dir_path, '/');
    if (last_slash != NULL && last_slash != dir_path) {
        *last_slash = '\0';  // 파일명 제거

        // 상위 디렉토리 먼저 생성 (/audio)
        char *second_slash = strrchr(dir_path, '/');
        if (second_slash != NULL && second_slash != dir_path) {
            char parent_dir[256];
            strncpy(parent_dir, dir_path, second_slash - dir_path);
            parent_dir[second_slash - dir_path] = '\0';

            FRESULT mkdir_res = f_mkdir(parent_dir);
            if (mkdir_res != FR_OK && mkdir_res != FR_EXIST) {
                printf("[WARN] f_mkdir(%s) failed: fres=%d\r\n", parent_dir, mkdir_res);
            }
        }

        // 하위 디렉토리 생성 (/audio/ch0)
        FRESULT mkdir_res = f_mkdir(dir_path);
        if (mkdir_res != FR_OK && mkdir_res != FR_EXIST) {
            printf("[WARN] f_mkdir(%s) failed: fres=%d\r\n", dir_path, mkdir_res);
        }
    }

    // 파일 열기
    fres = f_open(&file, file_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fres != FR_OK) {
        printf("[ERROR] f_open(%s) failed: fres=%d\r\n", file_path, fres);
        uart_send_error(405, "Failed to create file");
        g_ymodem_active = 0;
        if (using_cdc) CDC_Set_YModem_Mode(false);
        return YMODEM_ERROR;
    }

    // 첫 번째 패킷 (파일 정보) 요청
    // 표준 Y-MODEM 프로토콜: 'C' 문자를 1초마다 재전송 (최대 60회)
    printf("[DEBUG] Y-MODEM: waiting for sender (sending 'C' every 1 sec)...\r\n");

    HAL_StatusTypeDef status = HAL_ERROR;
    for (int retry = 0; retry < 60; retry++) {
        // 'C' 문자 전송
        if (transmit_byte(huart, YMODEM_CRC16) != HAL_OK) {
            printf("[WARN] Y-MODEM: transmit_byte('C') failed, retry=%d\r\n", retry);
        }

        // USB 호스트가 'C'를 읽을 시간 제공
        HAL_Delay(10);

        // 1초 동안 패킷 대기
        status = receive_packet(huart, packet_buffer, &packet_length, 1000);
        if (status == HAL_OK) {
            printf("[DEBUG] Y-MODEM: first packet received after %d retries\r\n", retry);
            break;  // 패킷 받음
        }
    }

    // 60초 타임아웃 체크
    if (status != HAL_OK) {
        f_close(&file);
        transmit_byte(huart, YMODEM_CAN);
        uart_send_error(501, "Y-MODEM timeout waiting for sender");
        g_ymodem_active = 0;
        if (using_cdc) CDC_Set_YModem_Mode(false);
        return YMODEM_TIMEOUT;
    }

    // 첫 번째 패킷 처리 (파일 정보 패킷)
    uint8_t header = packet_buffer[0];

    if (header == YMODEM_SOH || header == YMODEM_STX) {
        // 파일 정보 패킷 검증
        uint16_t data_size = (header == YMODEM_SOH) ? 128 : 1024;
        uint8_t blk_num = packet_buffer[1];
        uint8_t blk_num_inv = packet_buffer[2];

        // 블록 번호 확인 (첫 번째 패킷은 블록 0)
        if (blk_num != 0 || blk_num != (uint8_t)(~blk_num_inv)) {
            f_close(&file);
            transmit_byte(huart, YMODEM_CAN);
            uart_send_error(501, "Y-MODEM invalid file info packet");
            g_ymodem_active = 0;
            if (using_cdc) CDC_Set_YModem_Mode(false);
            return YMODEM_ERROR;
        }

        // CRC 확인
        uint16_t crc_received = (packet_buffer[3 + data_size] << 8) |
                                 packet_buffer[4 + data_size];
        uint16_t crc_calculated = crc16(&packet_buffer[3], data_size);

        if (crc_received != crc_calculated) {
            f_close(&file);
            transmit_byte(huart, YMODEM_CAN);
            uart_send_error(501, "Y-MODEM CRC error in file info packet");
            g_ymodem_active = 0;
            if (using_cdc) CDC_Set_YModem_Mode(false);
            return YMODEM_CRC_ERROR;
        }

        // 파일 정보 패킷 ACK
        transmit_byte(huart, YMODEM_ACK);
        printf("[DEBUG] Y-MODEM: file info packet ACKed\r\n");

        // 주의: 표준 Y-MODEM에서는 여기서 'C'를 보내야 하지만,
        // Python 구현이 'C'를 기다리지 않고 즉시 데이터 패킷을 보내므로
        // 'C'를 보내면 Python이 ACK 대기 시 'C'를 읽어서 타임아웃됨
        // 따라서 'C'를 보내지 않고 ACK만 보냄 (비표준이지만 Python 호환)

        // Python이 ACK를 읽자마자 패킷 1을 보내므로 지연 없이 즉시 수신 시작
        // 지연하면 패킷 1을 놓칠 수 있음!
        printf("[DEBUG] Y-MODEM: starting data reception...\r\n");
        packet_number = 1;
    } else {
        f_close(&file);
        transmit_byte(huart, YMODEM_CAN);
        uart_send_error(501, "Y-MODEM expected file info packet");
        g_ymodem_active = 0;
        if (using_cdc) CDC_Set_YModem_Mode(false);
        return YMODEM_ERROR;
    }

    // 재시도 카운터 초기화
    uint8_t timeout_retries = 0;
    uint8_t nak_retries = 0;

    // 데이터 패킷 수신 루프
    while (1) {
        // 패킷 수신
        status = receive_packet(huart, packet_buffer,
                                &packet_length, YMODEM_TIMEOUT_MS);

        if (status != HAL_OK) {
            // 타임아웃 또는 에러 - 재시도
            timeout_retries++;
            if (timeout_retries >= YMODEM_MAX_TIMEOUT_RETRIES) {
                // 최대 재시도 횟수 초과
                transmit_byte(huart, YMODEM_CAN);
                uart_send_error(501, "Y-MODEM timeout after retries");
                printf("[ERROR] Y-MODEM: timeout after %d retries\r\n", timeout_retries);
                result = YMODEM_TIMEOUT;
                break;
            }
            // 재시도
            printf("[WARN] Packet timeout, retry %d/%d\r\n",
                   timeout_retries, YMODEM_MAX_TIMEOUT_RETRIES);
            uart_send_response(ANSI_YELLOW "INFO:" ANSI_RESET " Timeout, retrying...\r\n");
            HAL_Delay(100);  // 100ms 대기 후 재시도
            continue;
        }

        // 수신 성공 시 타임아웃 카운터 리셋
        timeout_retries = 0;

        uint8_t header = packet_buffer[0];

        if (header == YMODEM_EOT) {
            // 전송 완료 - 남은 버퍼 데이터를 512 배수로 패딩하여 쓰기
            if (write_buffer_offset > 0) {
                // 512 배수로 올림 (SD 카드 최적화)
                uint32_t padded_size = ((write_buffer_offset + 511) / 512) * 512;

                // 패딩 영역을 0으로 채움
                if (padded_size > write_buffer_offset) {
                    memset(&sdmmc1_buffer[write_buffer_offset], 0, padded_size - write_buffer_offset);
                }

                // SD 카드에 마지막 데이터 쓰기
                UINT bytes_written;
                fres = f_write(&file, sdmmc1_buffer, padded_size, &bytes_written);

                if (fres != FR_OK) {
                    printf("[ERROR] Final SD write failed: fres=%d, written=%u/%lu\r\n",
                           fres, bytes_written, padded_size);
                    transmit_byte(huart, YMODEM_CAN);
                    uart_send_error(405, "Final SD write error");
                    result = YMODEM_ERROR;
                    break;
                }

                printf("[DEBUG] Final write: %lu bytes data + %lu bytes padding = %u bytes written\r\n",
                       write_buffer_offset, padded_size - write_buffer_offset, bytes_written);
                write_buffer_offset = 0;
            }

            transmit_byte(huart, YMODEM_ACK);
            uart_send_response(ANSI_GREEN "INFO:" ANSI_RESET " Transfer complete (%lu bytes)\r\n", total_bytes);
            result = YMODEM_OK;
            break;
        }
        else if (header == YMODEM_CAN) {
            // 전송 취소
            uart_send_response(ANSI_YELLOW "INFO:" ANSI_RESET " Transfer cancelled by sender\r\n");
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
                // 블록 번호 오류 - NAK 재시도
                nak_retries++;
                if (nak_retries >= YMODEM_MAX_NAK_RETRIES) {
                    // 최대 NAK 재시도 횟수 초과
                    transmit_byte(huart, YMODEM_CAN);
                    uart_send_error(501, "Too many NAK retries (block number)");
                    printf("[ERROR] Y-MODEM: NAK retries exceeded (%d) - block number mismatch\r\n", nak_retries);
                    result = YMODEM_ERROR;
                    break;
                }
                printf("[ERROR] Block number mismatch: blk=%u, ~blk=%u (NAK retry %d/%d)\r\n",
                       blk_num, blk_num_inv, nak_retries, YMODEM_MAX_NAK_RETRIES);
                transmit_byte(huart, YMODEM_NAK);
                continue;
            }

            // CRC 확인
            uint16_t crc_received = (packet_buffer[3 + data_size] << 8) |
                                     packet_buffer[4 + data_size];
            uint16_t crc_calculated = crc16(&packet_buffer[3], data_size);

            if (crc_received != crc_calculated) {
                // CRC 오류 - NAK 재시도
                nak_retries++;
                if (nak_retries >= YMODEM_MAX_NAK_RETRIES) {
                    // 최대 NAK 재시도 횟수 초과
                    transmit_byte(huart, YMODEM_CAN);
                    uart_send_error(501, "Too many NAK retries (CRC error)");
                    printf("[ERROR] Y-MODEM: NAK retries exceeded (%d) - CRC mismatch\r\n", nak_retries);
                    result = YMODEM_ERROR;
                    break;
                }
                printf("[ERROR] CRC mismatch! received=0x%04X, calculated=0x%04X (NAK retry %d/%d)\r\n",
                       crc_received, crc_calculated, nak_retries, YMODEM_MAX_NAK_RETRIES);
                transmit_byte(huart, YMODEM_NAK);
                uart_send_response(ANSI_YELLOW "INFO:" ANSI_RESET " CRC error, retrying...\r\n");
                continue;
            }

            // 패킷 데이터를 버퍼에 추가 (8KB 버퍼링으로 SD 카드 수명 보호)
            // Python은 Stop-and-Wait ARQ로 ACK를 30초 대기하므로 안전
            memcpy(&sdmmc1_buffer[write_buffer_offset], &packet_buffer[3], data_size);
            write_buffer_offset += data_size;
            total_bytes += data_size;
            packet_number++;

            // 패킷 성공 시 NAK 카운터 리셋
            nak_retries = 0;

            // 버퍼가 8KB 이상 차면 SD 카드에 쓰기 (512 * 16 = 최적 블록 크기)
            uint8_t sd_write_done = 0;
            if (write_buffer_offset >= SD_WRITE_BUFFER_SIZE) {
                UINT bytes_written;
                fres = f_write(&file, sdmmc1_buffer, write_buffer_offset, &bytes_written);

                if (fres != FR_OK || bytes_written != write_buffer_offset) {
                    // 쓰기 에러
                    printf("[ERROR] SD write failed: fres=%d, written=%u/%lu\r\n",
                           fres, bytes_written, write_buffer_offset);
                    transmit_byte(huart, YMODEM_CAN);
                    uart_send_error(405, "SD write error");
                    result = YMODEM_ERROR;
                    break;
                }

                // 버퍼 리셋
                write_buffer_offset = 0;
                sd_write_done = 1;  // SD 쓰기 완료 플래그

                // 1MB마다 f_sync() 호출 (SD 카드 데이터 무결성 보장)
                if (total_bytes % (1024 * 1024) == 0) {
                    fres = f_sync(&file);
                    if (fres != FR_OK) {
                        printf("[WARN] f_sync failed at %lu bytes: fres=%d\r\n", total_bytes, fres);
                    }
                }
            }

            // ACK 전송
            // 패킷 1-7: 버퍼에만 추가 후 즉시 ACK (빠름)
            // 패킷 8: 8KB SD 쓰기 후 상태 확인 + ACK
            HAL_StatusTypeDef ack_status = transmit_byte(huart, YMODEM_ACK);

            // ACK 전송 실패 시에만 로그
            if (ack_status != HAL_OK) {
                printf("[WARN] ACK send failed for packet %d, status=%d\r\n", packet_number, ack_status);
            }

            // SD 쓰기 직후에만 상태 기반 대기
            if (sd_write_done) {
                // SD 카드가 준비될 때까지 대기 (최대 100ms)
                uint32_t wait_start = HAL_GetTick();
                while (BSP_SD_GetCardState() != SD_TRANSFER_OK) {
                    if (HAL_GetTick() - wait_start > 100) {
                        printf("[WARN] SD not ready after 100ms at packet %d\r\n", packet_number);
                        break;
                    }
                    HAL_Delay(1);  // 1ms 폴링
                }
            }

            // 추가 안정화 지연 (USB CDC 핸드셰이킹)
            // 모든 패킷에서 필수 (제거 시 ACK 손실로 타임아웃 발생)
            HAL_Delay(8);

            // 진행 상황 로그 제거 (printf 오버헤드로 인한 타임아웃 방지)
            // if (packet_number % 500 == 0) {
            //     printf("[DEBUG] Progress: packet %d, total=%lu bytes\r\n", packet_number, total_bytes);
            // }

            // 진행률 출력 비활성화 (USB CDC 모드에서 링 버퍼 오염 방지)
            // if (packet_number % 100 == 0) {
            //     uart_send_response(ANSI_CYAN "INFO:" ANSI_RESET " Receiving... %lu bytes\r\n", total_bytes);
            // }
        }
    }

    // 파일 정상 종료: f_sync() 후 f_close() (SD 카드 데이터 무결성 보장)
    fres = f_sync(&file);
    if (fres != FR_OK) {
        printf("[WARN] f_sync before close failed: fres=%d\r\n", fres);
    }

    fres = f_close(&file);
    if (fres != FR_OK) {
        printf("[ERROR] f_close failed: fres=%d\r\n", fres);
    } else {
        printf("[DEBUG] File closed successfully\r\n");
    }

    // Y-MODEM 처리 종료
    g_ymodem_active = 0;

    // USB CDC 모드 해제
    if (using_cdc) {
        CDC_Set_YModem_Mode(false);
    }

    return result;
}

// 패킷 수신
static HAL_StatusTypeDef receive_packet(UART_HandleTypeDef *huart, uint8_t *buffer,
                                         uint16_t *length, uint32_t timeout)
{
    uint32_t start_tick = HAL_GetTick();

    if (huart == NULL) {
        // USB CDC 모드: 링 버퍼에서 읽기
        // 헤더 수신
        uint32_t read = CDC_Read_Data(buffer, 1, timeout);

        if (read == 0) {
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
            printf("[ERROR] receive_packet: invalid header 0x%02X\r\n", header);
            return HAL_ERROR;
        }

        // 나머지 수신: BLK(1) + ~BLK(1) + DATA(128/1024) + CRC(2)
        uint16_t remaining = 1 + 1 + data_size + 2;

        // USB CDC는 64바이트 청크로 전송되므로 충분한 타임아웃 필요
        // 1028바이트 = 약 17개 USB 패킷, SD 쓰기 지연 고려
        uint32_t data_timeout = 5000;  // 5초 (SD 쓰기 지연 + 여유)

        read = CDC_Read_Data(&buffer[1], remaining, data_timeout);

        if (read != remaining) {
            printf("[ERROR] receive_packet: data read failed (expected=%u, got=%lu)\r\n",
                   remaining, read);
            return HAL_TIMEOUT;
        }

        *length = 1 + remaining;
        return HAL_OK;
    } else {
        // UART 모드: 기존 코드
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

// 단일 바이트 전송 (UART 또는 USB CDC)
static HAL_StatusTypeDef transmit_byte(UART_HandleTypeDef *huart, uint8_t data)
{
    if (huart == NULL) {
        // USB CDC 모드 - 이전 전송 완료 대기 후 전송
        extern USBD_HandleTypeDef hUsbDeviceHS;
        extern uint8_t UserTxBufferHS[APP_TX_DATA_SIZE];
        USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceHS.pClassData;

        // TxState가 0이 될 때까지 대기 (이전 전송 완료 대기)
        // 메인 컨텍스트이므로 USB 인터럽트가 정상 처리됨
        uint32_t retry = 0;
        const uint32_t max_retries = 200;  // 최대 2초 대기
        while (hcdc->TxState != 0 && retry < max_retries) {
            retry++;
            HAL_Delay(10);  // 10ms 대기 (USB 콜백이 실행될 시간 제공)
        }

        if (hcdc->TxState != 0) {
            printf("[ERROR] transmit_byte: TX timeout (TxState=%lu after %lums)\r\n",
                   hcdc->TxState, retry * 10);
            return HAL_ERROR;
        }

        // UserTxBufferHS에 복사 (버퍼 수명 문제 해결)
        UserTxBufferHS[0] = data;

        uint8_t result = CDC_Transmit_HS(UserTxBufferHS, 1);

        if (result == USBD_BUSY) {
            printf("[ERROR] transmit_byte: CDC still BUSY (should not happen)\r\n");
            return HAL_ERROR;
        }

        if (result != USBD_OK) {
            printf("[ERROR] transmit_byte: CDC_Transmit_HS failed, result=%d\r\n", result);
            return HAL_ERROR;
        }

        // 전송 완료 대기 (USB는 비동기이므로 완료까지 기다려야 함)
        retry = 0;
        while (hcdc->TxState != 0 && retry < max_retries) {
            retry++;
            HAL_Delay(1);  // 1ms 대기 (USB 전송 완료 확인)
        }

        // USB 전송이 Python까지 도착하는 최소 시간 확보
        // 너무 짧으면 타이밍 이슈, 너무 길면 속도 저하
        if (hcdc->TxState == 0) {
            HAL_Delay(2);  // 2ms 안정화 지연 (USB 전송 완료 확인)
        }

        if (hcdc->TxState != 0) {
            printf("[ERROR] transmit_byte: TX complete timeout (TxState=%lu after %lums)\r\n",
                   hcdc->TxState, retry);
            return HAL_ERROR;
        }

        return HAL_OK;
    } else {
        // UART 모드
        return HAL_UART_Transmit(huart, &data, 1, 500);  // 100 → 500ms
    }
}
