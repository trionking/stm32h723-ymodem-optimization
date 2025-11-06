/*
 * ring_buffer.h
 *
 *  USB CDC용 링 버퍼 (Y-MODEM 지원)
 */

#ifndef INC_RING_BUFFER_H_
#define INC_RING_BUFFER_H_

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

// 링 버퍼 크기 (Y-MODEM 최대 패킷 1024 + 헤더/CRC 포함)
// 32KB: 약 30개 패킷 버퍼링 가능
// Y-MODEM 핸드셰이킹으로 SD write 중 Python 대기 → 오버플로우 없음
#define RING_BUFFER_SIZE  32768

typedef struct {
    uint8_t buffer[RING_BUFFER_SIZE];
    volatile uint32_t head;  // 쓰기 위치
    volatile uint32_t tail;  // 읽기 위치
    volatile uint32_t count; // 저장된 데이터 수
} RingBuffer_t;

// 함수 프로토타입
void ring_buffer_init(RingBuffer_t *rb);
bool ring_buffer_write(RingBuffer_t *rb, uint8_t data);
bool ring_buffer_write_array(RingBuffer_t *rb, const uint8_t *data, uint32_t length);
bool ring_buffer_read(RingBuffer_t *rb, uint8_t *data);
uint32_t ring_buffer_read_array(RingBuffer_t *rb, uint8_t *data, uint32_t length, uint32_t timeout_ms);
uint32_t ring_buffer_available(RingBuffer_t *rb);
void ring_buffer_clear(RingBuffer_t *rb);

// ============================================================================
// UART DMA TX용 Queue 구조체 및 함수 (레퍼런스: stm32h523-spi-hardware-nss-dma)
// ============================================================================

typedef struct Queue
{
    uint8_t *buf;
    uint16_t front;
    uint16_t rear;
    uint16_t buf_size;
} Queue;

#ifdef __cplusplus
extern "C" {
#endif

void InitQueue(Queue *queue, uint16_t q_size);
void flush_queue(Queue *queue);
uint16_t next_q(Queue *queue, uint16_t q_cnt);
int IsFull(Queue *queue);
int IsEmpty(Queue *queue);
void Enqueue(Queue *queue, uint8_t data);
void Enqueue_bytes(Queue *queue, uint8_t *data, uint32_t q_Len);
uint8_t Dequeue(Queue *queue);
void Dequeue_bytes(Queue *src_queue, uint8_t *dst_buff, uint32_t q_Len);
uint8_t Cuqueue(Queue *queue);  // Current Queue data
uint16_t Len_queue(Queue *queue);

#ifdef __cplusplus
}
#endif

#endif /* INC_RING_BUFFER_H_ */
