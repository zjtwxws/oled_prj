/**
 * @file    cmd_dispatcher.cpp
 * @brief   命令分发器实现
 */

#include "cmd_dispatcher.h"
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <iostream>

/* 简易 JSON 解析: 仅支持 {"cmd":"xxx","data":{...}} 格式 */
/* 生产环境建议使用 nlohmann/json 库 */

/* 辅助: 提取 JSON 字符串中某 key 的 value */
static std::string jsonGetStr(const std::string& json, const std::string& key)
{
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    pos += search.length();
    /* 跳空白 */
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

    if (pos >= json.length()) return "";

    if (json[pos] == '"') {
        /* 字符串 */
        pos++;
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    } else {
        /* 数字/布尔 */
        size_t end = pos;
        while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') end++;
        return json.substr(pos, end - pos);
    }
}

static int jsonGetInt(const std::string& json, const std::string& key)
{
    std::string s = jsonGetStr(json, key);
    return s.empty() ? 0 : std::stoi(s);
}

CmdDispatcher::CmdDispatcher(UartAdapter& uart) : m_uart(uart)
{
    m_uart.onFrame([this](const ProtoFrame& f) { onUartFrame(f); });
}

void CmdDispatcher::onJsonEvent(JsonEventCallback cb) { m_jsonCb = cb; }

bool CmdDispatcher::pollEvent(std::string& json)
{
    std::lock_guard<std::mutex> lock(m_eventMutex);
    if (m_eventQueue.empty()) return false;
    json = m_eventQueue.front();
    m_eventQueue.erase(m_eventQueue.begin());
    return true;
}

void CmdDispatcher::pushEvent(const std::string& json)
{
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventQueue.push_back(json);
    if (m_jsonCb) m_jsonCb(json);
}

std::string CmdDispatcher::handleJsonCommand(const std::string& json)
{
    std::string cmd = jsonGetStr(json, "cmd");
    std::string dataStr = jsonGetStr(json, "data");

    if (cmd == "led") {
        int state = jsonGetInt(dataStr, "state");
        if (state < 0 || state > 2) return buildAck("led", -1);

        uint8_t d = (uint8_t)state;
        int seq = m_uart.sendCommand(CMD_LED_CTRL, &d, 1, CMD_ACK);
        return buildAck("led", seq);
    }

    if (cmd == "mode") {
        int mode = jsonGetInt(dataStr, "mode");
        if (mode < 0 || mode > 6) return buildAck("mode", -1);

        uint8_t d = (uint8_t)mode;
        int seq = m_uart.sendCommand(CMD_DISPLAY_MODE, &d, 1, CMD_ACK);
        return buildAck("mode", seq);
    }

    if (cmd == "text") {
        std::string content = jsonGetStr(dataStr, "content");
        if (content.empty()) return buildAck("text", -1);
        if (content.length() > PROTO_MAX_DATA) content = content.substr(0, PROTO_MAX_DATA);

        int seq = m_uart.sendCommand(CMD_TEXT_CONTENT,
                                      (const uint8_t*)content.c_str(),
                                      (uint8_t)content.length(), CMD_ACK);
        return buildAck("text", seq);
    }

    if (cmd == "weather") {
        m_weatherType  = jsonGetInt(dataStr, "type");
        m_temperature  = jsonGetInt(dataStr, "temp");
        m_humidity     = jsonGetInt(dataStr, "humidity");
        m_windDir      = jsonGetInt(dataStr, "wind");

        uint8_t d[4] = {
            (uint8_t)m_weatherType,
            (uint8_t)m_temperature,
            (uint8_t)m_humidity,
            (uint8_t)m_windDir
        };
        int seq = m_uart.sendCommand(CMD_WEATHER_DATA, d, 4, CMD_ACK);
        return buildAck("weather", seq);
    }

    if (cmd == "time_sync") {
        time_t ts = (time_t)jsonGetInt(dataStr, "ts");
        struct tm* t = localtime(&ts);
        uint8_t d[7] = {
            (uint8_t)(t->tm_year - 100),  /* 年 (BCD-like, 简化) */
            (uint8_t)(t->tm_mon + 1),
            (uint8_t)t->tm_mday,
            (uint8_t)t->tm_hour,
            (uint8_t)t->tm_min,
            (uint8_t)t->tm_sec,
            (uint8_t)t->tm_wday
        };
        int seq = m_uart.sendCommand(CMD_TIME_SYNC, d, 7, CMD_ACK);
        return buildAck("time_sync", seq);
    }

    if (cmd == "boot_text") {
        std::string content = jsonGetStr(dataStr, "content");
        if (content.empty()) return buildAck("boot_text", -1);
        if (content.length() > PROTO_MAX_DATA) content = content.substr(0, PROTO_MAX_DATA);

        int seq = m_uart.sendCommand(CMD_BOOT_TEXT,
                                      (const uint8_t*)content.c_str(),
                                      (uint8_t)content.length(), CMD_ACK);
        return buildAck("boot_text", seq);
    }

    return buildAck("unknown", -2);
}

void CmdDispatcher::tickTimeSync()
{
    /* 每秒同步时间到 STM32 */
    uint32_t now = (uint32_t)time(nullptr);
    if (now - m_lastTimeSync >= 1) {
        m_lastTimeSync = now;
        struct tm* t = localtime((const time_t*)&now);
        uint8_t d[7] = {
            (uint8_t)(t->tm_year - 100),
            (uint8_t)(t->tm_mon + 1),
            (uint8_t)t->tm_mday,
            (uint8_t)t->tm_hour,
            (uint8_t)t->tm_min,
            (uint8_t)t->tm_sec,
            (uint8_t)t->tm_wday
        };
        m_uart.sendRaw(CMD_TIME_SYNC, 0, d, 7);
    }
}

void CmdDispatcher::onUartFrame(const ProtoFrame& frame)
{
    switch (frame.cmd) {
    case CMD_LED_STATUS: {
        int state = frame.len >= 1 ? frame.data[0] : 0;
        pushEvent(buildEvent("led_status", buildSimpleObj({{"state", std::to_string(state)}})));
        break;
    }
    case CMD_MODE_STATUS: {
        int mode = frame.len >= 1 ? frame.data[0] : 0;
        pushEvent(buildEvent("mode_status", buildSimpleObj({{"mode", std::to_string(mode)}})));
        break;
    }
    case CMD_KEY_EVENT: {
        int key    = frame.len >= 1 ? frame.data[0] : 0;
        int action = frame.len >= 2 ? frame.data[1] : 0;
        std::string actStr = (action == 0) ? "press" : (action == 1) ? "long_press" : "release";
        pushEvent(buildEvent("key_event",
            buildSimpleObj({{"key", std::to_string(key)}, {"action", actStr}})));
        /* 回复 ACK (协议要求: STM32 上报按键事件后 RK3506 需回复 ACK) */
        m_uart.sendRaw(CMD_ACK, frame.seq, nullptr, 0);
        break;
    }
    case CMD_ACK:
        pushEvent(buildEvent("ack", buildSimpleObj({{"code", "0"}})));
        break;
    case CMD_NAK: {
        int err = frame.len >= 1 ? frame.data[0] : 0;
        pushEvent(buildEvent("error", buildSimpleObj({{"code", std::to_string(err)}})));
        break;
    }
    default:
        break;
    }
}

/* JSON 构建辅助 */
std::string CmdDispatcher::buildAck(const std::string& cmd, int code)
{
    return "{\"cmd\":\"ack\",\"data\":{\"ref\":\"" + cmd + "\",\"code\":" + std::to_string(code) + "}}";
}

std::string CmdDispatcher::buildEvent(const std::string& evt, const std::string& dataJson)
{
    return "{\"evt\":\"" + evt + "\",\"data\":" + dataJson + "}";
}

std::string CmdDispatcher::buildSimpleObj(const std::map<std::string, std::string>& kv)
{
    std::ostringstream os;
    os << "{";
    bool first = true;
    for (auto& p : kv) {
        if (!first) os << ",";
        os << "\"" << p.first << "\":";
        /* 简单判断是否为数字 */
        bool isNum = !p.second.empty() && (p.second[0] == '-' || (p.second[0] >= '0' && p.second[0] <= '9'));
        if (isNum) os << p.second;
        else os << "\"" << p.second << "\"";
        first = false;
    }
    os << "}";
    return os.str();
}
