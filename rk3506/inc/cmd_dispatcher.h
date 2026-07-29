/**
 * @file    cmd_dispatcher.h
 * @brief   命令分发器 — WebSocket/TCP JSON ↔ UART 二进制帧 转换中心
 */

#ifndef CMD_DISPATCHER_H
#define CMD_DISPATCHER_H

#include "uart_adapter.h"
#include "protocol.h"
#include <string>
#include <functional>
#include <map>
#include <vector>
#include <array>

/* JSON 操作简化: 使用 nlohmann/json 或自定义简易解析器 */
/* 为减少依赖, 此处使用手写简易 JSON 构建/解析 */

class CmdDispatcher {
public:
    using JsonEventCallback = std::function<void(const std::string& json)>;

    CmdDispatcher(UartAdapter& uart);

    /* 下行: WebSocket/TCP JSON → UART 帧 */
    std::string handleJsonCommand(const std::string& json);

    /* 上行: UART 帧 → JSON 事件 (注册回调推送到 Web) */
    void onJsonEvent(JsonEventCallback cb);

    /* 拉取待推送的 JSON 事件 */
    bool pollEvent(std::string& json);

    /* 时间同步 tick */
    void tickTimeSync();

private:
    void onUartFrame(const ProtoFrame& frame);
    void pushEvent(const std::string& json);

    /* JSON 构建辅助 */
    static std::string buildAck(const std::string& cmd, int code);
    static std::string buildEvent(const std::string& evt, const std::string& dataJson);
    static std::string buildSimpleObj(const std::map<std::string, std::string>& kv);

    UartAdapter& m_uart;
    JsonEventCallback m_jsonCb;

    /* 事件队列 */
    std::vector<std::string> m_eventQueue;
    std::mutex m_eventMutex;

    /* 天气模拟状态 */
    int m_weatherType = 0;
    int m_temperature = 25;
    int m_humidity    = 60;
    int m_windDir     = 0;

    /* 时间 */
    uint32_t m_lastTimeSync = 0;

    /* OLED 显存同步重组 */
    std::array<uint8_t, 1024> m_oledFrame;
    uint8_t m_frameSegTotal = 0;
    uint8_t m_frameSegMask = 0;  /* 已收到的分片位掩图 (最多 8 片, 1024/200=6) */
    bool    m_frameReady = false;

    static std::string base64Encode(const uint8_t* data, size_t len);
};

#endif /* CMD_DISPATCHER_H */
