/**
 * @file    main.c
 * @brief   RK3506 主程序 — OLED 三级联动网关 (纯 C)
 *
 * 编译 (Buildroot / 交叉编译):
 *   arm-linux-gnueabihf-gcc -std=c11 -DMG_ENABLE_HTTP_WEBSOCKET=1 \
 *       -I../inc \
 *       main.c protocol.c uart_adapter.c cmd_dispatcher.c \
 *       web_server.c tcp_server.c mongoose.c -lpthread -o oled_gateway
 */

#include "uart_adapter.h"
#include "cmd_dispatcher.h"
#include "web_server.h"
#include "tcp_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* WebSocket 消息回调 (给 WebServer) */
static const char* web_msg_callback(const char* msg, void* user_data)
{
    CmdDispatcher* disp = (CmdDispatcher*)user_data;
    return disp_handle_json(disp, msg);
}

/* TCP 消息回调 (给 TcpServer) */
static void tcp_msg_callback(const char* msg, int client_id, void* user_data)
{
    CmdDispatcher* disp = (CmdDispatcher*)user_data;
    (void)client_id;
    const char* resp = disp_handle_json(disp, msg);
    /* TCP server 需要通过 tcp_send 发送响应 */
    /* 此处使用全局变量方式, 在 main 中处理 */
    /* 暂存到 disp 内部, 由主循环的 pollEvent 广播 */
    (void)resp;
}

/* 全局 TCP 服务器指针 (用于 tcp_msg_callback 中发送响应) */
static TcpServer* g_tcp = NULL;

static void tcp_msg_callback_with_reply(const char* msg, int client_id, void* user_data)
{
    CmdDispatcher* disp = (CmdDispatcher*)user_data;
    const char* resp = disp_handle_json(disp, msg);
    if (resp && resp[0] && g_tcp) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s\n", resp);
        tcp_send(g_tcp, client_id, buf);
    }
}

int main(int argc, char* argv[])
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    const char* uart_dev = "/dev/ttyS1";  /* 默认串口 */
    int web_port  = 80;
    int tcp_port  = 9527;

    if (argc > 1) uart_dev = argv[1];
    if (argc > 2) web_port = atoi(argv[2]);
    if (argc > 3) tcp_port = atoi(argv[3]);

    printf("=== OLED Gateway v1.0 (C) ===\n");
    printf("UART: %s | Web: %d | TCP: %d\n", uart_dev, web_port, tcp_port);

    /* 1. 初始化串口 */
    UartAdapter uart;
    uart_init(&uart);
    if (uart_open(&uart, uart_dev, 115200) != 0) {
        fprintf(stderr, "[FATAL] Failed to open UART %s\n", uart_dev);
        return -1;
    }
    printf("[UART] Connected to STM32\n");

    /* 2. 命令分发器 */
    CmdDispatcher disp;
    disp_init(&disp, &uart);

    /* 3. Web 服务器 */
    WebServer web;
    web_init(&web);
    web_start(&web, web_port, "./web");
    web_on_message(&web, web_msg_callback, &disp);

    /* 4. TCP 服务器 */
    TcpServer tcp;
    tcp_init(&tcp);
    g_tcp = &tcp;
    tcp_start(&tcp, tcp_port);
    tcp_on_message(&tcp, tcp_msg_callback_with_reply, &disp);

    /* 5. 注册事件回调 (仅用于日志) */
    disp_on_json_event(&disp, NULL, NULL);

    printf("[Main] All services started. Press Ctrl+C to stop.\n");

    /* 6. 主循环 */
    while (g_running) {
        uart_poll(&uart);

        /* Web服务器自带事件循环, 此处处理命令广播 */
        web_poll_events(&web);

        /* 分发 UART 事件到 Web/TCP */
        char evt_json[512];
        while (disp_poll_event(&disp, evt_json, sizeof(evt_json))) {
            web_broadcast(&web, evt_json);
            tcp_broadcast(&tcp, evt_json);
        }

        /* 时间同步 */
        disp_tick_time_sync(&disp);

        usleep(20000); /* 20ms */
    }

    /* 清理 */
    tcp_stop(&tcp);
    tcp_deinit(&tcp);
    web_stop(&web);
    web_deinit(&web);
    uart_close(&uart);
    uart_deinit(&uart);
    disp_deinit(&disp);

    printf("[Main] Gateway stopped.\n");
    return 0;
}
