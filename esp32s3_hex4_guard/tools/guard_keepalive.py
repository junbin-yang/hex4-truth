#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
hex4_guard 保活循环脚本: 每 1 秒发 ping, 打印回执摘要。
用于断线测试: 拔线 → 5s 后设备红灯锁存, 回执中断;
插回 → 下一拍恢复 ALLOW (锁存解除, 灯绿)。
用法: python3 guard_keepalive.py <串口>
"""
import sys
import time
import serial
sys.path.insert(0, __import__('os').path.dirname(__file__))
from guard_cmd import build_cmd, pack, parse_reply


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    port = sys.argv[1]
    ser = serial.Serial(port, 921600, timeout=0.5)
    ser.setDTR(True)
    ser.setRTS(True)
    print(f"保活循环启动: {port} (每秒 ping, Ctrl+C 退出)", flush=True)
    base = int(time.time()) & 0x7FFFFFFF
    seq = base
    miss = 0
    while True:
        seq += 1
        t0 = time.perf_counter()
        try:
            ser.reset_input_buffer()
            ser.write(pack(0x01, build_cmd(seq, 'ping', 'operator', {})))
            # 短轮询收帧 (避免 read(n) 阻塞到超时; 按帧头+长度+CRC 收全)
            rx = b''
            while time.perf_counter() - t0 < 1.2:
                try:
                    b = ser.read(1)
                except Exception:
                    break
                if b:
                    rx += b
                    continue
                if len(rx) >= 8:
                    break
        except Exception as e:
            print(f"[t+{int(time.monotonic())}s] 串口断开: {type(e).__name__} "
                  f"(线已拔, 2s 后重试重连...)", flush=True)
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(2)
            try:
                ser = serial.Serial(port, 921600, timeout=0.5)
                ser.setDTR(True)
                ser.setRTS(True)
                print("[重连成功, 恢复保活]", flush=True)
            except Exception:
                continue
            continue
        ms = (time.perf_counter() - t0) * 1000
        r = parse_reply(rx) if len(rx) >= 8 else None
        if r and r['obj']:
            o = r['obj']
            miss = 0
            print(f"[+{ms:6.1f}ms] seq={seq} {o.get('verdict')} "
                  f"led={o.get('led')} latched={o.get('latched')} "
                  f"state={o.get('state', {}).get('sensor')}", flush=True)
        else:
            miss += 1
            print(f"[+{ms:6.1f}ms] seq={seq} 无回执 (连续 {miss} 次)", flush=True)
        time.sleep(1)


if __name__ == "__main__":
    main()
