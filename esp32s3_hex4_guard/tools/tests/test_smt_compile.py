# -*- coding: utf-8 -*-
"""
test_smt_compile.py — 工具链 pytest

覆盖: DSL 解析（含错误注入）/ 四项验证（含故意矛盾检出）/
     代码生成（gcc -fsyntax-only）/ 报告生成 / CLI 端到端
"""

import os
import subprocess
import sys

import pytest

from conftest import TOOLS_DIR, FIXTURES

sys.path.insert(0, TOOLS_DIR)

from smt_dsl import load_constraints, DslError
from smt_verify import verify_constraint_set
from smt_codegen import (generate, GEN_H_NAME, GEN_C_NAME,
                         GEN_STATE_H_NAME, GEN_STATE_C_NAME)
from smt_report import write_verify_report, write_coverage_report

DEMO_YAML = os.path.join(TOOLS_DIR, "iso_constraints", "demo_collab.yaml")
DEMO_DIR = os.path.join(FIXTURES, "demo_pkg")

# ==================== DSL 解析 ====================


def _write_demo_bad(name, constraints_yaml):
    """生成一个与 demo 同构但约束不同的临时 yaml"""
    import yaml
    with open(DEMO_YAML, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)
    raw["constraints"] = yaml.safe_load(constraints_yaml)
    path = os.path.join(FIXTURES, name)
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(raw, f, allow_unicode=True)
    return path


def _write_demo_sm(name, sm_yaml):
    """生成与 demo 同构但 state_machine 段不同的临时 yaml"""
    import yaml
    with open(DEMO_YAML, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)
    raw["state_machine"] = yaml.safe_load(sm_yaml)
    path = os.path.join(FIXTURES, name)
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(raw, f, allow_unicode=True)
    return path


def test_load_demo_package():
    cs = load_constraints(DEMO_YAML)
    assert cs.package == "demo_collab"
    assert len(cs.actions) == 3
    assert len(cs.params) == 5
    assert len(cs.ranges) == 2
    assert len(cs.enums) == 3
    assert len(cs.combines) == 1
    assert len(cs.whens) == 3
    assert len(cs.ltls) == 4
    # 定点换算检查: 0.25 m/s × 1000
    r = cs.ranges[0]
    assert (r.lo, r.hi) == (0, 250)


def test_unsupported_shape_rejected():
    path = _write_demo_bad("bad_shape.yaml", """
      - { id: X1, source: t, shape: fuzzy, var: tcp_speed, lo: 0, hi: 1 }
    """)
    with pytest.raises(DslError, match="未知形状"):
        load_constraints(path)


def test_unsupported_expr_rejected():
    # 3 变量表达式超出 combine2 封闭模板
    path = _write_demo_bad("bad_expr.yaml", """
      - id: X2
        source: t
        shape: combine2
        expr: "0.5 * payload * tcp_speed^2 * tcp_force <= 10.0"
        bucket_var: payload
        bucket_domain: [0, 1]
        out_param: tcp_speed
    """)
    with pytest.raises(DslError, match="封闭模板"):
        load_constraints(path)


# ==================== 四项验证 ====================


def test_verify_demo_all_pass():
    cs = load_constraints(DEMO_YAML)
    verifies = verify_constraint_set(cs)
    assert len(verifies) == 14         # 10 约束 + 状态机 + 4 ltl
    assert all(v.ok for v in verifies), \
        [c.detail for v in verifies for c in v.checks if c.status != "PASS"]


def test_ltl_bmc_all_pass():
    """4 条 LTL (G/F/G→X) BMC 验证通过, 含证明口径"""
    cs = load_constraints(DEMO_YAML)
    verifies = {v.id: v for v in verify_constraint_set(cs)}
    for vid in ("LTL-ESTOP-1", "LTL-DOOR-1", "LTL-COLLISION-1", "LTL-REACH-1"):
        v = verifies[vid]
        assert v.ok, [c.detail for c in v.checks if c.status != "PASS"]
        assert any(c.name == "bmc" and c.status == "PASS" for c in v.checks)
    # 状态机静态检查: 确定性/可达性/deny 域/abort 必需转移
    sm = verifies["STATE-MACHINE"]
    assert sm.ok
    names = {c.name for c in sm.checks}
    assert {"determinism", "reachability", "deny-domain", "abort-trans"} <= names


def test_combine2_bounds_energy_limit():
    """½·m·v² ≤ 0.01 (10mJ 演示取值) 的逐档上界（定点, scale=1000, v 单位 m/s, 域上限=250）"""
    import math
    cs = load_constraints(DEMO_YAML)
    verifies = {v.id: v for v in verify_constraint_set(cs)}
    v = verifies["TS15066-5.5-1"]
    assert v.ok
    # m=0 档: 能量恒满足 → 边界退化为 RANGE 上界 (0.25m/s → 250)
    # m=1: sqrt(0.02), m=2: sqrt(0.01), m=5: sqrt(0.004), m=10: sqrt(0.002)
    expect = [250,
              int(math.sqrt(0.02) * 1000),
              int(math.sqrt(0.01) * 1000),
              int(math.sqrt(0.004) * 1000),
              int(math.sqrt(0.002) * 1000)]
    assert v.lut_bounds == expect


def test_conflict_ranges_detected():
    """同参数两条互斥 RANGE → consistency FAIL"""
    path = _write_demo_bad("bad_conflict.yaml", """
      - id: R1
        source: "ISO 10218-1 §5.12.3"
        shape: range
        var: tcp_speed
        lo: 100
        hi: 250
      - id: R2
        source: "ISO 10218-1 §5.12.4"
        shape: range
        var: tcp_speed
        lo: 0
        hi: 50
    """)
    cs = load_constraints(path)
    verifies = verify_constraint_set(cs)
    assert any(not v.ok for v in verifies)
    bad = [v for v in verifies if not v.ok]
    assert any(c.name == "consistency" and c.status == "FAIL"
               for v in bad for c in v.checks)


def test_bucket_missing_enum_detected():
    """combine2 缺 bucket ENUM → bucket-complete FAIL"""
    path = _write_demo_bad("bad_bucket.yaml", """
      - id: X3
        source: t
        shape: combine2
        expr: "0.5 * payload * tcp_speed^2 <= 10.0"
        bucket_var: tcp_force
        bucket_domain: [0, 1]
        out_param: tcp_speed
    """)
    cs = load_constraints(path)
    verifies = verify_constraint_set(cs)
    bad = [v for v in verifies if not v.ok]
    assert any(c.name == "bucket-complete" and c.status == "FAIL"
               for v in bad for c in v.checks)


def test_when_value_outside_enum_detected():
    """when 值不在参数 ENUM 域内 → consistency FAIL"""
    path = _write_demo_bad("bad_when.yaml", """
      - id: X4
        source: t
        shape: when
        when: { safety_door: [9] }
        then: { deny: any_motion }
    """)
    cs = load_constraints(path)
    verifies = verify_constraint_set(cs)
    bad = [v for v in verifies if not v.ok]
    assert any(c.name == "consistency" and c.status == "FAIL"
               for v in bad for c in v.checks)


# ==================== 状态机与 LTL (N1.3) ====================


def test_ltl_unknown_atom_rejected():
    """LTL 引用未声明状态 → DslError"""
    path = _write_demo_bad("bad_ltl.yaml", """
      - id: X6
        source: t
        shape: ltl
        spec: "G(NOT_A_STATE)"
    """)
    with pytest.raises(DslError, match="原子"):
        load_constraints(path)


def test_duplicate_transition_detected():
    """重复转移 (同 src/event/param) → determinism FAIL"""
    path = _write_demo_sm("bad_dup_trans.yaml", """
      initial: IDLE
      command_events:
        - { event: mode_switch, param: mode }
      transitions:
        - { from: IDLE, event: mode_switch, param: 0, to: AUTO }
        - { from: IDLE, event: mode_switch, param: 0, to: MANUAL }
    """)
    cs = load_constraints(path)
    verifies = verify_constraint_set(cs)
    sm = next(v for v in verifies if v.id == "STATE-MACHINE")
    assert not sm.ok
    assert any(c.name == "determinism" and c.status == "FAIL"
               for c in sm.checks)


def test_ltl_violation_detected():
    """deny 缺失 (锁存态允许 motion) → G(¬(ESTOP_LATCH∧allow_motion)) BMC 反例 FAIL"""
    path = _write_demo_sm("bad_ltl_violate.yaml", """
      initial: IDLE
      transitions:
        - { from: "*", event: estop_release, to: ESTOP_LATCH }
        - { from: ESTOP_LATCH, event: operator_ack, to: IDLE }
    """)
    cs = load_constraints(path)
    verifies = verify_constraint_set(cs)
    v = next(x for x in verifies if x.id == "LTL-ESTOP-1")
    assert not v.ok
    assert any(c.name == "bmc" and c.status == "FAIL" for c in v.checks)


def test_unreachable_state_detected():
    """孤立状态 (无入边) → reachability FAIL"""
    path = _write_demo_sm("bad_unreach.yaml", """
      initial: IDLE
      transitions:
        - { from: "*", event: estop_release, to: ESTOP_LATCH }
    """)
    cs = load_constraints(path)
    verifies = verify_constraint_set(cs)
    sm = next(v for v in verifies if v.id == "STATE-MACHINE")
    assert not sm.ok
    assert any(c.name == "reachability" and c.status == "FAIL"
               for c in sm.checks)


def test_top_level_x_rejected():
    """顶层 X(φ) 不属于封闭集 → DslError (此前被静默解析为 G(φ))"""
    path = _write_demo_bad("bad_toplevel_x.yaml", """
      - id: X7
        source: t
        shape: ltl
        spec: "X(ESTOP_LATCH)"
    """)
    with pytest.raises(DslError, match="顶层算子"):
        load_constraints(path)


def test_wildcard_overlap_detected():
    """通配 src+通配 param 与具体条目重叠 → determinism FAIL"""
    path = _write_demo_sm("bad_overlap.yaml", """
      initial: IDLE
      command_events:
        - { event: mode_switch, param: mode }
      transitions:
        - { from: IDLE, event: mode_switch, param: 1, to: MANUAL }
        - { from: "*", event: mode_switch, to: COLLAB }
    """)
    cs = load_constraints(path)
    verifies = verify_constraint_set(cs)
    sm = next(v for v in verifies if v.id == "STATE-MACHINE")
    assert not sm.ok
    assert any(c.name == "determinism" and c.status == "FAIL"
               for c in sm.checks)


def test_deny_bitmap_over_31_detected():
    """deny 动作 ID ≥32 → uint32 位图截断 fail-open → FAIL"""
    import yaml
    with open(DEMO_YAML, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)
    raw["actions"].append({"id": 35, "name": "big_move", "class": "motion"})
    raw["state_machine"] = yaml.safe_load("""
      initial: IDLE
      deny:
        ESTOP_LATCH: [big_move]
      transitions:
        - { from: "*", event: estop_release, to: ESTOP_LATCH }
    """)
    path = os.path.join(FIXTURES, "bad_deny32.yaml")
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(raw, f, allow_unicode=True)
    cs = load_constraints(path)
    verifies = verify_constraint_set(cs)
    sm = next(v for v in verifies if v.id == "STATE-MACHINE")
    assert not sm.ok
    assert any(c.name == "deny-domain" and c.status == "FAIL"
               for c in sm.checks)


def test_bmc_deep_violation_detected():
    """10 状态链上 G(!S9) 在第 9 步违例 → BMC 深度自适应检出 FAIL
    (此前固定 k=8 产生假 PASS)"""
    import yaml
    with open(DEMO_YAML, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)
    raw["states"] = [f"S{i}" for i in range(10)]
    raw["events"] = [f"e{i}" for i in range(9)]
    raw["state_machine"] = yaml.safe_load(f"""
      initial: S0
      transitions:
""" + "\n".join(
        f"        - {{ from: S{i}, event: e{i}, to: S{i+1} }}"
        for i in range(9)))
    raw["constraints"] = [
        {"id": "LT-DEEP", "source": "t", "shape": "ltl",
         "spec": "G(!S9)"},
    ]
    path = os.path.join(FIXTURES, "bad_bmc_deep.yaml")
    with open(path, "w", encoding="utf-8") as f:
        yaml.safe_dump(raw, f, allow_unicode=True)
    cs = load_constraints(path)
    verifies = verify_constraint_set(cs)
    v = next(x for x in verifies if x.id == "LT-DEEP")
    assert not v.ok
    assert any(c.name == "bmc" and c.status == "FAIL" for c in v.checks)


# ==================== 代码生成 ====================


def test_codegen_output_compiles():
    cs = load_constraints(DEMO_YAML)
    verifies = verify_constraint_set(cs)
    assert all(v.ok for v in verifies)
    out = os.path.join(DEMO_DIR, "generated")
    generate(cs, verifies, out)

    h = open(os.path.join(out, GEN_H_NAME), encoding="utf-8").read()
    c = open(os.path.join(out, GEN_C_NAME), encoding="utf-8").read()

    # 期望条目存在
    assert "GUARD_PARAM_RANGE_LUT" in c
    assert "GUARD_PARAM_COND" in c
    assert "g_gen_param_tcp_speed" in h
    assert "g_gen_action_gates" in h
    # 数据数组 extern 化 (手写动作表/测试引用指针)
    assert "g_gen_lut_tcp_speed" in h
    assert "g_gen_enum_payload" in h
    # 能量限表: m=10kg 档上界 44 (0.044 m/s, 10mJ 演示取值)
    assert "44u" in c
    # deny 位图: 动作 1|2 → 0x6
    assert "0x0000000000000006ULL" in c
    # COND when 集合长度字段 (无哨兵, 运行时按 when_count 扫描)
    assert ".when_count = 1u" in c

    # gcc -fsyntax-only（stub 头形状与 N1.2 扩展后的 guard_permissions.h 一致）
    r = subprocess.run(
        ["gcc", "-fsyntax-only", "-Wall", "-I", FIXTURES, "-I", out,
         os.path.join(out, GEN_C_NAME)],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


def test_codegen_state_gen_compiles():
    """状态机生成物: 枚举/转移表/deny 位图/指令事件, gcc -fsyntax-only 通过"""
    cs = load_constraints(DEMO_YAML)
    verifies = verify_constraint_set(cs)
    assert all(v.ok for v in verifies)
    out = os.path.join(DEMO_DIR, "generated")
    generate(cs, verifies, out)

    sh = open(os.path.join(out, GEN_STATE_H_NAME), encoding="utf-8").read()
    sc = open(os.path.join(out, GEN_STATE_C_NAME), encoding="utf-8").read()
    assert "GUARD_STATE_ESTOP_LATCH" in sh
    assert "GUARD_EV_mode_switch" in sh
    assert "GUARD_STATE_ANY = 0xFFFFu" in sh
    assert "g_gen_state_trans" in sh
    assert "0x00000006u" in sc          # ESTOP_LATCH deny 位图 (动作 1|2)
    assert "GUARD_STATE_ANY" in sc      # 通配源状态转移
    assert "g_gen_state_initial" in sc
    assert '"mode_switch"' in sc        # 指令事件映射

    r = subprocess.run(
        ["gcc", "-fsyntax-only", "-Wall", "-I", FIXTURES, "-I", out,
         os.path.join(out, GEN_STATE_C_NAME)],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr


# ==================== 报告与 CLI ====================


def test_reports_written():
    cs = load_constraints(DEMO_YAML)
    verifies = verify_constraint_set(cs)
    out = os.path.join(DEMO_DIR, "reports")
    vp = write_verify_report(cs, verifies, out)
    cp = write_coverage_report(cs, verifies, out)
    vtext = open(vp, encoding="utf-8").read()
    ctext = open(cp, encoding="utf-8").read()
    assert "TS15066-5.5-1" in vtext and "equiv" in vtext
    assert "100.0%" in ctext          # 9/9 条款全覆盖（demo 包, N1.4 清单）
    assert "5.12.4" in ctext and "5.5.6" in ctext


def test_cli_end_to_end(tmp_path):
    r = subprocess.run(
        [sys.executable, os.path.join(TOOLS_DIR, "smt_compile.py"),
         DEMO_YAML, "--out-dir", str(tmp_path / "gen"),
         "--report-dir", str(tmp_path / "rep")],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    assert os.path.exists(tmp_path / "gen" / GEN_C_NAME)
    assert os.path.exists(tmp_path / "gen" / GEN_STATE_C_NAME)
    assert os.path.exists(tmp_path / "rep" / "smt_verify_report.md")


def test_cli_fails_on_bad_yaml(tmp_path):
    bad = _write_demo_bad("bad_cli.yaml", """
      - id: X5
        source: t
        shape: range
        var: tcp_speed
        lo: 300
        hi: 100
    """)
    r = subprocess.run(
        [sys.executable, os.path.join(TOOLS_DIR, "smt_compile.py"),
         bad, "--out-dir", str(tmp_path / "gen"),
         "--report-dir", str(tmp_path / "rep")],
        capture_output=True, text=True)
    assert r.returncode == 1
    assert not os.path.exists(tmp_path / "gen" / GEN_C_NAME)
