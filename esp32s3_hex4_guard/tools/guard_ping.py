#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
hex4_guard 上位机对拍脚本
用法: python3 guard_ping.py <串口设备> [JSON字符串]
示例: python3 guard_ping.py /dev/ttyUSB1 '{"seq":1,"action":"ping"}'
默认发 ping 帧, 打印收到的 REPLY 帧 (echo 对拍)。
帧格式与 guard_frame.c 一致: HX | ver | type | len(LE) | payload | CRC16(LE, XMODEM, 覆盖 len+payload)
"""
import sys
import struct
import serial

POLY = 0x1021


def crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ POLY) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def pack(type_: int, payload: bytes) -> bytes:
    assert len(payload) <= 480, "payload 超 480B 上限"
    body = struct.pack("<H", len(payload)) + payload   # 长度 LE + 负载
    crc = crc16(body)                                   # CRC 覆盖长度+负载
    return b"HX" + bytes([0x01, type_]) + body + struct.pack("<H", crc)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    port = sys.argv[1]
    payload = sys.argv[2].encode() if len(sys.argv) > 2 else b'{"seq":1,"action":"ping"}'

    frame = pack(0x01, payload)                         # 0x01 = CMD
    ser = serial.Serial(port, 921600, timeout=1)
    ser.write(frame)
    print(f"[TX] {len(frame)}B: {frame.hex(' ')}")
    rx = ser.read(512)
    print(f"[RX] {len(rx)}B: {rx.hex(' ')}")
    if rx[:2] == b"HX" and len(rx) >= 8:
        plen = struct.unpack("<H", rx[4:6])[0]
        print(f"[RX] type=0x{rx[3]:02x} len={plen} payload={rx[6:6 + plen]!r}")
        print(f"[RX] CRC {'OK' if struct.unpack('<H', rx[6 + plen:8 + plen])[0] == crc16(rx[4:6 + plen]) else 'FAIL'}")
    ser.close()


if __name__ == "__main__":
    main()
