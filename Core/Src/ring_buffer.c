/*
 * ring_buffer.c
 *
 *  USB CDC용 링 버퍼 구현
 */

#include "ring_buffer.h"

// 링 버퍼 초기화
void ring_buffer_init(RingBuffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

// 단일 바이트 쓰기
bool ring_buffer_write(RingBuffer_t *rb, uint8_t data)
{
    if (rb->count >= RING_BUFFER_SIZE) {
        return false;  // 버퍼 가득 찼음
    }

    rb->buffer[rb->head] = data;
    rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
    rb->count++;

    return true;
}

// 배열 쓰기
bool ring_buffer_write_array(RingBuffer_t *rb, const uint8_t *data, uint32_t length)
{
    if (rb->count + length > RING_BUFFER_SIZE) {
        return false;  // 공간 부족
    }

    for (uint32_t i = 0; i < length; i++) {
        rb->buffer[rb->head] = data[i];
        rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
        rb->count++;
    }

    return true;
}

// 단일 바이트 읽기
bool ring_buffer_read(RingBuffer_t *rb, uint8_t *data)
{
    if (rb->count == 0) {
        return false;  // 버퍼 비어있음
    }

    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
    rb->count--;

    return true;
}

// 배열 읽기 (타임아웃 지원)
uint32_t ring_buffer_read_array(RingBuffer_t *rb, uint8_t *data, uint32_t length, uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();
    uint32_t read_count = 0;

    // UART2 DMA TX 처리 함수 (외부 함수)
    extern void UART2_Process_TX_Queue(void);
    extern volatile uint8_t g_ymodem_active;

    while (read_count < length) {
        // 타임아웃 체크
        if (HAL_GetTick() - start_tick > timeout_ms) {
            break;  // 타임아웃
        }

        // 데이터 읽기
        if (rb->count > 0) {
            data[read_count] = rb->buffer[rb->tail];
            rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
            rb->count--;
            read_count++;
        } else {
            // 데이터가 없으면 대기 (USB 인터럽트 실행 시간 제공)
            // USB CDC는 1ms 폴링 주기이므로 1ms 대기
            HAL_Delay(1);

            // Y-MODEM 처리 중에는 타이밍을 보존하기 위해 UART TX 큐 처리 안 함
            // (YMODEM_UPLOAD_GUIDE.md: 정밀한 10ms 타이밍 필요)
            if (!g_ymodem_active) {
                // 일반 동작 중에만 UART2 DMA TX 큐 처리 (printf 출력 유지)
                UART2_Process_TX_Queue();
            }
        }
    }

    return read_count;
}

// 사용 가능한 데이터 수
uint32_t ring_buffer_available(RingBuffer_t *rb)
{
    return rb->count;
}

// 버퍼 클리어
void ring_buffer_clear(RingBuffer_t *rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->count = 0;
}

// ============================================================================
// UART DMA TX용 Queue 구현 (레퍼런스: stm32h523-spi-hardware-nss-dma)
// ============================================================================

#include <stdlib.h>

void InitQueue(Queue *queue, uint16_t q_size)
{
    queue->buf_size = q_size;
    queue->buf = malloc(q_size + 10);
    queue->front = queue->rear = 0;
}

void flush_queue(Queue *queue)
{
    queue->front = queue->rear = 0;
}

uint16_t next_q(Queue *queue, uint16_t q_cnt)
{
    return ((q_cnt + 1) % (queue->buf_size));
}

int IsFull(Queue *queue)
{
    return (next_q(queue, queue->rear) == queue->front);
}

int IsEmpty(Queue *queue)
{
    return (queue->front == queue->rear);
}

void Enqueue(Queue *queue, uint8_t data)
{
    uint8_t dummy;

    (void)dummy;

    if (IsFull(queue))
    {
        dummy = Dequeue(queue);
    }
    queue->buf[queue->rear] = data;
    queue->rear = next_q(queue, queue->rear);
}

void Enqueue_bytes(Queue *queue, uint8_t *data, uint32_t q_Len)
{
    for(uint32_t i = 0; q_Len > 0; q_Len--, i++)
    {
        Enqueue(queue, data[i]);
    }
}

uint8_t Dequeue(Queue *queue)
{
    uint8_t re = 0;
    if (IsEmpty(queue))
    {
        return re;
    }
    re = queue->buf[queue->front];
    queue->front = next_q(queue, queue->front);
    return re;
}

void Dequeue_bytes(Queue *src_queue, uint8_t *dst_buff, uint32_t q_Len)
{
    for(uint32_t i = 0; q_Len > 0; q_Len--)
    {
        dst_buff[i++] = Dequeue(src_queue);
    }
}

uint8_t Cuqueue(Queue *queue)  // Current Queue data
{
    uint8_t re = 0;
    if (IsEmpty(queue))
    {
        return re;
    }
    re = queue->buf[queue->front];
    return re;
}

uint16_t Len_queue(Queue *queue)
{
    return ((queue->buf_size - queue->front + queue->rear) % (queue->buf_size));
}
