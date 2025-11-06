/*
 * wav_parser.c
 *
 *  Created on: Oct 26, 2025
 *      Author: Audio Mux Project
 *
 *  WAV 파일 파서 구현
 */

#include "wav_parser.h"
#include <string.h>
#include <stdio.h>

/* 내부 함수 프로토타입 */
static FRESULT find_data_chunk(FIL *fp, uint32_t *data_size, uint32_t *data_offset);

/**
 * @brief  WAV 파일 열기 및 헤더 파싱
 */
FRESULT wav_open(WAV_FileInfo_t *info, const char *filename)
{
    FRESULT res;
    UINT bytes_read;
    WAV_Header_Basic_t header;

    if (info == NULL || filename == NULL) {
        return FR_INVALID_PARAMETER;
    }

    /* 구조체 초기화 */
    memset(info, 0, sizeof(WAV_FileInfo_t));

    /* 파일 열기 */
    res = f_open(&info->file, filename, FA_READ);
    if (res != FR_OK) {
        printf("WAV: Failed to open file %s (error %d)\r\n", filename, res);
        return res;
    }

    /* WAV 헤더 읽기 */
    res = f_read(&info->file, &header, sizeof(WAV_Header_Basic_t), &bytes_read);
    if (res != FR_OK || bytes_read != sizeof(WAV_Header_Basic_t)) {
        printf("WAV: Failed to read header\r\n");
        f_close(&info->file);
        return res;
    }

    /* RIFF 헤더 검증 */
    if (memcmp(header.riff_id, "RIFF", 4) != 0 || memcmp(header.wave_id, "WAVE", 4) != 0) {
        printf("WAV: Invalid RIFF/WAVE header\r\n");
        f_close(&info->file);
        return FR_INVALID_OBJECT;
    }

    /* fmt 청크 검증 */
    if (memcmp(header.fmt_id, "fmt ", 4) != 0) {
        printf("WAV: Invalid fmt chunk\r\n");
        f_close(&info->file);
        return FR_INVALID_OBJECT;
    }

    /* 포맷 확인 (PCM만 지원) */
    if (header.audio_format != 1) {
        printf("WAV: Only PCM format supported (got %d)\r\n", header.audio_format);
        f_close(&info->file);
        return FR_INVALID_OBJECT;
    }

    /* 파일 정보 저장 */
    info->sample_rate = header.sample_rate;
    info->bits_per_sample = header.bits_per_sample;
    info->channels = header.num_channels;

    /* data 청크 찾기 */
    res = find_data_chunk(&info->file, &info->data_size, &info->data_offset);
    if (res != FR_OK) {
        printf("WAV: Failed to find data chunk\r\n");
        f_close(&info->file);
        return res;
    }

    /* 총 샘플 수 계산 */
    uint32_t bytes_per_sample = (info->bits_per_sample + 7) / 8;  // 올림
    info->total_samples = info->data_size / (bytes_per_sample * info->channels);
    info->current_sample = 0;
    info->is_open = 1;

    printf("WAV: Opened %s\r\n", filename);
    printf("  Sample rate: %lu Hz\r\n", info->sample_rate);
    printf("  Bits/sample: %u\r\n", info->bits_per_sample);
    printf("  Channels: %u\r\n", info->channels);
    printf("  Total samples: %lu\r\n", info->total_samples);

    /* 파일 포인터를 데이터 시작 위치로 이동 */
    return f_lseek(&info->file, info->data_offset);
}

/**
 * @brief  data 청크 찾기 (fmt 청크 크기가 다를 수 있으므로)
 */
static FRESULT find_data_chunk(FIL *fp, uint32_t *data_size, uint32_t *data_offset)
{
    FRESULT res;
    UINT bytes_read;
    char chunk_id[4];
    uint32_t chunk_size;
    uint32_t offset = 12;  // RIFF 헤더 이후

    /* RIFF 헤더 이후부터 청크 검색 */
    res = f_lseek(fp, 12);
    if (res != FR_OK) return res;

    while (1) {
        /* 청크 ID 읽기 */
        res = f_read(fp, chunk_id, 4, &bytes_read);
        if (res != FR_OK || bytes_read != 4) {
            return FR_INVALID_OBJECT;
        }

        /* 청크 크기 읽기 */
        res = f_read(fp, &chunk_size, 4, &bytes_read);
        if (res != FR_OK || bytes_read != 4) {
            return FR_INVALID_OBJECT;
        }

        offset += 8;

        /* data 청크 찾음 */
        if (memcmp(chunk_id, "data", 4) == 0) {
            *data_size = chunk_size;
            *data_offset = offset;
            return FR_OK;
        }

        /* 다음 청크로 이동 */
        offset += chunk_size;
        res = f_lseek(fp, offset);
        if (res != FR_OK) {
            return res;
        }

        /* 파일 끝에 도달 */
        if (f_eof(fp)) {
            return FR_INVALID_OBJECT;
        }
    }
}

/**
 * @brief  WAV 파일에서 샘플 읽기 (16비트로 변환)
 */
FRESULT wav_read_samples(WAV_FileInfo_t *info, uint16_t *buffer, uint32_t num_samples, uint32_t *samples_read)
{
    FRESULT res;
    UINT bytes_read;
    uint32_t samples_to_read;

    if (info == NULL || buffer == NULL || samples_read == NULL || !info->is_open) {
        return FR_INVALID_PARAMETER;
    }

    /* 읽을 샘플 수 제한 (파일 끝 확인) */
    samples_to_read = num_samples;
    if (info->current_sample + samples_to_read > info->total_samples) {
        samples_to_read = info->total_samples - info->current_sample;
    }

    if (samples_to_read == 0) {
        *samples_read = 0;
        return FR_OK;  // 파일 끝
    }

    /* 모노 채널만 지원 */
    if (info->channels != 1) {
        printf("WAV: Only mono channel supported\r\n");
        return FR_INVALID_PARAMETER;
    }

    /* 16비트 데이터 직접 읽기 */
    if (info->bits_per_sample == 16) {
        res = f_read(&info->file, buffer, samples_to_read * 2, &bytes_read);
        if (res != FR_OK) {
            return res;
        }
        *samples_read = bytes_read / 2;

        /* 12비트 마스킹 (상위 4비트 제거, 하위 12비트만 사용) */
        for (uint32_t i = 0; i < *samples_read; i++) {
            buffer[i] &= 0x0FFF;  // 12비트 마스킹
        }
    }
    /* 12비트 packed 형식 (3바이트 = 2샘플) - 필요시 추가 */
    else {
        printf("WAV: Unsupported bits per sample: %u\r\n", info->bits_per_sample);
        return FR_INVALID_PARAMETER;
    }

    info->current_sample += *samples_read;
    return FR_OK;
}

/**
 * @brief  WAV 파일 읽기 위치 초기화
 */
FRESULT wav_rewind(WAV_FileInfo_t *info)
{
    if (info == NULL || !info->is_open) {
        return FR_INVALID_PARAMETER;
    }

    info->current_sample = 0;
    return f_lseek(&info->file, info->data_offset);
}

/**
 * @brief  WAV 파일 닫기
 */
FRESULT wav_close(WAV_FileInfo_t *info)
{
    if (info == NULL) {
        return FR_INVALID_PARAMETER;
    }

    if (info->is_open) {
        info->is_open = 0;
        return f_close(&info->file);
    }

    return FR_OK;
}

/**
 * @brief  WAV 파일 정보 검증 (32kHz, 모노)
 */
uint8_t wav_is_valid(WAV_FileInfo_t *info)
{
    if (info == NULL || !info->is_open) {
        return 0;
    }

    /* 샘플레이트 확인 (32kHz) */
    if (info->sample_rate != 32000) {
        printf("WAV: Invalid sample rate %lu (expected 32000)\r\n", info->sample_rate);
        return 0;
    }

    /* 모노 채널 확인 */
    if (info->channels != 1) {
        printf("WAV: Invalid channels %u (expected 1)\r\n", info->channels);
        return 0;
    }

    /* 비트 수 확인 (12 또는 16비트) */
    if (info->bits_per_sample != 12 && info->bits_per_sample != 16) {
        printf("WAV: Invalid bits per sample %u (expected 12 or 16)\r\n", info->bits_per_sample);
        return 0;
    }

    return 1;
}
