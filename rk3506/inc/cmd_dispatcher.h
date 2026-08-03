/**
 * @file    cmd_dispatcher.h
 * @brief   命令分发器 — WebSocket/TCP JSON ↔ UART 二进制帧 转换中心 (纯 C)
 */

#ifndef CMD_DISPATCHER_H
#define CMD_DISPATCHER_H

#include "uart_adapter.h"
#include "protocol.h"
#include <stdint.h>
#include <pthread.h>
#include <time.h>

#define DISP_EVENT_QUEUE_MAX  64
#define OLED_FRAME_SIZE       1024
#define OLED_SEG_MAX          8

/* JSON 事件回调 */
typedef void (*disp_json_event_callback_t)(const char* json, void* user_data);

/* 命令分发器 */
typedef struct {
    UartAdapter* uart;
    disp_json_event_callback_t json_cb;
    void*       json_cb_user_data;

    /* 事件队列 */
    char        event_queue[DISP_EVENT_QUEUE_MAX][4096];
    int         event_head;
    int         event_tail;
    int         event_count;
    pthread_mutex_t event_mutex;

    /* 天气模拟状态 */
    int         weather_type;
    int         temperature;
    int         humidity;
    int         wind_dir;

    /* 时间同步 */
    time_t      last_time_sync;

    /* OLED 显存同步重组 */
    uint8_t     oled_frame[OLED_FRAME_SIZE];
    uint8_t     frame_seg_total;
    uint8_t     frame_seg_mask;
    int         frame_ready;

    /* JSON 响应构建锁 (保护静态缓冲区 disp_build_ack 的多线程竞态) */
    pthread_mutex_t json_mutex;
} CmdDispatcher;

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化/销毁 */
void disp_init(CmdDispatcher* d, UartAdapter* uart);
void disp_deinit(CmdDispatcher* d);

/* 下行: WebSocket/TCP JSON → UART 帧, 返回 JSON 响应字符串 */
const char* disp_handle_json(CmdDispatcher* d, const char* json);

/* 上行: UART 帧 → JSON 事件 (注册回调推送到 Web) */
void disp_on_json_event(CmdDispatcher* d, disp_json_event_callback_t cb, void* user_data);

/* 拉取待推送的 JSON 事件, 返回 1 表示有事件, json_buf 至少 512 字节 */
int  disp_poll_event(CmdDispatcher* d, char* json_buf, int buf_size);

/* 时间同步 tick (主循环中每帧调用) */
void disp_tick_time_sync(CmdDispatcher* d);

/* 获取 ACK 响应字符串 (静态缓冲区, 下次调用覆盖) */
const char* disp_build_ack(const char* cmd, int code);

/* 构建事件 JSON (静态缓冲区, 下次调用覆盖) */
const char* disp_build_event(const char* evt, const char* data_json);

/* 构建简单 KV JSON 对象 (静态缓冲区, 下次调用覆盖) */
const char* disp_build_simple_obj(const char* key, const char* val);
const char* disp_build_simple_obj_int(const char* key, int val);

#ifdef __cplusplus
}
#endif

#endif /* CMD_DISPATCHER_H */
