#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
hex4_guard 上位机指令脚本 (角色密钥 HMAC 签名 + 全用例对拍)
用法: python3 guard_cmd.py <串口> <动作> [角色] [参数]
示例:
  python3 guard_cmd.py /dev/ttyACM1 ping
  python3 guard_cmd.py /dev/ttyACM1 motor_run operator speed=50
  python3 guard_cmd.py /dev/ttyACM1 motor_run supervisor speed=50   # 期望 DENY/L3
帧格式与 guard_frame.c 一致; HMAC 规范字节串与 guard_cmd.c 一致。
"""
import sys
import json
import time
import struct
import hmac
import hashlib
import serial

POLY = 0x1021

# 与固件 guard_permissions.c 测试密钥一致: C 侧 `{ 0x01 }` 初始化 =
# key[0]=0x01, 其余 31B 为 0x00 (生产经 eFuse/加密 NVS 注入)
ROLES = {
    "operator":    {"id": 0, "key": bytes([0x01]) + bytes(31)},
    "maintenance": {"id": 1, "key": bytes([0x02]) + bytes(31)},
    "supervisor":  {"id": 2, "key": bytes([0x03]) + bytes(31)},
}
# 与固件动作表一致
ACTIONS = {
    "motor_run":  {"id": 1, "params": {"speed": 1}},
    "motor_stop": {"id": 2, "params": {}},
    "ping":       {"id": 0, "params": {}},
}


def crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ POLY) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def pack(type_: int, payload: bytes) -> bytes:
    assert len(payload) <= 480, "payload 超 480B 上限"
    body = struct.pack("<H", len(payload)) + payload
    crc = crc16(body)
    return b"HX" + bytes([0x01, type_]) + body + struct.pack("<H", crc)


def canonical(seq: int, action_id: int, role_id: int, params, param_ids) -> bytes:
    """HMAC 规范字节串: seq(4B LE)‖action_id(2B LE)‖role_id(1B)‖(param_id,value)*"""
    out = struct.pack("<IHB", seq, action_id, role_id)
    for name, val in params.items():
        out += struct.pack("<BI", param_ids[name], val)
    return out


def build_cmd(seq: int, action: str, role: str, params: dict) -> bytes:
    a = ACTIONS[action]
    r = ROLES[role]
    msg = canonical(seq, a["id"], r["id"], params, a["params"])
    tag = hmac.new(r["key"], msg, hashlib.sha256).hexdigest()
    obj = {"seq": seq, "role": role, "action": action, "params": params, "hmac": tag}
    return json.dumps(obj, separators=(",", ":")).encode()


def parse_reply(rx: bytes):
    if rx[:2] != b"HX" or len(rx) < 8:
        return None
    plen = struct.unpack("<H", rx[4:6])[0]
    if len(rx) < 8 + plen:
        return None
    crc_ok = struct.unpack("<H", rx[6 + plen:8 + plen])[0] == crc16(rx[4:6 + plen])
    try:
        obj = json.loads(rx[6:6 + plen])
    except Exception:
        obj = None
    return {"type": rx[3], "crc_ok": crc_ok, "obj": obj}


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    port, action = sys.argv[1], sys.argv[2]
    role = "operator"
    seq = int(time.time()) & 0x7FFFFFFF   # 默认时间戳序号 (防误命中幂等缓存)
    params = {}
    for a in sys.argv[3:]:
        if a.startswith("--seq="):
            seq = int(a.split("=", 1)[1])
        elif "=" in a:
            k, v = a.split("=", 1)
            params[k] = int(v)
        else:
            role = a

    payload = build_cmd(seq, action, role, params)
    frame = pack(0x01, payload)

    ser = serial.Serial(port, 921600, timeout=2)
    ser.setDTR(True)
    ser.setRTS(True)
    ser.write(frame)
    print(f"[TX] {len(frame)}B: {payload.decode()}")
    rx = ser.read(512)
    print(f"[RX] {len(rx)}B")
    r = parse_reply(rx)
    if r:
        print(f"[RX] type=0x{r['type']:02x} CRC={'OK' if r['crc_ok'] else 'FAIL'} "
              f"reply={r['obj']}")
    else:
        print("[RX] 无有效回执")
    ser.close()


if __name__ == "__main__":
    main()
