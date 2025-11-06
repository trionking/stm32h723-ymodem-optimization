/*
 * wav_parser.h
 *
 *  Created on: Oct 26, 2025
 *      Author: Audio Mux Project
 *
 *  WAV 파일 파서 - 12비트 모노 WAV 파일 읽기 및 16비트로 변환
 */

#ifndef INC_WAV_PARSER_H_
#define INC_WAV_PARSER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ff.h"
#include <stdint.h>

/* WAV 파일 정보 구조체 */
typedef struct {
    FIL file;                   // FatFs 파일 핸들
    uint32_t sample_rate;       // 샘플레이트 (Hz)
    uint16_t bits_per_sample;   // 비트 수 (12 또는 16)
    uint16_t channels;          // 채널 수 (1=모노, 2=스테레오)
    uint32_t data_size;         // 데이터 크기 (바이트)
    uint32_t data_offset;       // 데이터 시작 오프셋
    uint32_t total_samples;     // 총 샘플 수
    uint32_t current_sample;    // 현재 읽기 위치
    uint8_t is_open;            // 파일 열림 상태
} WAV_FileInfo_t;

/* WAV 헤더 구조체 (표준 RIFF WAV) */
typedef struct __attribute__((packed)) {
    /* RIFF 청크 */
    char riff_id[4];            // "RIFF"
    uint32_t riff_size;         // 파일 크기 - 8
    char wave_id[4];            // "WAVE"

    /* fmt 청크 */
    char fmt_id[4];             // "fmt "
    uint32_t fmt_size;          // fmt 청크 크기 (16)
    uint16_t audio_format;      // 오디오 포맷 (1=PCM)
    uint16_t num_channels;      // 채널 수
    uint32_t sample_rate;       // 샘플레이트
    uint32_t byte_rate;         // 바이트레이트
    uint16_t block_align;       // 블록 정렬
    uint16_t bits_per_sample;   // 비트/샘플
} WAV_Header_Basic_t;

/* 함수 프로토타입 */

/**
 * @brief  WAV 파일 열기 및 헤더 파싱
 * @param  info: WAV 파일 정보 구조체 포인터
 * @param  filename: 파일 경로 (예: "0:/audio/sound1.wav")
 * @retval FR_OK: 성공, 기타: FatFs 에러 코드
 */
FRESULT wav_open(WAV_FileInfo_t *info, const char *filename);

/**
 * @brief  WAV 파일에서 샘플 읽기 (16비트로 변환)
 * @param  info: WAV 파일 정보 구조체 포인터
 * @param  buffer: 출력 버퍼 (16비트 샘플 배열)
 * @param  num_samples: 읽을 샘플 수
 * @param  samples_read: 실제 읽은 샘플 수 (출력)
 * @retval FR_OK: 성공, 기타: FatFs 에러 코드
 */
FRESULT wav_read_samples(WAV_FileInfo_t *info, uint16_t *buffer, uint32_t num_samples, uint32_t *samples_read);

/**
 * @brief  WAV 파일 읽기 위치 초기화 (처음으로 되돌리기)
 * @param  info: WAV 파일 정보 구조체 포인터
 * @retval FR_OK: 성공, 기타: FatFs 에러 코드
 */
FRESULT wav_rewind(WAV_FileInfo_t *info);

/**
 * @brief  WAV 파일 닫기
 * @param  info: WAV 파일 정보 구조체 포인터
 * @retval FR_OK: 성공, 기타: FatFs 에러 코드
 */
FRESULT wav_close(WAV_FileInfo_t *info);

/**
 * @brief  WAV 파일 정보 검증 (32kHz, 모노 확인)
 * @param  info: WAV 파일 정보 구조체 포인터
 * @retval 0: 유효하지 않음, 1: 유효함
 */
uint8_t wav_is_valid(WAV_FileInfo_t *info);

#ifdef __cplusplus
}
#endif

#endif /* INC_WAV_PARSER_H_ */
