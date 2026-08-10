"""
协议帧构建/解析模块 (Python 端, 与 STM32 端兼容)

帧格式: SOF(0xA5) | LEN(1) | CMD(1) | DATA(0~251) | CRC8(1) | EOF(0x5A)
"""

import struct
from crc32 import crc8

SOF = 0xA5
EOF = 0x5A
MAX_DATA = 251

# 命令码
CMD_OTA_START  = 0x07
CMD_OTA_DATA   = 0x08
CMD_OTA_FINISH = 0x09
CMD_OTA_ABORT  = 0x0A
CMD_ACK        = 0xF0
CMD_NAK        = 0xFF

# NAK 错误码
NAK_CODES = {
    0x01: "CRC 校验错误",
    0x02: "未知命令码",
    0x03: "参数错误",
    0x04: "Flash 写入失败",
    0x05: "设备忙",
    0x06: "OTA 偏移越界",
    0x07: "OTA 整体 CRC32 校验失败",
    0x08: "OTA Flash 擦除失败",
}


def build_frame(cmd: int, data: bytes = b"") -> bytes:
    """
    构建协议帧
    :param cmd:  命令码
    :param data: DATA 段 (≤251 bytes)
    :return:     完整帧 bytes
    """
    if len(data) > MAX_DATA:
        raise ValueError(f"DATA too long: {len(data)} > {MAX_DATA}")

    buf = bytearray()
    buf.append(SOF)
    buf.append(len(data))
    buf.append(cmd)
    buf.extend(data)
    buf.append(crc8(bytes(buf)))
    buf.append(EOF)
    return bytes(buf)


def parse_frame(data: bytes):
    """
    解析协议帧
    :param data: 完整帧 bytes (从 SOF 到 EOF)
    :return:     (cmd, payload) 或 None (无效帧)
    """
    if len(data) < 6:
        return None
    if data[0] != SOF or data[-1] != EOF:
        return None

    length = data[1]
    cmd = data[2]
    payload = data[3:3 + length]

    # 验证 CRC
    expected_crc = crc8(data[:-2])
    actual_crc = data[-2]
    if expected_crc != actual_crc:
        return None

    return (cmd, payload)


def build_ota_start(slot: int, size: int, crc32_val: int, version: int = 0x00000001) -> bytes:
    """
    构建 CMD_OTA_START 帧
    :param slot:      目标槽 (0=A, 1=B)
    :param size:      固件大小 (字节)
    :param crc32_val: 固件 CRC32
    :param version:   版本号 (如 0x00010003 = V1.0.3)
    """
    data = struct.pack("<BIII", slot, size, crc32_val, version)
    return build_frame(CMD_OTA_START, data)


def build_ota_data(offset: int, payload: bytes) -> bytes:
    """
    构建 CMD_OTA_DATA 帧
    :param offset:   槽内偏移 (字节)
    :param payload:  固件数据块 (≤200 字节)
    """
    data = struct.pack("<I", offset) + payload
    return build_frame(CMD_OTA_DATA, data)


def build_ota_finish() -> bytes:
    """构建 CMD_OTA_FINISH 帧 (无数据)"""
    return build_frame(CMD_OTA_FINISH, b"")


def build_ota_abort() -> bytes:
    """构建 CMD_OTA_ABORT 帧 (无数据)"""
    return build_frame(CMD_OTA_ABORT, b"")


def build_ack() -> bytes:
    """构建 ACK 帧"""
    return build_frame(CMD_ACK, b"")


def is_ack(frame_data: bytes) -> bool:
    """判断是否为 ACK 帧"""
    if len(frame_data) < 5:
        return False
    return frame_data[0] == SOF and frame_data[2] == CMD_ACK


def is_nak(frame_data: bytes) -> bool:
    """判断是否为 NAK 帧"""
    if len(frame_data) < 5:
        return False
    return frame_data[0] == SOF and frame_data[2] == CMD_NAK


def get_nak_code(frame_data: bytes) -> int:
    """从 NAK 帧提取错误码"""
    result = parse_frame(frame_data)
    if result is None:
        return -1
    cmd, payload = result
    if cmd != CMD_NAK or len(payload) < 1:
        return -1
    return payload[0]


def format_nak_error(code: int) -> str:
    """格式化 NAK 错误信息"""
    return NAK_CODES.get(code, f"未知错误 (0x{code:02X})")
