/*
 * ansi_colors.h
 *
 * ANSI 색상 코드 정의
 * UART 출력에 색상을 추가하기 위한 헤더
 *
 * 사용 예:
 *   uart_send_response(ANSI_GREEN "OK" ANSI_RESET "\r\n");
 *   uart_send_response("Status: " ANSI_BOLD ANSI_CYAN "READY" ANSI_RESET "\r\n");
 */

#ifndef INC_ANSI_COLORS_H_
#define INC_ANSI_COLORS_H_

// Reset
#define ANSI_RESET          "\x1b[0m"

// 텍스트 스타일
#define ANSI_BOLD           "\x1b[1m"
#define ANSI_ITALIC         "\x1b[3m"
#define ANSI_UNDERLINE      "\x1b[4m"

// 일반 전경색
#define ANSI_BLACK          "\x1b[30m"
#define ANSI_RED            "\x1b[31m"
#define ANSI_GREEN          "\x1b[32m"
#define ANSI_YELLOW         "\x1b[33m"
#define ANSI_BLUE           "\x1b[34m"
#define ANSI_MAGENTA        "\x1b[35m"
#define ANSI_CYAN           "\x1b[36m"
#define ANSI_WHITE          "\x1b[37m"

// 밝은 전경색
#define ANSI_BRIGHT_BLACK   "\x1b[90m"
#define ANSI_BRIGHT_RED     "\x1b[91m"
#define ANSI_BRIGHT_GREEN   "\x1b[92m"
#define ANSI_BRIGHT_YELLOW  "\x1b[93m"
#define ANSI_BRIGHT_BLUE    "\x1b[94m"
#define ANSI_BRIGHT_MAGENTA "\x1b[95m"
#define ANSI_BRIGHT_CYAN    "\x1b[96m"
#define ANSI_BRIGHT_WHITE   "\x1b[97m"

// 배경색
#define ANSI_BG_BLACK       "\x1b[40m"
#define ANSI_BG_RED         "\x1b[41m"
#define ANSI_BG_GREEN       "\x1b[42m"
#define ANSI_BG_YELLOW      "\x1b[43m"
#define ANSI_BG_BLUE        "\x1b[44m"
#define ANSI_BG_MAGENTA     "\x1b[45m"
#define ANSI_BG_CYAN        "\x1b[46m"
#define ANSI_BG_WHITE       "\x1b[47m"

// 편의 매크로
#define ANSI_OK             ANSI_GREEN "OK" ANSI_RESET
#define ANSI_ERROR          ANSI_RED "ERROR" ANSI_RESET
#define ANSI_WARNING        ANSI_YELLOW "WARNING" ANSI_RESET
#define ANSI_INFO           ANSI_CYAN "INFO" ANSI_RESET

#endif /* INC_ANSI_COLORS_H_ */
