/**
 * @file    main.cpp
 * @brief   RK3506 主程序 — OLED 三级联动网关
 *
 * 编译 (Buildroot / 交叉编译):
 *   arm-linux-gnueabihf-g++ -std=c++11 -DMG_ENABLE_HTTP_WEBSOCKET=1 \
 *       -I../inc \
 *       main.cpp protocol.cpp uart_adapter.cpp cmd_dispatcher.cpp \
 *       web_server.cpp tcp_server.cpp mongoose.c -lpthread -o oled_gateway
 */

#include "uart_adapter.h"
#include "cmd_dispatcher.h"
#include "web_server.h"
#include "tcp_server.h"
#include <iostream>
#include <csignal>
#include <string>
#include <thread>
#include <chrono>

static volatile int g_running = 1;

void signal_handler(int) { g_running = 0; }

int main(int argc, char* argv[])
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::string uartDev = "/dev/ttyS1";  /* 默认串口 */
    int webPort  = 80;
    int tcpPort  = 9527;

    if (argc > 1) uartDev = argv[1];
    if (argc > 2) webPort = std::stoi(argv[2]);
    if (argc > 3) tcpPort = std::stoi(argv[3]);

    std::cout << "=== OLED Gateway v1.0 ===" << std::endl;
    std::cout << "UART: " << uartDev << " | Web: " << webPort << " | TCP: " << tcpPort << std::endl;

    /* 1. 初始化串口 */
    UartAdapter uart;
    if (uart.open(uartDev, 115200) != 0) {
        std::cerr << "[FATAL] Failed to open UART " << uartDev << std::endl;
        return -1;
    }
    std::cout << "[UART] Connected to STM32" << std::endl;

    /* 2. 命令分发器 */
    CmdDispatcher dispatcher(uart);

    /* 3. Web 服务器 */
    WebServer web;
    web.start(webPort, "./web");
    web.onMessage([&dispatcher](const std::string& msg) -> std::string {
        return dispatcher.handleJsonCommand(msg);
    });

    /* 4. TCP 服务器 */
    TcpServer tcp;
    tcp.start(tcpPort);
    tcp.onMessage([&dispatcher, &tcp](const std::string& msg, int clientId) {
        std::string resp = dispatcher.handleJsonCommand(msg);
        if (!resp.empty()) {
            tcp.send(clientId, resp + "\n");
        }
    });

    /* 5. 注册事件回调 (仅记录到队列, 不在回调中直接广播, 避免与主循环 pollEvent 重复) */
    dispatcher.onJsonEvent([&web, &tcp](const std::string& json) {
        /* 回调仅做日志或辅助处理, 实际广播由主循环 pollEvent 完成 */
        (void)json;
    });

    std::cout << "[Main] All services started. Press Ctrl+C to stop." << std::endl;

    /* 6. 主循环 */
    while (g_running) {
        uart.poll();

        /* Web服务器自带事件循环, 此处处理命令广播 */
        web.pollEvents();

        /* 分发 UART 事件到 Web/TCP */
        std::string evtJson;
        while (dispatcher.pollEvent(evtJson)) {
            web.broadcast(evtJson);
            tcp.broadcast(evtJson + "\n");
        }

        /* 时间同步 */
        dispatcher.tickTimeSync();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    /* 清理 */
    tcp.stop();
    web.stop();
    uart.close();

    std::cout << "[Main] Gateway stopped." << std::endl;
    return 0;
}
