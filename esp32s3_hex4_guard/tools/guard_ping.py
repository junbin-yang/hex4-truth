#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
hex4_guard 快速对拍脚本 (兼容现行协议: 角色密钥 HMAC 签名, 复用 guard_cmd 实现)
用法: python3 guard_ping.py <串口> [动作] [角色] [参数k=v ...]
示例:
  python3 guard_ping.py /dev/ttyACM1                               # ping (默认)
  python3 guard_ping.py /dev/ttyACM1 motor_run operator \
      tcp_speed=100 payload=1000 safety_door=0 tcp_force=10000 mode=0   # ALLOW
  python3 guard_ping.py /dev/ttyACM1 motor_run operator \
      tcp_speed=142 payload=1000 safety_door=0 tcp_force=10000 mode=0   # DENY/L3 能量限
帧格式与 guard_frame.c 一致; HMAC 规范字节串与 guard_cmd.c 一致。
"""
import sys
import os
import time
import serial

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from guard_cmd import build_cmd, pack, parse_reply


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    port = sys.argv[1]
    action = sys.argv[2] if len(sys.argv) > 2 else "ping"
    role = sys.argv[3] if len(sys.argv) > 3 else "operator"
    params = {}
    for kv in sys.argv[4:]:
        if "=" in kv:
            k, v = kv.split("=", 1)
            params[k] = int(v)

    seq = int(time.time()) & 0x7FFFFFFF
    frame = pack(0x01, build_cmd(seq, action, role, params))

    ser = serial.Serial(port, 921600, timeout=1)
    ser.setDTR(True)
    ser.setRTS(True)
    ser.write(frame)
    print(f"[TX] {len(frame)}B: {frame.hex(' ')}")
    rx = ser.read(512)
    print(f"[RX] {len(rx)}B: {rx.hex(' ')}")
    r = parse_reply(rx)
    if r:
        print(f"[RX] type=0x{r['type']:02x} CRC={'OK' if r['crc_ok'] else 'FAIL'} "
              f"reply={r['obj']}")
    ser.close()


if __name__ == "__main__":
    main()
