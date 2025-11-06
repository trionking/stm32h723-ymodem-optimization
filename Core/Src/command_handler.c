/*
 * command_handler.c
 *
 *  명령 핸들러 구현
 */

#include "command_handler.h"
#include "uart_command.h"
#include "ymodem.h"
#include "audio_stream.h"
#include "ansi_colors.h"
#include "user_def.h"
#include "ff.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern UART_HandleTypeDef huart2;
extern FATFS SDFatFS;  // FatFs 파일 시스템 객체
extern char SDPath[4];  // SD 카드 경로 ("0:")

// Y-MODEM 업로드 요청 전역 변수
volatile UploadRequest_t upload_request = {0};

// Helper function to convert audio state to string
static const char* get_state_string(AudioChannelState_t state)
{
    switch (state) {
        case CHANNEL_IDLE:    return "IDLE";
        case CHANNEL_LOADING: return "LOADING";
        case CHANNEL_PLAYING: return "PLAYING";
        case CHANNEL_PAUSED:  return "PAUSED";
        case CHANNEL_STOPPED: return "STOPPED";
        case CHANNEL_ERROR:   return "ERROR";
        default:              return "UNKNOWN";
    }
}

// 명령 실행
void execute_command(UartCommand_t *cmd)
{
    // HELLO 명령
    if (strcmp(cmd->command, "HELLO") == 0) {
        uart_send_response(ANSI_OK " AUDIO_MUX v1.00 STM32H723\r\n");
    }

    // STATUS 명령
    else if (strcmp(cmd->command, "STATUS") == 0) {
        // 모든 응답을 하나의 버퍼에 모아서 한 번에 전송
        char response[512];
        int offset = 0;

        offset += snprintf(response + offset, sizeof(response) - offset,
                          ANSI_OK " STATUS\r\n");

        // 각 채널 상태 출력
        for (int ch = 0; ch < 6; ch++) {
            AudioChannelState_t state = audio_get_state(ch);
            const char *state_str = get_state_string(state);
            offset += snprintf(response + offset, sizeof(response) - offset,
                             "CH%d: %s\r\n", ch, state_str);
        }

        offset += snprintf(response + offset, sizeof(response) - offset, "END\r\n");

        // 한 번에 전송
        uart_send_response("%s", response);
    }

    // PLAY 명령
    else if (strcmp(cmd->command, "PLAY") == 0) {
        if (cmd->argc < 2) {
            uart_send_error(401, "Invalid arguments: PLAY requires 2 arguments");
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

        // 파일 로드 및 재생
        if (audio_load_file(channel, file_path, 0) == 0) {
            audio_play(channel);
            uart_send_response(ANSI_OK " Playing ch%d: %s\r\n", channel, filename);
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
        uart_send_response(ANSI_OK " Stopped ch%d\r\n", channel);
    }

    // STOPALL 명령
    else if (strcmp(cmd->command, "STOPALL") == 0) {
        for (int i = 0; i < 6; i++) {
            audio_stop(i);
        }
        uart_send_response(ANSI_OK " All channels stopped\r\n");
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
        uart_send_response(ANSI_OK " Volume ch%d: %d\r\n", channel, volume);
    }

    // LS 명령
    else if (strcmp(cmd->command, "LS") == 0) {
        // argc가 0이면 /audio, 아니면 /audio/ch<N>
        char path[64];

        if (cmd->argc > 0) {
            int channel = atoi(cmd->argv[0]);
            if (channel < 0 || channel > 5) {
                uart_send_error(402, "Invalid channel (must be 0~5)");
                return;
            }
            snprintf(path, sizeof(path), "/audio/ch%d", channel);
        } else {
            snprintf(path, sizeof(path), "/audio");
        }

        DIR dir;
        FILINFO fno;
        FRESULT res;

        res = f_opendir(&dir, path);
        if (res != FR_OK) {
            uart_send_error(404, "Directory not found");
            return;
        }

        // 모든 응답을 하나의 버퍼에 모아서 한 번에 전송
        char response[1024];
        int offset = 0;

        offset += snprintf(response + offset, sizeof(response) - offset,
                          ANSI_OK " LS %s\r\n", path);

        while (1) {
            res = f_readdir(&dir, &fno);
            if (res != FR_OK || fno.fname[0] == 0) break;

            if (!(fno.fattrib & AM_DIR)) {
                offset += snprintf(response + offset, sizeof(response) - offset,
                                 "%s %luKB\r\n", fno.fname, fno.fsize / 1024);

                // 버퍼 오버플로우 방지
                if (offset >= sizeof(response) - 64) break;
            }
        }

        offset += snprintf(response + offset, sizeof(response) - offset, "END\r\n");

        f_closedir(&dir);

        // 한 번에 전송
        uart_send_response("%s", response);
    }

    // DELETE 명령
    else if (strcmp(cmd->command, "DELETE") == 0) {
        if (cmd->argc < 2) {
            uart_send_error(401, "Invalid arguments: DELETE requires 2 arguments");
            return;
        }

        int channel = atoi(cmd->argv[0]);
        char *filename = cmd->argv[1];

        if (channel < 0 || channel > 5) {
            uart_send_error(402, "Invalid channel (must be 0~5)");
            return;
        }

        // 파일 경로 생성
        char file_path[128];
        snprintf(file_path, sizeof(file_path), "/audio/ch%d/%s", channel, filename);

        FRESULT res = f_unlink(file_path);

        if (res == FR_OK) {
            uart_send_response(ANSI_OK " Deleted %s\r\n", file_path);
        } else {
            uart_send_error(404, "File not found or delete failed");
        }
    }

    // UPLOAD 명령
    else if (strcmp(cmd->command, "UPLOAD") == 0) {
        printf("[DEBUG] UPLOAD command received\r\n");

        if (cmd->argc < 2) {
            printf("[DEBUG] UPLOAD: insufficient arguments (argc=%d)\r\n", cmd->argc);
            uart_send_error(401, "Invalid arguments: UPLOAD requires 2 arguments");
            return;
        }

        int channel = atoi(cmd->argv[0]);
        char *filename = cmd->argv[1];

        printf("[DEBUG] UPLOAD: ch=%d, file=%s\r\n", channel, filename);

        if (channel < 0 || channel > 5) {
            printf("[DEBUG] UPLOAD: invalid channel %d\r\n", channel);
            uart_send_error(402, "Invalid channel (must be 0~5)");
            return;
        }

        // 디렉토리 생성 (없으면)
        char dir_path[64];
        snprintf(dir_path, sizeof(dir_path), "/audio");
        f_mkdir(dir_path);
        snprintf(dir_path, sizeof(dir_path), "/audio/ch%d", channel);
        f_mkdir(dir_path);

        // 파일 경로 생성 및 저장
        snprintf((char*)upload_request.file_path, sizeof(upload_request.file_path),
                 "/audio/ch%d/%s", channel, filename);
        upload_request.channel = channel;

        printf("[DEBUG] UPLOAD: sending Ready response\r\n");

        // Y-MODEM 준비 완료 응답 (인터럽트 핸들러에서는 여기까지만)
        uart_send_response(ANSI_OK " Ready for Y-MODEM\r\n");

        // 플래그 설정 (메인 루프에서 처리)
        upload_request.requested = true;

        printf("[DEBUG] UPLOAD: request queued, will process in main loop\r\n");
    }

    // RESET 명령
    else if (strcmp(cmd->command, "RESET") == 0) {
        uart_send_response(ANSI_OK " Resetting...\r\n");
        HAL_Delay(100);
        NVIC_SystemReset();
    }

    // FORMAT 명령 (SD 카드 포맷)
    else if (strcmp(cmd->command, "FORMAT") == 0) {
        format_sd_card();
    }

    // TEST1 명령 (SPI 명령 패킷 테스트)
    else if (strcmp(cmd->command, "TEST1") == 0) {
        uart_send_response(ANSI_OK " Starting SPI Command Packet Test\r\n");
        extern void test_spi_command_packets(void);
        test_spi_command_packets();
        uart_send_response(ANSI_OK " Test complete\r\n");
    }

    // TEST2 명령 (SPI 데이터 패킷 테스트)
    else if (strcmp(cmd->command, "TEST2") == 0) {
        uart_send_response(ANSI_OK " Starting SPI Data Packet Test\r\n");
        extern void test_spi_data_packets(void);
        test_spi_data_packets();
        uart_send_response(ANSI_OK " Test complete\r\n");
    }

    // TEST3 명령 (SPI 연속 전송 테스트)
    else if (strcmp(cmd->command, "TEST3") == 0) {
        uart_send_response(ANSI_OK " Starting SPI Continuous Transmission Test\r\n");
        extern void test_spi_continuous_transmission(void);
        test_spi_continuous_transmission();
        uart_send_response(ANSI_OK " Test complete\r\n");
    }

    // SPITEST 명령 (SPI 통신 테스트)
    else if (strcmp(cmd->command, "SPITEST") == 0) {
        if (cmd->argc < 1) {
            uart_send_error(401, "Invalid arguments: SPITEST requires test type");
            uart_send_response("Usage: SPITEST <type> [slave_id] [channel]\r\n");
            uart_send_response("  BASIC <slave_id>           : Basic command test\r\n");
            uart_send_response("  DATA <slave_id>            : Data transfer test\r\n");
            uart_send_response("  MULTI                      : Multi-slave test\r\n");
            uart_send_response("  ERROR <slave_id>           : Error handling test\r\n");
            uart_send_response("  ALL <slave_id>             : Run all tests\r\n");
            uart_send_response("  PLAY <slave_id> <channel>  : Send PLAY command\r\n");
            uart_send_response("  STOP <slave_id> <channel>  : Send STOP command\r\n");
            return;
        }

        char *test_type = cmd->argv[0];

        // MULTI 테스트는 slave_id 불필요
        if (strcmp(test_type, "MULTI") == 0) {
            uart_send_response(ANSI_OK " Starting Multi-Slave Test\r\n");
            spi_test_multi_slave();
            uart_send_response(ANSI_OK " Test complete\r\n");
            return;
        }

        // PLAY, STOP 명령은 slave_id와 channel 필요
        if (strcmp(test_type, "PLAY") == 0 || strcmp(test_type, "STOP") == 0) {
            if (cmd->argc < 3) {
                uart_send_error(401, "Invalid arguments: requires slave_id and channel");
                uart_send_response("Usage: SPITEST %s <slave_id> <channel>\r\n", test_type);
                return;
            }

            int slave_id = atoi(cmd->argv[1]);
            int channel = atoi(cmd->argv[2]);

            if (slave_id < 0 || slave_id > 2) {
                uart_send_error(402, "Invalid slave_id (must be 0~2)");
                return;
            }

            if (channel < 0 || channel > 1) {
                uart_send_error(403, "Invalid channel (must be 0 or 1)");
                return;
            }

            if (strcmp(test_type, "PLAY") == 0) {
                spi_test_play(slave_id, channel);
            } else {
                spi_test_stop(slave_id, channel);
            }
            return;
        }

        // 나머지 테스트는 slave_id 필수
        if (cmd->argc < 2) {
            uart_send_error(401, "Invalid arguments: slave_id required");
            return;
        }

        int slave_id = atoi(cmd->argv[1]);
        if (slave_id < 0 || slave_id > 2) {
            uart_send_error(402, "Invalid slave_id (must be 0~2)");
            return;
        }

        if (strcmp(test_type, "BASIC") == 0) {
            uart_send_response(ANSI_OK " Starting Basic Test (Slave %d)\r\n", slave_id);
            spi_test_basic(slave_id);
            uart_send_response(ANSI_OK " Test complete\r\n");
        }
        else if (strcmp(test_type, "DATA") == 0) {
            uart_send_response(ANSI_OK " Starting Data Transfer Test (Slave %d)\r\n", slave_id);
            spi_test_data(slave_id);
            uart_send_response(ANSI_OK " Test complete\r\n");
        }
        else if (strcmp(test_type, "ERROR") == 0) {
            uart_send_response(ANSI_OK " Starting Error Test (Slave %d)\r\n", slave_id);
            spi_test_error(slave_id);
            uart_send_response(ANSI_OK " Test complete\r\n");
        }
        else if (strcmp(test_type, "ALL") == 0) {
            uart_send_response(ANSI_OK " Starting Full Test Suite (Slave %d)\r\n", slave_id);
            spi_test_all(slave_id);
            uart_send_response(ANSI_OK " All tests complete\r\n");
        }
        else {
            uart_send_error(400, "Invalid test type");
            uart_send_response("Valid types: BASIC, DATA, MULTI, ERROR, ALL, PLAY, STOP\r\n");
        }
    }

    // 알 수 없는 명령
    else {
        uart_send_error(400, "Invalid command");
    }
}

/**
 * @brief  메인 루프에서 호출하여 Y-MODEM 업로드 요청 처리
 *         인터럽트 핸들러가 아닌 메인 컨텍스트에서 실행되므로
 *         USB CDC 전송 완료 인터럽트가 정상 처리됨
 */
void process_upload_request(void)
{
    if (!upload_request.requested) {
        return;  // 요청 없음
    }

    // 플래그 클리어 (재진입 방지)
    upload_request.requested = false;

    printf("[DEBUG] Processing upload request in main loop\r\n");

    // USB CDC 전송 완료 대기 (메인 컨텍스트이므로 HAL_Delay 안전)
    HAL_Delay(200);

    printf("[DEBUG] Starting Y-MODEM receive to %s\r\n", upload_request.file_path);

    // Y-MODEM 모드 활성화 (CDC 또는 UART)
    UART_HandleTypeDef *huart = (get_command_transport() == CMD_TRANSPORT_USB_CDC) ? NULL : &huart2;
    YmodemResult_t result = ymodem_receive(huart, (const char*)upload_request.file_path);

    if (result == YMODEM_OK) {
        printf("[DEBUG] Y-MODEM upload complete\r\n");
        uart_send_response(ANSI_OK " Upload complete %s\r\n", upload_request.file_path);
    } else {
        printf("[DEBUG] Y-MODEM upload failed, result=%d\r\n", result);
        uart_send_error(501, "Y-MODEM transfer failed");
    }
}

/**
 * @brief SD 카드 포맷 함수
 *
 * FAT32 파일 시스템으로 포맷합니다.
 * 주의: 모든 데이터가 삭제됩니다!
 */
void format_sd_card(void)
{
    printf(ANSI_YELLOW "[INFO]" ANSI_RESET " Formatting SD card...\r\n");
    printf(ANSI_YELLOW "[WARN]" ANSI_RESET " All data will be erased!\r\n");

    // 작업 버퍼 (클러스터 크기에 따라 필요)
    // 32KB 버퍼 할당 (스택에서 큰 버퍼는 위험하므로 static 또는 extern 사용)
    extern uint8_t sdmmc1_buffer[32768];  // user_def.c에서 선언된 DMA 버퍼 재사용

    // 포맷 옵션 (이전 버전 FatFs API 사용)
    // opt: FM_FAT32 (0x02) = FAT32 포맷
    // au: 0 = 자동 클러스터 크기
    BYTE opt = FM_FAT32;  // FAT32 포맷
    DWORD au = 0;         // 자동 클러스터 크기

    // SD 카드 포맷 실행
    FRESULT fres = f_mkfs("0:", opt, au, sdmmc1_buffer, sizeof(sdmmc1_buffer));

    if (fres == FR_OK) {
        printf(ANSI_GREEN "[INFO]" ANSI_RESET " SD card formatted successfully (FAT32)\r\n");

        // 포맷 후 재마운트
        fres = f_mount(&SDFatFS, SDPath, 1);
        if (fres == FR_OK) {
            printf(ANSI_GREEN "[INFO]" ANSI_RESET " SD card remounted successfully\r\n");
            uart_send_response(ANSI_OK " SD card formatted (FAT32)\r\n");
        } else {
            printf(ANSI_RED "[ERROR]" ANSI_RESET " Failed to remount SD card: fres=%d\r\n", fres);
            uart_send_error(500, "SD remount failed after format");
        }
    } else {
        printf(ANSI_RED "[ERROR]" ANSI_RESET " SD card format failed: fres=%d\r\n", fres);
        uart_send_error(500, "SD format failed");

        // 에러 코드 설명
        switch (fres) {
            case FR_DISK_ERR:
                printf("  -> FR_DISK_ERR: Disk I/O error\r\n");
                break;
            case FR_NOT_READY:
                printf("  -> FR_NOT_READY: SD card not ready\r\n");
                break;
            case FR_WRITE_PROTECTED:
                printf("  -> FR_WRITE_PROTECTED: SD card is write protected\r\n");
                break;
            case FR_INVALID_PARAMETER:
                printf("  -> FR_INVALID_PARAMETER: Invalid parameter\r\n");
                break;
            case FR_MKFS_ABORTED:
                printf("  -> FR_MKFS_ABORTED: Format aborted\r\n");
                break;
            default:
                printf("  -> Unknown error code: %d\r\n", fres);
                break;
        }
    }
}
