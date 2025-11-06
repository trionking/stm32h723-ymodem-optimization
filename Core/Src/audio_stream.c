/*
 * audio_stream.c
 *
 *  Created on: Oct 26, 2025
 *      Author: Audio Mux Project
 *
 *  멀티채널 오디오 스트리밍 시스템 구현
 */

#include "audio_stream.h"
#include <string.h>
#include <stdio.h>

/* SD 카드 읽기 버퍼 (RAM_D2 - MDMA 사용, 캐시 OFF) */
static uint8_t sd_read_buffer[AUDIO_BUFFER_SAMPLES * 2]
    __attribute__((section(".sd_mdma_buffer")))
    __attribute__((aligned(32)));

/* 샘플 버퍼 (16비트) */
static uint16_t sample_buffer[AUDIO_BUFFER_SAMPLES]
    __attribute__((aligned(32)));

/* 채널 관리 배열 */
static AudioChannel_t channels[AUDIO_TOTAL_CHANNELS];

/* 시스템 상태 */
static uint8_t audio_initialized = 0;

/* 내부 함수 프로토타입 */
static void process_channel(uint8_t channel_id);
static void send_audio_data(uint8_t channel_id);

/**
 * @brief  오디오 스트리밍 시스템 초기화
 */
int audio_stream_init(SPI_HandleTypeDef *hspi)
{
    printf("Audio Stream: Initializing...\r\n");

    /* 채널 구조체 초기화 */
    memset(channels, 0, sizeof(channels));

    /* 각 채널의 Slave ID 및 DAC 채널 설정 */
    for (uint8_t i = 0; i < AUDIO_TOTAL_CHANNELS; i++) {
        channels[i].slave_id = i / SPI_CHANNEL_PER_SLAVE;    // 0,0, 1,1, 2,2
        channels[i].dac_channel = i % SPI_CHANNEL_PER_SLAVE;  // 0,1, 0,1, 0,1
        channels[i].state = CHANNEL_IDLE;
        channels[i].volume = 2048;  // 기본 볼륨 50%
        channels[i].loop = 0;
    }

    /* SPI 프로토콜 초기화 */
    spi_protocol_init(hspi);

    audio_initialized = 1;
    printf("Audio Stream: Initialized successfully\r\n");
    return 0;
}

/**
 * @brief  채널에 WAV 파일 로드
 */
int audio_load_file(uint8_t channel_id, const char *filename, uint8_t loop)
{
    FRESULT res;
    AudioChannel_t *ch;

    if (channel_id >= AUDIO_TOTAL_CHANNELS || !audio_initialized) {
        printf("Audio: Invalid channel ID %d\r\n", channel_id);
        return -1;
    }

    ch = &channels[channel_id];

    /* 기존 파일이 열려있으면 닫기 */
    if (ch->wav_file.is_open) {
        wav_close(&ch->wav_file);
    }

    /* 파일 열기 */
    res = wav_open(&ch->wav_file, filename);
    if (res != FR_OK) {
        printf("Audio: Failed to open file on channel %d\r\n", channel_id);
        ch->state = CHANNEL_ERROR;
        return -1;
    }

    /* 파일 유효성 확인 (32kHz, 모노) */
    if (!wav_is_valid(&ch->wav_file)) {
        printf("Audio: Invalid WAV file on channel %d\r\n", channel_id);
        wav_close(&ch->wav_file);
        ch->state = CHANNEL_ERROR;
        return -1;
    }

    /* 채널 정보 업데이트 */
    strncpy(ch->filename, filename, sizeof(ch->filename) - 1);
    ch->filename[sizeof(ch->filename) - 1] = '\0';
    ch->loop = loop;
    ch->samples_sent = 0;
    ch->state = CHANNEL_STOPPED;

    printf("Audio: Loaded file '%s' on channel %d (Slave%d DAC%d)\r\n",
           filename, channel_id, ch->slave_id, ch->dac_channel);
    return 0;
}

/**
 * @brief  채널 재생 시작
 */
int audio_play(uint8_t channel_id)
{
    AudioChannel_t *ch;
    HAL_StatusTypeDef status;

    if (channel_id >= AUDIO_TOTAL_CHANNELS || !audio_initialized) {
        return -1;
    }

    ch = &channels[channel_id];

    /* 파일이 로드되어 있는지 확인 */
    if (!ch->wav_file.is_open) {
        printf("Audio: No file loaded on channel %d\r\n", channel_id);
        return -1;
    }

    /* 이미 재생 중이면 무시 */
    if (ch->state == CHANNEL_PLAYING) {
        return 0;
    }

    /* 파일 시작 위치로 이동 */
    wav_rewind(&ch->wav_file);

    /* Slave에게 재생 시작 명령 전송 */
    status = spi_send_command(ch->slave_id, ch->dac_channel, SPI_CMD_PLAY, 0);
    if (status != HAL_OK) {
        printf("Audio: Failed to send PLAY command to channel %d\r\n", channel_id);
        return -1;
    }

    /* 볼륨 설정 명령 전송 */
    spi_send_command(ch->slave_id, ch->dac_channel, SPI_CMD_VOLUME, ch->volume);

    ch->state = CHANNEL_PLAYING;
    ch->samples_sent = 0;
    ch->last_update_tick = HAL_GetTick();

    printf("Audio: Playing channel %d\r\n", channel_id);
    return 0;
}

/**
 * @brief  채널 재생 정지
 */
int audio_stop(uint8_t channel_id)
{
    AudioChannel_t *ch;

    if (channel_id >= AUDIO_TOTAL_CHANNELS || !audio_initialized) {
        return -1;
    }

    ch = &channels[channel_id];

    /* Slave에게 정지 명령 전송 */
    spi_send_command(ch->slave_id, ch->dac_channel, SPI_CMD_STOP, 0);

    ch->state = CHANNEL_STOPPED;
    printf("Audio: Stopped channel %d\r\n", channel_id);
    return 0;
}

/**
 * @brief  채널 볼륨 설정
 */
int audio_set_volume(uint8_t channel_id, uint16_t volume)
{
    AudioChannel_t *ch;

    if (channel_id >= AUDIO_TOTAL_CHANNELS || !audio_initialized) {
        return -1;
    }

    /* 볼륨 범위 제한 (12비트) */
    if (volume > 4095) {
        volume = 4095;
    }

    ch = &channels[channel_id];
    ch->volume = volume;

    /* Slave에게 볼륨 명령 전송 */
    spi_send_command(ch->slave_id, ch->dac_channel, SPI_CMD_VOLUME, volume);

    printf("Audio: Set volume=%u on channel %d\r\n", volume, channel_id);
    return 0;
}

/**
 * @brief  모든 채널 정지
 */
void audio_stop_all(void)
{
    for (uint8_t i = 0; i < AUDIO_TOTAL_CHANNELS; i++) {
        if (channels[i].state == CHANNEL_PLAYING) {
            audio_stop(i);
        }
    }
}

/**
 * @brief  오디오 스트리밍 메인 태스크
 */
void audio_stream_task(void)
{
    if (!audio_initialized) {
        return;
    }

    /* 모든 채널 처리 */
    for (uint8_t i = 0; i < AUDIO_TOTAL_CHANNELS; i++) {
        if (channels[i].state == CHANNEL_PLAYING) {
            process_channel(i);
        }
    }
}

/**
 * @brief  개별 채널 처리
 */
static void process_channel(uint8_t channel_id)
{
    AudioChannel_t *ch = &channels[channel_id];

    /* RDY 핀 확인 (Slave가 데이터 수신 준비됨?) */
    if (!spi_check_ready(ch->slave_id)) {
        return;  // 아직 준비 안 됨
    }

    /* 오디오 데이터 전송 */
    send_audio_data(channel_id);
}

/**
 * @brief  오디오 데이터 전송
 */
static void send_audio_data(uint8_t channel_id)
{
    AudioChannel_t *ch = &channels[channel_id];
    FRESULT res;
    uint32_t samples_read;
    HAL_StatusTypeDef status;

    /* WAV 파일에서 샘플 읽기 */
    res = wav_read_samples(&ch->wav_file, sample_buffer, AUDIO_BUFFER_SAMPLES, &samples_read);
    if (res != FR_OK) {
        printf("Audio: Failed to read samples from channel %d\r\n", channel_id);
        ch->state = CHANNEL_ERROR;
        return;
    }

    /* 파일 끝 도달 */
    if (samples_read == 0) {
        if (ch->loop) {
            /* 루프 재생: 파일 처음으로 되돌리기 */
            wav_rewind(&ch->wav_file);
            printf("Audio: Loop channel %d\r\n", channel_id);
        } else {
            /* 1회 재생: 정지 */
            audio_stop(channel_id);
            printf("Audio: End of file on channel %d\r\n", channel_id);
        }
        return;
    }

    /* SPI DMA로 데이터 전송 */
    status = spi_send_data_dma(ch->slave_id, ch->dac_channel, sample_buffer, samples_read);
    if (status != HAL_OK) {
        printf("Audio: Failed to send data on channel %d\r\n", channel_id);
        return;
    }

    /* DMA 완료 대기 (타임아웃 100ms) */
    status = spi_wait_dma_complete(100);
    if (status != HAL_OK) {
        printf("Audio: DMA timeout on channel %d\r\n", channel_id);
        return;
    }

    ch->samples_sent += samples_read;
    ch->last_update_tick = HAL_GetTick();
}

/**
 * @brief  채널 상태 조회
 */
AudioChannelState_t audio_get_state(uint8_t channel_id)
{
    if (channel_id >= AUDIO_TOTAL_CHANNELS) {
        return CHANNEL_ERROR;
    }
    return channels[channel_id].state;
}

/**
 * @brief  시스템 상태 출력 (디버그용)
 */
void audio_print_status(void)
{
    printf("\r\n===== Audio Stream Status =====\r\n");
    for (uint8_t i = 0; i < AUDIO_TOTAL_CHANNELS; i++) {
        AudioChannel_t *ch = &channels[i];
        const char *state_str[] = {"IDLE", "LOADING", "PLAYING", "PAUSED", "STOPPED", "ERROR"};

        printf("CH%d (Slave%d DAC%d): %s", i, ch->slave_id, ch->dac_channel, state_str[ch->state]);
        if (ch->wav_file.is_open) {
            printf(" | File: %s", ch->filename);
            printf(" | Vol: %u", ch->volume);
            printf(" | Samples: %lu/%lu", ch->samples_sent, ch->wav_file.total_samples);
        }
        printf("\r\n");
    }
    printf("================================\r\n");
}
