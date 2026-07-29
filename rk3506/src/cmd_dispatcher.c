/**
 * @file    cmd_dispatcher.c
 * @brief   命令分发器实现 (纯 C)
 */

#include "cmd_dispatcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ========== 简易 JSON 解析器 (纯 C) ========== */

static void skip_ws(const char* s, int* pos)
{
    while (s[*pos] && (s[*pos] == ' ' || s[*pos] == '\t' ||
                       s[*pos] == '\n' || s[*pos] == '\r')) {
        (*pos)++;
    }
}

static int json_parse_string(const char* s, int* pos, char* out, int out_size)
{
    if (!s[*pos] || s[*pos] != '"') return -1;
    (*pos)++; /* 跳过左引号 */
    int idx = 0;
    while (s[*pos] && s[*pos] != '"') {
        char c = s[*pos];
        if (c == '\\' && s[*pos + 1]) {
            char next = s[(*pos) + 1];
            char mapped = next;
            switch (next) {
                case '"': case '\\': case '/': mapped = next; break;
                case 'n': mapped = '\n'; break;
                case 'r': mapped = '\r'; break;
                case 't': mapped = '\t'; break;
                case 'b': mapped = '\b'; break;
                case 'f': mapped = '\f'; break;
                default:  mapped = next; break;
            }
            if (idx < out_size - 1) out[idx++] = mapped;
            (*pos) += 2;
        } else {
            if (idx < out_size - 1) out[idx++] = c;
            (*pos)++;
        }
    }
    if (s[*pos] == '"') (*pos)++;
    out[idx] = '\0';
    return idx;
}

/* 跳过 value */
static void skip_value(const char* s, int* pos)
{
    skip_ws(s, pos);
    if (s[*pos] == '"') {
        char tmp[256];
        json_parse_string(s, pos, tmp, sizeof(tmp));
    } else if (s[*pos] == '{') {
        int depth = 1;
        (*pos)++;
        while (s[*pos] && depth > 0) {
            if (s[*pos] == '{') depth++;
            else if (s[*pos] == '}') depth--;
            else if (s[*pos] == '"') { char tmp[256]; json_parse_string(s, pos, tmp, sizeof(tmp)); continue; }
            if (depth > 0) (*pos)++;
        }
    } else if (s[*pos] == '[') {
        int depth = 1;
        (*pos)++;
        while (s[*pos] && depth > 0) {
            if (s[*pos] == '[') depth++;
            else if (s[*pos] == ']') depth--;
            else if (s[*pos] == '"') { char tmp[256]; json_parse_string(s, pos, tmp, sizeof(tmp)); continue; }
            if (depth > 0) (*pos)++;
        }
    } else {
        while (s[*pos] && s[*pos] != ',' && s[*pos] != '}' && s[*pos] != ']') (*pos)++;
    }
}

/* 在 JSON 对象中查找 key, 返回 key 对应 value 的起始位置 */
static int json_find_key(const char* json, const char* key)
{
    int pos = 0;
    skip_ws(&json[0], &pos);
    if (json[pos] != '{') return -1;
    pos++; /* 跳过 { */

    while (json[pos]) {
        skip_ws(json, &pos);
        if (json[pos] == '}') return -1;
        if (json[pos] != '"') return -1;

        char name[128];
        int name_len = json_parse_string(json, &pos, name, sizeof(name));
        if (name_len < 0) return -1;

        skip_ws(json, &pos);
        if (json[pos] != ':') return -1;
        pos++; /* 跳过 : */

        if (strcmp(name, key) == 0) {
            return pos; /* 返回 value 起始位置 */
        }

        /* 跳过该 value */
        skip_value(json, &pos);
        skip_ws(json, &pos);
        if (json[pos] == ',') pos++;
    }
    return -1;
}

/* 提取 JSON 中某 key 的字符串值 */
static int json_get_str(const char* json, const char* key, char* out, int out_size)
{
    int val_pos = json_find_key(json, key);
    if (val_pos < 0) { out[0] = '\0'; return -1; }

    skip_ws(json, &val_pos);
    if (json[val_pos] == '"') {
        return json_parse_string(json, &val_pos, out, out_size);
    } else {
        /* 复制到逗号/大括号 */
        int idx = 0;
        while (json[val_pos] && json[val_pos] != ',' && json[val_pos] != '}' && idx < out_size - 1) {
            out[idx++] = json[val_pos++];
        }
        out[idx] = '\0';
        /* trim */
        while (idx > 0 && (out[idx-1] == ' ' || out[idx-1] == '\t' || out[idx-1] == '\n')) out[--idx] = '\0';
        return idx;
    }
}

/* 解析嵌套路径: key1.key2 */
static int json_get_nested_str(const char* json, const char* key_path, char* out, int out_size)
{
    char path_copy[128];
    strncpy(path_copy, key_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    /* 找到最外层 key */
    char* dot = strchr(path_copy, '.');
    if (!dot) {
        return json_get_str(json, path_copy, out, out_size);
    }

    *dot = '\0';
    char* inner_key = dot + 1;

    /* 提取内层 JSON 对象 */
    int val_pos = json_find_key(json, path_copy);
    if (val_pos < 0) { out[0] = '\0'; return -1; }

    skip_ws(json, &val_pos);
    if (json[val_pos] != '{') { out[0] = '\0'; return -1; }

    /* 找到内层对象的结束 */
    int depth = 1;
    int start = val_pos;
    val_pos++;
    while (json[val_pos] && depth > 0) {
        if (json[val_pos] == '{') depth++;
        else if (json[val_pos] == '}') depth--;
        else if (json[val_pos] == '"') { char tmp[256]; json_parse_string(json, &val_pos, tmp, sizeof(tmp)); continue; }
        if (depth > 0) val_pos++;
    }

    /* 创建临时子 JSON */
    int sub_len = val_pos - start + 1;
    char* sub_json = (char*)malloc(sub_len + 1);
    if (!sub_json) { out[0] = '\0'; return -1; }
    memcpy(sub_json, json + start, sub_len);
    sub_json[sub_len] = '\0';

    int result = json_get_str(sub_json, inner_key, out, out_size);
    free(sub_json);
    return result;
}

static int json_get_nested_int(const char* json, const char* key_path, int default_val)
{
    char buf[32];
    if (json_get_nested_str(json, key_path, buf, sizeof(buf)) < 0) return default_val;
    if (buf[0] == '\0') return default_val;
    return atoi(buf);
}

/* ========== Base64 编码 ========== */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t* data, int len, char* out, int out_size)
{
    int idx = 0;
    for (int i = 0; i < len && idx < out_size - 4; i += 3) {
        uint32_t b = (uint32_t)data[i] << 16;
        if (i + 1 < len) b |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) b |= (uint32_t)data[i + 2];

        out[idx++] = b64_table[(b >> 18) & 0x3F];
        out[idx++] = b64_table[(b >> 12) & 0x3F];
        out[idx++] = (i + 1 < len) ? b64_table[(b >> 6) & 0x3F] : '=';
        out[idx++] = (i + 2 < len) ? b64_table[b & 0x3F] : '=';
    }
    out[idx] = '\0';
}

/* ========== UART 帧回调 ========== */

static void on_uart_frame_cb(const ProtoFrame* frame, void* user_data)
{
    CmdDispatcher* d = (CmdDispatcher*)user_data;

    switch (frame->cmd) {
    case CMD_LED_STATUS: {
        int state = (frame->len >= 1) ? frame->data[0] : 0;
        const char* obj = disp_build_simple_obj_int("state", state);
        const char* evt = disp_build_event("led_status", obj);
        disp_poll_event(d, NULL, 0); /* 仅触发 push */
        /* 直接推送 (调用 poll_event 的 push 路径) */
        {
            pthread_mutex_lock(&d->event_mutex);
            if (d->event_count < DISP_EVENT_QUEUE_MAX) {
                snprintf(d->event_queue[d->event_tail], sizeof(d->event_queue[0]), "%s", evt);
                d->event_tail = (d->event_tail + 1) % DISP_EVENT_QUEUE_MAX;
                d->event_count++;
            }
            pthread_mutex_unlock(&d->event_mutex);
        }
        break;
    }
    case CMD_MODE_STATUS: {
        int mode = (frame->len >= 1) ? frame->data[0] : 0;
        const char* obj = disp_build_simple_obj_int("mode", mode);
        const char* evt = disp_build_event("mode_status", obj);
        pthread_mutex_lock(&d->event_mutex);
        if (d->event_count < DISP_EVENT_QUEUE_MAX) {
            snprintf(d->event_queue[d->event_tail], sizeof(d->event_queue[0]), "%s", evt);
            d->event_tail = (d->event_tail + 1) % DISP_EVENT_QUEUE_MAX;
            d->event_count++;
        }
        pthread_mutex_unlock(&d->event_mutex);
        break;
    }
    case CMD_KEY_EVENT: {
        int key    = (frame->len >= 1) ? frame->data[0] : 0;
        int action = (frame->len >= 2) ? frame->data[1] : 0;
        const char* act_str = (action == 0) ? "press" : (action == 1) ? "long_press" : "release";

        char data_json[256];
        snprintf(data_json, sizeof(data_json), "{\"key\":%d,\"action\":\"%s\"}", key, act_str);
        const char* evt = disp_build_event("key_event", data_json);
        pthread_mutex_lock(&d->event_mutex);
        if (d->event_count < DISP_EVENT_QUEUE_MAX) {
            snprintf(d->event_queue[d->event_tail], sizeof(d->event_queue[0]), "%s", evt);
            d->event_tail = (d->event_tail + 1) % DISP_EVENT_QUEUE_MAX;
            d->event_count++;
        }
        pthread_mutex_unlock(&d->event_mutex);

        /* 回复 ACK */
        uart_send_raw(d->uart, CMD_ACK, frame->seq, NULL, 0);
        break;
    }
    case CMD_ACK: {
        const char* evt = disp_build_event("ack", "{\"code\":0}");
        pthread_mutex_lock(&d->event_mutex);
        if (d->event_count < DISP_EVENT_QUEUE_MAX) {
            snprintf(d->event_queue[d->event_tail], sizeof(d->event_queue[0]), "%s", evt);
            d->event_tail = (d->event_tail + 1) % DISP_EVENT_QUEUE_MAX;
            d->event_count++;
        }
        pthread_mutex_unlock(&d->event_mutex);
        break;
    }
    case CMD_NAK: {
        int err = (frame->len >= 1) ? frame->data[0] : 0;
        char data_json[64];
        snprintf(data_json, sizeof(data_json), "{\"code\":%d}", err);
        const char* evt = disp_build_event("error", data_json);
        pthread_mutex_lock(&d->event_mutex);
        if (d->event_count < DISP_EVENT_QUEUE_MAX) {
            snprintf(d->event_queue[d->event_tail], sizeof(d->event_queue[0]), "%s", evt);
            d->event_tail = (d->event_tail + 1) % DISP_EVENT_QUEUE_MAX;
            d->event_count++;
        }
        pthread_mutex_unlock(&d->event_mutex);
        break;
    }
    case CMD_FRAME_SYNC:
        if (frame->len >= 2) {
            uint8_t seg   = frame->data[0];
            uint8_t total = frame->data[1];
            uint8_t payload_len = frame->len - 2;

            if (total > 0 && total <= OLED_SEG_MAX && seg < total && payload_len > 0) {
                if (d->frame_seg_total != total) {
                    d->frame_seg_total = total;
                    d->frame_seg_mask = 0;
                    memset(d->oled_frame, 0, OLED_FRAME_SIZE);
                }

                uint16_t offset = seg * (OLED_FRAME_SIZE / total);
                if (seg == total - 1) {
                    offset = OLED_FRAME_SIZE - payload_len;
                }

                if (offset + payload_len <= OLED_FRAME_SIZE) {
                    memcpy(&d->oled_frame[offset], &frame->data[2], payload_len);
                    d->frame_seg_mask |= (1 << seg);

                    if (d->frame_seg_mask == (uint8_t)((1 << total) - 1)) {
                        d->frame_ready = 1;
                        char b64[2048];
                        base64_encode(d->oled_frame, OLED_FRAME_SIZE, b64, sizeof(b64));

                        char data_json[2200];
                        snprintf(data_json, sizeof(data_json), "\"%s\"", b64);
                        const char* evt = disp_build_event("frame_sync", data_json);

                        pthread_mutex_lock(&d->event_mutex);
                        if (d->event_count < DISP_EVENT_QUEUE_MAX) {
                            snprintf(d->event_queue[d->event_tail], sizeof(d->event_queue[0]), "%s", evt);
                            d->event_tail = (d->event_tail + 1) % DISP_EVENT_QUEUE_MAX;
                            d->event_count++;
                        }
                        pthread_mutex_unlock(&d->event_mutex);

                        d->frame_seg_mask = 0;
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

/* ========== 公共接口 ========== */

void disp_init(CmdDispatcher* d, UartAdapter* uart)
{
    memset(d, 0, sizeof(*d));
    d->uart = uart;
    d->weather_type = 0;
    d->temperature  = 25;
    d->humidity     = 60;
    d->wind_dir     = 0;
    pthread_mutex_init(&d->event_mutex, NULL);

    uart_on_frame(uart, on_uart_frame_cb, d);
}

void disp_deinit(CmdDispatcher* d)
{
    pthread_mutex_destroy(&d->event_mutex);
}

void disp_on_json_event(CmdDispatcher* d, disp_json_event_callback_t cb, void* user_data)
{
    d->json_cb = cb;
    d->json_cb_user_data = user_data;
}

int disp_poll_event(CmdDispatcher* d, char* json_buf, int buf_size)
{
    pthread_mutex_lock(&d->event_mutex);
    if (d->event_count == 0) {
        pthread_mutex_unlock(&d->event_mutex);
        return 0;
    }

    if (json_buf && buf_size > 0) {
        snprintf(json_buf, buf_size, "%s", d->event_queue[d->event_head]);
    }
    d->event_head = (d->event_head + 1) % DISP_EVENT_QUEUE_MAX;
    d->event_count--;
    pthread_mutex_unlock(&d->event_mutex);

    return 1;
}

const char* disp_handle_json(CmdDispatcher* d, const char* json)
{
    char cmd_buf[64];
    if (json_get_str(json, "cmd", cmd_buf, sizeof(cmd_buf)) < 0) {
        return disp_build_ack("unknown", -2);
    }

    if (strcmp(cmd_buf, "led") == 0) {
        int state = json_get_nested_int(json, "data.state", -1);
        if (state < 0 || state > 2) return disp_build_ack("led", -1);

        uint8_t led_val = (uint8_t)state;
        int seq = uart_send_command(d->uart, CMD_LED_CTRL, &led_val, 1, CMD_ACK, UART_TIMEOUT_MS);
        return disp_build_ack("led", seq);
    }

    if (strcmp(cmd_buf, "mode") == 0) {
        int mode = json_get_nested_int(json, "data.mode", -1);
        if (mode < 0 || mode > 6) return disp_build_ack("mode", -1);

        uint8_t mode_val = (uint8_t)mode;
        int seq = uart_send_command(d->uart, CMD_DISPLAY_MODE, &mode_val, 1, CMD_ACK, UART_TIMEOUT_MS);
        return disp_build_ack("mode", seq);
    }

    if (strcmp(cmd_buf, "text") == 0) {
        char content[PROTO_MAX_DATA + 1];
        int clen = json_get_nested_str(json, "data.content", content, sizeof(content));
        if (clen <= 0) return disp_build_ack("text", -1);
        if (clen > PROTO_MAX_DATA) clen = PROTO_MAX_DATA;

        int seq = uart_send_command(d->uart, CMD_TEXT_CONTENT,
                                     (const uint8_t*)content,
                                     (uint8_t)clen, CMD_ACK, UART_TIMEOUT_MS);
        return disp_build_ack("text", seq);
    }

    if (strcmp(cmd_buf, "weather") == 0) {
        d->weather_type = json_get_nested_int(json, "data.type", 0);
        d->temperature  = json_get_nested_int(json, "data.temp", 25);
        d->humidity     = json_get_nested_int(json, "data.humidity", 60);
        d->wind_dir     = json_get_nested_int(json, "data.wind", 0);

        uint8_t wdata[4] = {
            (uint8_t)d->weather_type,
            (uint8_t)d->temperature,
            (uint8_t)d->humidity,
            (uint8_t)d->wind_dir
        };
        int seq = uart_send_command(d->uart, CMD_WEATHER_DATA, wdata, 4, CMD_ACK, UART_TIMEOUT_MS);
        return disp_build_ack("weather", seq);
    }

    if (strcmp(cmd_buf, "time_sync") == 0) {
        time_t ts = (time_t)json_get_nested_int(json, "data.ts", 0);
        struct tm* t = localtime(&ts);
        uint8_t time_data[7] = {
            (uint8_t)(t->tm_year - 100),
            (uint8_t)(t->tm_mon + 1),
            (uint8_t)t->tm_mday,
            (uint8_t)t->tm_hour,
            (uint8_t)t->tm_min,
            (uint8_t)t->tm_sec,
            (uint8_t)t->tm_wday
        };
        int seq = uart_send_command(d->uart, CMD_TIME_SYNC, time_data, 7, CMD_ACK, UART_TIMEOUT_MS);
        return disp_build_ack("time_sync", seq);
    }

    if (strcmp(cmd_buf, "boot_text") == 0) {
        char content[PROTO_MAX_DATA + 1];
        int clen = json_get_nested_str(json, "data.content", content, sizeof(content));
        if (clen <= 0) return disp_build_ack("boot_text", -1);
        if (clen > PROTO_MAX_DATA) clen = PROTO_MAX_DATA;

        int seq = uart_send_command(d->uart, CMD_BOOT_TEXT,
                                     (const uint8_t*)content,
                                     (uint8_t)clen, CMD_ACK, UART_TIMEOUT_MS);
        return disp_build_ack("boot_text", seq);
    }

    return disp_build_ack("unknown", -2);
}

void disp_tick_time_sync(CmdDispatcher* d)
{
    uint32_t now = (uint32_t)time(NULL);
    if (now - d->last_time_sync >= 1) {
        d->last_time_sync = now;
        struct tm* t = localtime((const time_t*)&now);
        uint8_t d7[7] = {
            (uint8_t)(t->tm_year - 100),
            (uint8_t)(t->tm_mon + 1),
            (uint8_t)t->tm_mday,
            (uint8_t)t->tm_hour,
            (uint8_t)t->tm_min,
            (uint8_t)t->tm_sec,
            (uint8_t)t->tm_wday
        };
        uart_send_raw(d->uart, CMD_TIME_SYNC, 0, d7, 7);
    }
}

/* ========== JSON 构建辅助 (使用静态缓冲区) ========== */

const char* disp_build_ack(const char* cmd, int code)
{
    static char buf[2048];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"ack\",\"data\":{\"ref\":\"%s\",\"code\":%d}}",
             cmd, code);
    return buf;
}

const char* disp_build_event(const char* evt, const char* data_json)
{
    static char buf[4096];
    snprintf(buf, sizeof(buf), "{\"evt\":\"%s\",\"data\":%s}", evt, data_json);
    return buf;
}

const char* disp_build_simple_obj(const char* key, const char* val)
{
    static char buf[256];
    snprintf(buf, sizeof(buf), "{\"%s\":\"%s\"}", key, val);
    return buf;
}

const char* disp_build_simple_obj_int(const char* key, int val)
{
    static char buf[256];
    snprintf(buf, sizeof(buf), "{\"%s\":%d}", key, val);
    return buf;
}
