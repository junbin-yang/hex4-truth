# -*- coding: utf-8 -*-
"""
smt_codegen.py — 验证后的约束 → C 规则表生成器

设计文档: docs/ESP32-S3安全监控器设计文档.md §6.2/§6.3
输出: guard_constraints_gen.h/.c（提交入库, 编译进 .rodata/flash）

映射（每参数一个数组, 动作表 params 指针引用, param_count=数组长度）:
  range           → GUARD_PARAM_RANGE
  enum            → GUARD_PARAM_ENUM
  combine2        → GUARD_PARAM_RANGE_LUT  (lut_bounds=验证产物的逐档边界)
  when/restrict   → GUARD_PARAM_COND       (enum_vals=when集合, lo/hi=收紧域)
  when/deny       → guard_action_gate_t[]  (动作级门控, 位图按动作 ID)

类型定义在 guard_permissions.h（运行时 N1.2 扩展）, 生成文件仅 extern 数据。
"""

from smt_dsl import ConstraintSet, DslError
from smt_verify import ConstraintVerify


GEN_H_NAME = "guard_constraints_gen.h"
GEN_C_NAME = "guard_constraints_gen.c"
GEN_STATE_H_NAME = "guard_state_gen.h"
GEN_STATE_C_NAME = "guard_state_gen.c"
BANNER = ("/* 本文件由 tools/smt_compile.py 自动生成, 勿手改.\n"
          " * 约束源: {src}\n"
          " * 重新生成并比对: python3 tools/smt_compile.py --check\n"
          " */\n")


def _c_ident(s: str) -> str:
    """yaml id/名字 → 合法 C 标识符"""
    out = []
    for ch in s:
        out.append(ch if (ch.isalnum() or ch == "_") else "_")
    return "".join(out)


def _emit_param_arrays(cs: ConstraintSet, verifies):
    """每参数聚合生成 guard_param_def_t 数组文本（.c 用）"""
    lut = {v.id: v for v in verifies}
    blocks = []
    decls = []          # (数组名, 条目数)
    data_decls = []     # 数据数组 (数组名, 项数) — extern 供手写动作表/测试引用
    for p in cs.params:
        entries = []
        for r in cs.ranges:
            if r.param == p.name:
                entries.append(
                    f"    {{ .param_id = {p.param_id}, .name = \"{p.name}\", "
                    f".kind = GUARD_PARAM_RANGE, .lo = {r.lo}u, .hi = {r.hi}u }},"
                    f"  /* {r.id} */")
        for e in cs.enums:
            if e.param == p.name:
                arr = f"g_gen_enum_{_c_ident(p.name)}"
                vals = ", ".join(f"{v}u" for v in e.values)
                entries.append(
                    f"    {{ .param_id = {p.param_id}, .name = \"{p.name}\", "
                    f".kind = GUARD_PARAM_ENUM, .lo = 0, .hi = {len(e.values)}u, "
                    f".enum_vals = {arr} }},"
                    f"  /* {e.id} */")
                blocks.append(
                    f"const uint32_t {arr}[] = {{ {vals} }};")
                data_decls.append((arr, len(e.values)))
        for c in cs.combines:
            if c.out_param == p.name:
                v = lut[c.id]
                if not v.ok or not v.lut_bounds:
                    raise DslError(f"{c.id}: 验证未通过或边界缺失, 拒绝生成")
                arr = f"g_gen_lut_{_c_ident(p.name)}"
                vals = ", ".join(f"{b}u" for b in v.lut_bounds)
                bucket = cs.param(c.bucket_var)
                upper = 0 if v.bound_op == "<=" else 1   # lo 编码比较方向
                entries.append(
                    f"    {{ .param_id = {p.param_id}, .name = \"{p.name}\", "
                    f".kind = GUARD_PARAM_RANGE_LUT, .lo = {upper}, "
                    f".hi = {len(v.lut_bounds)}u, "
                    f".lut_bounds = {arr}, "
                    f".ref_param_id = {bucket.param_id} }},"
                    f"  /* {c.id} */")
                blocks.append(
                    f"const uint32_t {arr}[] = {{ {vals} }};")
                data_decls.append((arr, len(v.lut_bounds)))
        for w in cs.whens:
            if w.then_kind == "restrict" and w.restrict_param == p.name:
                arr = f"g_gen_cond_{_c_ident(p.name)}"
                if len(w.when_values) > 15:
                    raise DslError(
                        f"{w.id}: when 值数 {len(w.when_values)} > 15 "
                        f"(运行时 when_count 上限, 封闭集)")
                vals = ", ".join(f"{x}u" for x in w.when_values)
                wp = cs.param(w.when_param)
                entries.append(
                    f"    {{ .param_id = {p.param_id}, .name = \"{p.name}\", "
                    f".kind = GUARD_PARAM_COND, "
                    f".lo = {w.restrict_lo}u, .hi = {w.restrict_hi}u, "
                    f".enum_vals = {arr}, "
                    f".when_count = {len(w.when_values)}u, "
                    f".ref_param_id = {wp.param_id} }},"
                    f"  /* {w.id} */")
                blocks.append(
                    f"const uint32_t {arr}[] = {{ {vals} }};")
                data_decls.append((arr, len(w.when_values)))
        if entries:
            name = f"g_gen_param_{_c_ident(p.name)}"
            blocks.append(f"const guard_param_def_t {name}[] = {{\n"
                          + "\n".join(entries) + "\n};")
            decls.append((name, len(entries)))
    return blocks, decls, data_decls


def _emit_gates(cs: ConstraintSet):
    """when/deny → guard_action_gate_t[] 文本（.c 用）"""
    blocks = []
    gates = []
    for i, w in enumerate(cs.whens):
        if w.then_kind != "deny":
            continue
        wp = cs.param(w.when_param)
        for a in w.deny_actions:
            if not (0 <= a < 64):
                raise DslError(
                    f"{w.id}: 动作 ID {a} 超出位图域 (封闭集: 动作 ID < 64)")
        bitmap = 0
        for a in w.deny_actions:
            bitmap |= 1 << a
        arr = f"gate_when_{i}"
        vals = ", ".join(f"{x}u" for x in w.when_values)
        blocks.append(f"static const uint32_t {arr}[] = {{ {vals} }};")
        gates.append(
            f"    {{ .ref_param_id = {wp.param_id}, "
            f".when_count = {len(w.when_values)}u, "
            f".when_values = {arr}, "
            f".deny_actions = 0x{bitmap:016x}ULL }},  /* {w.id} */")
    return blocks, gates


def _emit_state_gen(cs: ConstraintSet):
    """state_machine 段 → guard_state_gen.h/.c 文本 (无 sm 段返回 None)"""
    if cs.sm is None:
        return None, None
    sm = cs.sm
    for s in cs.states + cs.events:
        if _c_ident(s) != s:
            raise DslError(f"状态/事件名 {s!r} 非 C 标识符 (生成枚举需要)")
    # 运行时计数类型为 uint8_t, 超出即静默截断 (防转移/事件静默失效)
    if len(cs.states) > 255:
        raise DslError(f"状态数 {len(cs.states)} > 255 (运行时 uint8 计数上限)")
    if len(cs.events) > 255:
        raise DslError(f"事件数 {len(cs.events)} > 255 (运行时 uint8 计数上限)")
    if len(sm.transitions) > 255:
        raise DslError(f"转移数 {len(sm.transitions)} > 255 (运行时 uint8 计数上限)")
    state_enum = [f"    GUARD_STATE_{s} = {i}," for i, s in enumerate(cs.states)]
    state_enum.append("    GUARD_STATE_COUNT,")
    state_enum.append("    GUARD_STATE_ANY = 0xFFFFu   /* 转移表通配源状态 */")
    ev_enum = [f"    GUARD_EV_{e} = {i}," for i, e in enumerate(cs.events)]
    ev_enum.append("    GUARD_EV_COUNT")
    trans_rows = []
    for t in sm.transitions:
        src = "GUARD_STATE_ANY" if t.src == "*" else f"GUARD_STATE_{t.src}"
        pv = "0xFFFFu" if t.param is None else f"{t.param}u"
        trans_rows.append(
            f"    {{ .state = {src}, .event = GUARD_EV_{t.event}, "
            f".param = {pv}, .next = GUARD_STATE_{t.dst} }},")
    deny_rows = []
    for s in cs.states:
        bm = 0
        for a in sm.deny.get(s, []):
            bm |= 1 << a
        deny_rows.append(f"    0x{bm:08x}u,  /* {s} */")
    cev_rows = []
    for ce in sm.command_events:
        pn = f'"{ce.param_name}"' if ce.param_name else "NULL"
        cev_rows.append(
            f'    {{ .name = "{ce.event}", .param_name = {pn}, '
            f".param_id = {ce.param_id}u, .event = GUARD_EV_{ce.event} }},")
    src_note = f"{cs.package} (title: {cs.title})"
    h = BANNER.format(src=src_note) + f"""#ifndef GUARD_STATE_GEN_H
#define GUARD_STATE_GEN_H

#include "guard_state.h"

#ifdef __cplusplus
extern "C" {{
#endif

/* 状态/事件枚举 (类型 typedef 在 guard_state.h) */
enum {{
{chr(10).join(state_enum)}
}};

enum {{
{chr(10).join(ev_enum)}
}};

extern const guard_state_trans_t g_gen_state_trans[];
extern const uint8_t g_gen_state_trans_count;
extern const uint32_t g_gen_state_deny[GUARD_STATE_COUNT];
extern const char *const g_gen_state_names[GUARD_STATE_COUNT];
extern const char *const g_gen_event_names[GUARD_EV_COUNT];
extern const guard_state_event_def_t g_gen_state_events[];
extern const uint8_t g_gen_state_event_count;
extern const guard_state_id_t g_gen_state_initial;

#ifdef __cplusplus
}}
#endif

#endif /* GUARD_STATE_GEN_H */
"""
    c = BANNER.format(src=src_note) + f"""#include "guard_state_gen.h"

const guard_state_trans_t g_gen_state_trans[] = {{
{chr(10).join(trans_rows)}
}};
const uint8_t g_gen_state_trans_count = sizeof(g_gen_state_trans) / sizeof(g_gen_state_trans[0]);
const uint32_t g_gen_state_deny[GUARD_STATE_COUNT] = {{
{chr(10).join(deny_rows)}
}};
const char *const g_gen_state_names[GUARD_STATE_COUNT] = {{
    {", ".join(f'"{s}"' for s in cs.states)}
}};
const char *const g_gen_event_names[GUARD_EV_COUNT] = {{
    {", ".join(f'"{e}"' for e in cs.events)}
}};
const guard_state_event_def_t g_gen_state_events[] = {{
{chr(10).join(cev_rows) or "    /* 无指令事件 */"}
}};
const uint8_t g_gen_state_event_count = sizeof(g_gen_state_events) / sizeof(g_gen_state_events[0]);
const guard_state_id_t g_gen_state_initial = GUARD_STATE_{sm.initial};
"""
    return h, c


def generate(cs: ConstraintSet, verifies, out_dir: str):
    import os
    param_blocks, decls, data_decls = _emit_param_arrays(cs, verifies)
    gate_blocks, gates = _emit_gates(cs)
    src_note = f"{cs.package} (title: {cs.title})"

    h = BANNER.format(src=src_note) + f"""#ifndef GUARD_CONSTRAINTS_GEN_H
#define GUARD_CONSTRAINTS_GEN_H

#include "guard_permissions.h"

#ifdef __cplusplus
extern "C" {{
#endif

"""
    for name, count in decls:
        h += f"extern const guard_param_def_t {name}[];  /* {count} 条 */\n"
    if data_decls:
        h += f"\n/* 数据数组 (手写动作表/测试引用指针, 如 .lut_bounds = g_gen_lut_xxx) */\n"
        for name, count in data_decls:
            h += f"extern const uint32_t {name}[];  /* {count} 项 */\n"
    if gates:
        h += f"\nextern const guard_action_gate_t g_gen_action_gates[];\n"
        h += f"extern const uint8_t g_gen_action_gate_count;\n"
    h += f"""
#ifdef __cplusplus
}}
#endif

#endif /* GUARD_CONSTRAINTS_GEN_H */
"""

    c = BANNER.format(src=src_note) + f"""#include "guard_constraints_gen.h"

"""
    c += "\n".join(param_blocks) + "\n"
    if gates:
        c += "\n".join(gate_blocks) + "\n"
        c += f"const guard_action_gate_t g_gen_action_gates[] = {{\n"
        c += "\n".join(gates) + f"\n}};\n"
        c += f"const uint8_t g_gen_action_gate_count = "
        c += f"sizeof(g_gen_action_gates) / sizeof(g_gen_action_gates[0]);\n"
    c += "\n"

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, GEN_H_NAME), "w", encoding="utf-8") as f:
        f.write(h)
    with open(os.path.join(out_dir, GEN_C_NAME), "w", encoding="utf-8") as f:
        f.write(c)
    # N1.3: 状态机生成物 (无 state_machine 段则不生成)
    state_h, state_c = _emit_state_gen(cs)
    if state_h:
        with open(os.path.join(out_dir, GEN_STATE_H_NAME), "w",
                  encoding="utf-8") as f:
            f.write(state_h)
        with open(os.path.join(out_dir, GEN_STATE_C_NAME), "w",
                  encoding="utf-8") as f:
            f.write(state_c)
        return GEN_H_NAME, GEN_C_NAME, GEN_STATE_H_NAME, GEN_STATE_C_NAME
    return GEN_H_NAME, GEN_C_NAME, None, None
