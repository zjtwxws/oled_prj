/**
 * @file    cmd_dispatcher.cpp
 * @brief   命令分发器实现
 */

#include "cmd_dispatcher.h"
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <iostream>

/* 简易 JSON 解析器 (加固版):
 * 不引入外部库时用于 RK3506 网关. 支持:
 *   - 字符串中的 \" 和 \\ 转义
 *   - 嵌套对象路径, 如 "data.state"
 *   - 基本数字/布尔/null
 * 生产环境仍建议迁移到 nlohmann/json。
 */

static void skipWhitespace(const std::string& s, size_t& pos)
{
    while (pos < s.length() && (s[pos] == ' ' || s[pos] == '\t' ||
                                s[pos] == '\n' || s[pos] == '\r')) {
        pos++;
    }
}

static std::string jsonParseString(const std::string& s, size_t& pos)
{
    if (pos >= s.length() || s[pos] != '"') return "";
    pos++;  /* 跳过左引号 */
    std::string out;
    while (pos < s.length()) {
        char c = s[pos];
        if (c == '"') {
            pos++;
            return out;
        }
        if (c == '\\' && pos + 1 < s.length()) {
            char next = s[pos + 1];
            if (next == '"' || next == '\\' || next == '/') out.push_back(next);
            else if (next == 'n') out.push_back('\n');
            else if (next == 'r') out.push_back('\r');
            else if (next == 't') out.push_back('\t');
            else if (next == 'b') out.push_back('\b');
            else if (next == 'f') out.push_back('\f');
            /* \uXXXX 未实现, 原样保留反斜杠+next */
            else { out.push_back(c); out.push_back(next); }
            pos += 2;
        } else {
            out.push_back(c);
            pos++;
        }
    }
    return out;  /* 未遇到右引号, 返回已解析部分 */
}

/* 查找 key 的起始位置; key 支持 "data.state" 嵌套路径 */
static size_t jsonFindKey(const std::string& json, const std::string& key)
{
    size_t pos = 0;
    size_t keyPos = 0;

    while (keyPos < key.length()) {
        size_t dot = key.find('.', keyPos);
        std::string part = (dot == std::string::npos) ? key.substr(keyPos) : key.substr(keyPos, dot - keyPos);
        keyPos = (dot == std::string::npos) ? key.length() : dot + 1;

        skipWhitespace(json, pos);
        if (pos >= json.length() || json[pos] != '{') return std::string::npos;
        pos++;  /* skip { */

        bool found = false;
        while (pos < json.length()) {
            skipWhitespace(json, pos);
            if (pos < json.length() && json[pos] == '}') return std::string::npos;

            if (json[pos] != '"') return std::string::npos;
            size_t nameStart = pos;
            std::string name = jsonParseString(json, pos);
            skipWhitespace(json, pos);
            if (pos >= json.length() || json[pos] != ':') return std::string::npos;
            pos++;  /* skip : */

            if (name == part) {
                found = true;
                break;
            }

            /* 跳过该 key 对应的 value */
            skipWhitespace(json, pos);
            if (pos >= json.length()) return std::string::npos;
            if (json[pos] == '"') {
                jsonParseString(json, pos);
            } else if (json[pos] == '{') {
                int depth = 1;
                pos++;
                while (pos < json.length() && depth > 0) {
                    if (json[pos] == '{') depth++;
                    else if (json[pos] == '}') depth--;
                    else if (json[pos] == '"') jsonParseString(json, pos);
                    if (depth > 0) pos++;
                }
            } else if (json[pos] == '[') {
                int depth = 1;
                pos++;
                while (pos < json.length() && depth > 0) {
                    if (json[pos] == '[') depth++;
                    else if (json[pos] == ']') depth--;
                    else if (json[pos] == '"') jsonParseString(json, pos);
                    if (depth > 0) pos++;
                }
            } else {
                while (pos < json.length() && json[pos] != ',' && json[pos] != '}') pos++;
            }

            skipWhitespace(json, pos);
            if (pos < json.length() && json[pos] == ',') pos++;
        }

        if (!found) return std::string::npos;
        /* 循环继续, pos 现在指向该 key 的 value 开始位置 */
    }

    return pos;
}

/* 提取 JSON 字符串中某 key 的原始 value 字符串 */
static std::string jsonGetRaw(const std::string& json, const std::string& key)
{
    size_t pos = jsonFindKey(json, key);
    if (pos == std::string::npos) return "";

    skipWhitespace(json, pos);
    if (pos >= json.length()) return "";

    if (json[pos] == '"') {
        return jsonParseString(json, pos);
    } else {
        size_t end = pos;
        while (end < json.length() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
            end++;
        }
        return json.substr(pos, end - pos);
    }
}

static std::string jsonGetStr(const std::string& json, const std::string& key)
{
    return jsonGetRaw(json, key);
}

static int jsonGetInt(const std::string& json, const std::string& key)
{
    std::string s = jsonGetRaw(json, key);
    if (s.empty()) return 0;
    try {
        size_t idx = 0;
        int v = std::stoi(s, &idx);
        (void)idx;
        return v;
    } catch (...) {
        return 0;
    }
}

CmdDispatcher::CmdDispatcher(UartAdapter& uart) : m_uart(uart)
{
    m_uart.onFrame([this](const ProtoFrame& f) { onUartFrame(f); });
    m_oledFrame.fill(0);
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
        int state = jsonGetInt(json, "data.state");
        if (state < 0 || state > 2) return buildAck("led", -1);

        uint8_t d = (uint8_t)state;
        int seq = m_uart.sendCommand(CMD_LED_CTRL, &d, 1, CMD_ACK);
        return buildAck("led", seq);
    }

    if (cmd == "mode") {
        int mode = jsonGetInt(json, "data.mode");
        if (mode < 0 || mode > 6) return buildAck("mode", -1);

        uint8_t d = (uint8_t)mode;
        int seq = m_uart.sendCommand(CMD_DISPLAY_MODE, &d, 1, CMD_ACK);
        return buildAck("mode", seq);
    }

    if (cmd == "text") {
        std::string content = jsonGetStr(json, "data.content");
        if (content.empty()) return buildAck("text", -1);
        if (content.length() > PROTO_MAX_DATA) content = content.substr(0, PROTO_MAX_DATA);

        int seq = m_uart.sendCommand(CMD_TEXT_CONTENT,
                                      (const uint8_t*)content.c_str(),
                                      (uint8_t)content.length(), CMD_ACK);
        return buildAck("text", seq);
    }

    if (cmd == "weather") {
        m_weatherType  = jsonGetInt(json, "data.type");
        m_temperature  = jsonGetInt(json, "data.temp");
        m_humidity     = jsonGetInt(json, "data.humidity");
        m_windDir      = jsonGetInt(json, "data.wind");

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
        time_t ts = (time_t)jsonGetInt(json, "data.ts");
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
        std::string content = jsonGetStr(json, "data.content");
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
    case CMD_FRAME_SYNC:
        if (frame.len >= 2) {
            uint8_t seg = frame.data[0];
            uint8_t total = frame.data[1];
            uint8_t payloadLen = frame.len - 2;

            if (total > 0 && total <= 8 && seg < total && payloadLen > 0) {
                if (m_frameSegTotal != total) {
                    m_frameSegTotal = total;
                    m_frameSegMask = 0;
                    m_oledFrame.fill(0);
                }

                uint16_t offset = seg * (1024 / total);
                if (seg == total - 1) {
                    offset = 1024 - payloadLen;
                }

                if (offset + payloadLen <= m_oledFrame.size()) {
                    memcpy(&m_oledFrame[offset], &frame.data[2], payloadLen);
                    m_frameSegMask |= (1 << seg);

                    if (m_frameSegMask == (uint8_t)((1 << total) - 1)) {
                        m_frameReady = true;
                        std::string b64 = base64Encode(m_oledFrame.data(), m_oledFrame.size());
                        pushEvent(buildEvent("frame_sync", "\"" + b64 + "\""));
                        m_frameSegMask = 0;  /* 准备接收下一帧 */
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

/* JSON 构建辅助 */
std::string CmdDispatcher::buildAck(const std::string& cmd, int code)
{
    return "{\"cmd\":\"ack\",\"data\":{\"ref\":\"" + cmd + "\",\"code\":" + std::to_string(code) + "}}";
}

std::string CmdDispatcher::base64Encode(const uint8_t* data, size_t len)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = data[i] << 16;
        if (i + 1 < len) b |= data[i + 1] << 8;
        if (i + 2 < len) b |= data[i + 2];

        out.push_back(table[(b >> 18) & 0x3F]);
        out.push_back(table[(b >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? table[(b >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? table[b & 0x3F] : '=');
    }
    return out;
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
