/*
 * command_handler.h
 *
 *  명령 핸들러 헤더
 */

#ifndef INC_COMMAND_HANDLER_H_
#define INC_COMMAND_HANDLER_H_

#include "uart_command.h"
#include <stdbool.h>

// Y-MODEM 업로드 요청 구조체
typedef struct {
    bool requested;
    int channel;
    char file_path[128];
} UploadRequest_t;

// 전역 변수 (extern)
extern volatile UploadRequest_t upload_request;

// 함수 프로토타입
void execute_command(UartCommand_t *cmd);
void process_upload_request(void);  // 메인 루프에서 호출
void format_sd_card(void);  // SD 카드 포맷

#endif /* INC_COMMAND_HANDLER_H_ */
