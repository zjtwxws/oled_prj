"""
OTA 升级客户端 — 串口通信 + 升级流程控制
"""

import time
import struct
import serial
import sys
from crc32 import crc32_ieee
from proto import (
    build_ota_start, build_ota_data, build_ota_finish, build_ota_abort,
    build_ack, is_ack, is_nak, get_nak_code, format_nak_error
)

CHUNK_SIZE = 200  # 每块数据大小 (≤247)
ACK_TIMEOUT = 5.0  # ACK 等待超时 (秒)


class OtaClient:
    """OTA 升级客户端"""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 5.0,
                 retry: int = 3):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.retry = retry
        self.ser = None

    def open(self) -> bool:
        """打开串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=ACK_TIMEOUT
            )
            return True
        except serial.SerialException as e:
            print(f"[ERROR] 无法打开串口 {self.port}: {e}")
            self._list_ports()
            return False

    def close(self):
        """关闭串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()

    @staticmethod
    def _list_ports():
        """列出可用串口"""
        try:
            from serial.tools.list_ports import comports
            ports = list(comports())
            if ports:
                print("\n可用串口:")
                for p in ports:
                    print(f"  {p.device} - {p.description}")
            else:
                print("\n未检测到串口设备")
        except ImportError:
            pass

    def _send_frame(self, frame: bytes) -> bool:
        """发送一帧"""
        try:
            self.ser.write(frame)
            self.ser.flush()
            return True
        except serial.SerialException as e:
            print(f"[ERROR] 发送失败: {e}")
            return False

    def _recv_frame(self, timeout: float = None) -> bytes:
        """
        接收一帧 (SOF...EOF)
        :return: 完整帧 bytes 或 b"" (超时/错误)
        """
        if timeout is not None:
            self.ser.timeout = timeout

        buf = bytearray()

        # 等待 SOF
        while True:
            b = self.ser.read(1)
            if not b:
                return b""  # 超时
            if b[0] == 0xA5:
                buf.append(b[0])
                break

        # 读取 LEN + CMD (最少 2 字节)
        rest = self.ser.read(2)
        if len(rest) < 2:
            return b""
        buf.extend(rest)

        data_len = buf[1]

        # 读取 DATA + CRC + EOF
        remaining = data_len + 2  # DATA + CRC(1) + EOF(1)
        rest = self.ser.read(remaining)
        if len(rest) < remaining:
            return b""
        buf.extend(rest)

        if timeout is not None:
            self.ser.timeout = ACK_TIMEOUT

        return bytes(buf)

    def _wait_ack(self, timeout: float = None) -> bool:
        """等待 ACK 帧"""
        t = timeout or self.timeout
        frame = self._recv_frame(t)
        if not frame:
            print("[TIMEOUT] 等待 ACK 超时")
            return False
        if is_nak(frame):
            code = get_nak_code(frame)
            print(f"[NAK] 设备返回错误: {format_nak_error(code)}")
            return False
        if is_ack(frame):
            return True
        # 收到其他数据, 丢弃继续等
        return self._wait_ack(t)

    def upgrade(self, firmware_path: str, slot: int = None,
                version: int = 0x00000001, progress_cb=None) -> bool:
        """
        执行 OTA 升级
        :param firmware_path: .bin 文件路径
        :param slot:          目标槽 (0=A, 1=B, None=自动选非活跃槽)
        :param version:       版本号
        :param progress_cb:   进度回调 callback(current, total)
        :return:              成功/失败
        """
        # 读取固件
        try:
            with open(firmware_path, "rb") as f:
                fw_data = f.read()
        except IOError as e:
            print(f"[ERROR] 无法读取固件文件: {e}")
            return False

        fw_size = len(fw_data)
        fw_crc = crc32_ieee(fw_data)

        print(f"\n固件信息:")
        print(f"  文件: {firmware_path}")
        print(f"  大小: {fw_size} bytes ({fw_size/1024:.1f} KB)")
        print(f"  CRC32: 0x{fw_crc:08X}")
        print(f"  版本: 0x{version:08X}")
        if slot is not None:
            print(f"  目标槽: Slot {'B' if slot else 'A'} (强制)")
        else:
            print(f"  目标槽: 自动选择 (非活跃槽)")

        # 1. 发送 OTA_START
        target_slot = slot if slot is not None else 0  # 默认 Slot A, 实际由设备决定
        print(f"\n[1/3] 发送 OTA_START (slot={target_slot})...", end=" ")
        sys.stdout.flush()

        if not self._send_frame(build_ota_start(target_slot, fw_size, fw_crc, version)):
            return False

        if not self._wait_ack():
            return False
        print("OK")

        # 2. 分块发送 OTA_DATA
        total_chunks = (fw_size + CHUNK_SIZE - 1) // CHUNK_SIZE
        print(f"[2/3] 发送固件数据 ({total_chunks} 块)...")

        offset = 0
        chunk_idx = 0

        while offset < fw_size:
            chunk = fw_data[offset:offset + CHUNK_SIZE]
            chunk_len = len(chunk)

            # 发送数据块 (带重试)
            sent = False
            for attempt in range(self.retry):
                frame = build_ota_data(offset, chunk)
                if not self._send_frame(frame):
                    continue
                if self._wait_ack():
                    offset += chunk_len
                    chunk_idx += 1
                    sent = True
                    break
                print(f"\n  [RETRY] 块 {chunk_idx + 1}/{total_chunks} 第 {attempt + 1} 次重试...")
                time.sleep(0.2)

            if not sent:
                print(f"\n[ERROR] 块 {chunk_idx + 1} 发送失败, 已重试 {self.retry} 次")
                self._send_frame(build_ota_abort())
                return False

            # 进度回调
            if progress_cb:
                progress_cb(offset, fw_size)
            else:
                pct = offset * 100 // fw_size
                bar = "█" * (pct // 5) + "░" * (20 - pct // 5)
                print(f"\r  [{bar}] {pct:3d}%  {offset}/{fw_size}", end="")
                sys.stdout.flush()

        print()  # 换行

        # 3. 发送 OTA_FINISH
        print("[3/3] 发送 OTA_FINISH, 校验中...", end=" ")
        sys.stdout.flush()

        if not self._send_frame(build_ota_finish()):
            return False

        if not self._wait_ack(timeout=10.0):  # 校验需要时间
            return False

        print("OK")
        print("\n✓ 固件升级成功! 设备将从新槽位启动。")
        return True
