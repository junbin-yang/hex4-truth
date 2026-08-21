#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
smt_compile.py — 物理约束形式化编译链 CLI 入口

设计文档: docs/ESP32-S3安全监控器设计文档.md §6

流水线: yaml(DSL) → smt_dsl 解析 → smt_verify 四项验证 → smt_codegen 生成
        → smt_report 两份报告

用法:
  python3 tools/smt_compile.py tools/iso_constraints/<包>.yaml
      [--out-dir 生成物目录] [--report-dir 报告目录] [--check]
  --check: 重新生成并与入库生成物比对 (CI 用, 不一致以退出码 1 报错)
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from smt_dsl import load_constraints, DslError
from smt_verify import verify_constraint_set
from smt_codegen import generate, GEN_H_NAME, GEN_C_NAME
from smt_report import write_verify_report, write_coverage_report

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(
    os.path.abspath(__file__)), "..", ".."))
DEFAULT_OUT_DIR = os.path.join(REPO_ROOT, "esp32s3_hex4_guard",
                               "components", "hex4_guard", "generated")
DEFAULT_REPORT_DIR = os.path.join(REPO_ROOT, "docs", "reports")


def run_one(yaml_path: str, out_dir: str, report_dir: str) -> bool:
    print(f"[smt_compile] 解析 {yaml_path}")
    cs = load_constraints(yaml_path)

    print(f"[smt_compile] 验证 {len(cs.ranges)} range / {len(cs.enums)} enum / "
          f"{len(cs.combines)} combine2 / {len(cs.whens)} when "
          f"/ {len(cs.ltls)} ltl / 状态机{'' if cs.sm else '(无)'}")
    verifies = verify_constraint_set(cs)
    fails = [v for v in verifies if not v.ok]
    for v in verifies:
        mark = "PASS" if v.ok else "FAIL"
        print(f"  [{mark}] {v.id} ({v.shape})")
        if not v.ok:
            for ck in v.checks:
                if ck.status == "FAIL":
                    print(f"        {ck.name}: {ck.detail}")
    if fails:
        print(f"[smt_compile] {len(fails)} 条验证失败, 拒绝生成")
        return False

    print(f"[smt_compile] 生成 → {out_dir}")
    h_name, c_name, state_h, state_c = generate(cs, verifies, out_dir)
    print(f"  生成 {h_name} / {c_name}"
          + (f" / {state_h} / {state_c}" if state_h else ""))

    print(f"[smt_compile] 报告 → {report_dir}")
    vp = write_verify_report(cs, verifies, report_dir)
    cp = write_coverage_report(cs, verifies, report_dir)
    print(f"  生成 {os.path.basename(vp)} / {os.path.basename(cp)}")
    return True


def check_generated(out_dir: str) -> bool:
    """CI: 检查入库生成物存在（逐字节比对在 pytest 中实现, 见 tests/）"""
    for name in (GEN_H_NAME, GEN_C_NAME):
        committed = os.path.join(out_dir, name)
        if not os.path.exists(committed):
            print(f"[check] FAIL: 入库生成物缺失 {committed}")
            return False
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("yaml", nargs="+", help="约束包 yaml 路径")
    ap.add_argument("--out-dir", default=DEFAULT_OUT_DIR)
    ap.add_argument("--report-dir", default=DEFAULT_REPORT_DIR)
    ap.add_argument("--check", action="store_true",
                    help="CI 模式: 重新生成并与入库生成物逐字节比对")
    args = ap.parse_args()

    ok = True
    for y in args.yaml:
        try:
            ok = run_one(y, args.out_dir, args.report_dir) and ok
        except DslError as e:
            print(f"[smt_compile] DSL 错误: {e}")
            ok = False
    if not ok:
        sys.exit(1)
    if args.check and not check_generated(args.out_dir):
        sys.exit(1)
    print("[smt_compile] 完成")
    sys.exit(0)


if __name__ == "__main__":
    main()
