/*
 * audio_stream.h
 *
 *  Created on: Oct 26, 2025
 *      Author: Audio Mux Project
 *
 *  멀티채널 오디오 스트리밍 시스템
 */

#ifndef INC_AUDIO_STREAM_H_
#define INC_AUDIO_STREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "wav_parser.h"
#include "spi_protocol.h"

/* 상수 정의 */
#define AUDIO_TOTAL_CHANNELS    6       // 총 채널 수 (3 Slave x 2 DAC)
#define AUDIO_BUFFER_SAMPLES    2048    // 버퍼 크기 (샘플 수)

/* 채널 상태 */
typedef enum {
    CHANNEL_IDLE = 0,       // 유휴 상태
    CHANNEL_LOADING,        // 파일 로딩 중
    CHANNEL_PLAYING,        // 재생 중
    CHANNEL_PAUSED,         // 일시정지
    CHANNEL_STOPPED,        // 정지
    CHANNEL_ERROR           // 에러
} AudioChannelState_t;

/* 오디오 채널 구조체 */
typedef struct {
    uint8_t slave_id;                   // Slave ID (0~2)
    uint8_t dac_channel;                // DAC 채널 (0=DAC1, 1=DAC2)
    AudioChannelState_t state;          // 채널 상태
    WAV_FileInfo_t wav_file;            // WAV 파일 정보
    char filename[64];                  // 파일명
    uint16_t volume;                    // 볼륨 (0~4095, 12비트)
    uint8_t loop;                       // 루프 재생 여부
    uint32_t samples_sent;              // 전송된 샘플 수
    uint32_t last_update_tick;          // 마지막 업데이트 시간
} AudioChannel_t;

/* 함수 프로토타입 */

/**
 * @brief  오디오 스트리밍 시스템 초기화
 * @param  hspi: SPI 핸들 포인터
 * @retval 0: 성공, -1: 실패
 */
int audio_stream_init(SPI_HandleTypeDef *hspi);

/**
 * @brief  채널에 WAV 파일 로드
 * @param  channel_id: 채널 ID (0~5)
 * @param  filename: WAV 파일 경로 (예: "0:/audio/sound1.wav")
 * @param  loop: 루프 재생 여부 (0=1회 재생, 1=루프)
 * @retval 0: 성공, -1: 실패
 */
int audio_load_file(uint8_t channel_id, const char *filename, uint8_t loop);

/**
 * @brief  채널 재생 시작
 * @param  channel_id: 채널 ID (0~5)
 * @retval 0: 성공, -1: 실패
 */
int audio_play(uint8_t channel_id);

/**
 * @brief  채널 재생 정지
 * @param  channel_id: 채널 ID (0~5)
 * @retval 0: 성공, -1: 실패
 */
int audio_stop(uint8_t channel_id);

/**
 * @brief  채널 볼륨 설정
 * @param  channel_id: 채널 ID (0~5)
 * @param  volume: 볼륨 (0~4095, 12비트)
 * @retval 0: 성공, -1: 실패
 */
int audio_set_volume(uint8_t channel_id, uint16_t volume);

/**
 * @brief  모든 채널 정지
 * @retval None
 */
void audio_stop_all(void);

/**
 * @brief  오디오 스트리밍 메인 태스크 (주기적으로 호출)
 * @retval None
 */
void audio_stream_task(void);

/**
 * @brief  채널 상태 조회
 * @param  channel_id: 채널 ID (0~5)
 * @retval 채널 상태
 */
AudioChannelState_t audio_get_state(uint8_t channel_id);

/**
 * @brief  시스템 상태 출력 (디버그용)
 * @retval None
 */
void audio_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* INC_AUDIO_STREAM_H_ */
