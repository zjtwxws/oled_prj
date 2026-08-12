#!/usr/bin/env python3
"""
STM32F407 OTA 固件升级工具 (A/B 双槽位)

用法:
    python ota_tool.py COM3 firmware.bin
    python ota_tool.py COM3 firmware.bin --force-slot 0
    python ota_tool.py /dev/ttyUSB0 firmware.bin --baud 460800
"""

import argparse
import re
import sys
from ota_client import OtaClient


def parse_version(filename: str) -> int:
    """
    从文件名提取版本号
    支持格式: xxx_V1.0.3.bin → 0x00010003
    """
    m = re.search(r'[Vv](\d+)\.(\d+)\.(\d+)', filename)
    if m:
        major = int(m.group(1))
        minor = int(m.group(2))
        patch = int(m.group(3))
        return (major << 16) | (minor << 8) | patch
    return 0x00000001  # 默认版本


def main():
    parser = argparse.ArgumentParser(
        description="STM32F407 OTA 固件升级工具 (A/B 双槽位)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python ota_tool.py COM3 app_slot_a.bin
  python ota_tool.py COM3 app_slot_b.bin --force-slot 1
  python ota_tool.py /dev/ttyUSB0 firmware.bin --version 1.2.0
        """
    )
    parser.add_argument("port", help="串口号 (Windows: COM3, Linux: /dev/ttyUSB0)")
    parser.add_argument("firmware", help="固件 .bin 文件路径")
    parser.add_argument("--baud", type=int, default=115200,
                        help="波特率 (默认: 115200)")
    parser.add_argument("--force-slot", type=int, choices=[0, 1],
                        help="强制指定目标槽 (0=A, 1=B)")
    parser.add_argument("--version", type=str,
                        help="版本号 (如 1.0.3), 默认从文件名提取")
    parser.add_argument("--timeout", type=float, default=5.0,
                        help="帧超时(秒) (默认: 5)")
    parser.add_argument("--retry", type=int, default=3,
                        help="最大重试次数 (默认: 3)")

    args = parser.parse_args()

    # 解析版本号
    if args.version:
        parts = args.version.split(".")
        if len(parts) != 3:
            print("[ERROR] 版本号格式错误, 应为 X.Y.Z (如 1.0.3)")
            sys.exit(1)
        try:
            version = (int(parts[0]) << 16) | (int(parts[1]) << 8) | int(parts[2])
        except ValueError:
            print("[ERROR] 版本号各部分必须为数字")
            sys.exit(1)
    else:
        version = parse_version(args.firmware)

    print("=" * 50)
    print("  STM32F407 OTA 固件升级工具 V1.0")
    print("=" * 50)

    # 创建客户端
    client = OtaClient(
        port=args.port,
        baud=args.baud,
        timeout=args.timeout,
        retry=args.retry
    )

    # 打开串口
    print(f"\n打开 {args.port} ({args.baud} 8N1)...")
    if not client.open():
        sys.exit(1)

    try:
        # 执行升级
        success = client.upgrade(
            firmware_path=args.firmware,
            slot=args.force_slot,
            version=version
        )

        if not success:
            print("\n✗ 固件升级失败!")
            sys.exit(1)

    except KeyboardInterrupt:
        print("\n\n[中断] 用户取消操作")
        sys.exit(1)
    finally:
        client.close()
        print("串口已关闭")


if __name__ == "__main__":
    main()
