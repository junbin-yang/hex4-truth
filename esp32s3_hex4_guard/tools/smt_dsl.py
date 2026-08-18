# -*- coding: utf-8 -*-
"""
smt_dsl.py — 物理约束 DSL 数据模型与解析层

设计文档: docs/ESP32-S3安全监控器物理约束形式化扩展设计文档.md §4.1
形状封闭集: range / enum / combine2 / when(deny|restrict) / ltl(N1.3)
数值约定: 指令参数为定点整数, DSL 声明 unit+scale, 工具归一化并检查。
"""

from dataclasses import dataclass, field
from typing import List, Optional
import yaml


class DslError(Exception):
    """DSL 语法/语义错误（含条目 ID 定位）"""


# ==================== 数据模型 ====================

@dataclass
class ActionDecl:
    """动作清单条目（供 when→deny 的 class 展开）"""
    action_id: int
    name: str
    action_class: str


@dataclass
class ParamDecl:
    """参数/变量声明（定点约定）"""
    param_id: int       # 与动作表 params 的 param_id 对齐（规范编码用）
    name: str
    unit: str
    scale: int          # 定点缩放: 物理值 × scale = 指令定点值


@dataclass
class RangeConstraint:
    id: str
    source: str
    param: str          # 变量名
    lo: int             # 定点
    hi: int             # 定点


@dataclass
class EnumConstraint:
    id: str
    source: str
    param: str
    values: List[int]   # 定点


@dataclass
class Combine2Constraint:
    """c1 * v^p (v∈{a,b}, p∈{0,1,2}) + c0 <= C, 逐档降维为 RANGE_LUT"""
    id: str
    source: str
    c1: float
    c0: float
    p1: int             # 变量1 幂
    p2: int             # 变量2 幂
    op: str             # "<=" 或 ">="
    C: float            # 物理单位常量
    bucket_var: str     # 分档变量（须有同名 ENUM 约束, 值为 bucket_domain）
    bucket_domain: List[float]   # 物理单位分档值
    out_param: str      # 被约束变量


@dataclass
class WhenConstraint:
    """when: {param: [值...]} then: deny(动作/类) | restrict(参数收紧)"""
    id: str
    source: str
    when_param: str
    when_values: List[int]      # 定点
    then_kind: str              # "deny" | "restrict"
    deny_actions: List[int]     # then_kind=deny: 展开后的动作 ID 列表
    restrict_param: str = ""    # then_kind=restrict
    restrict_lo: int = 0        # 定点
    restrict_hi: int = 0        # 定点


@dataclass
class CommandEventDecl:
    """上位机指令事件映射 (JSON action 名 → 状态机事件)"""
    event: str
    param_name: str         # "" = 无参
    param_id: int           # 0 = 无参 (验签 canon 用)


@dataclass
class StateTrans:
    """转移表条目: from(event[,param]) -> to; from="*" 通配, param=None 通配"""
    src: str
    event: str
    param: Optional[int]    # None = 通配 (无参事件或任意参数)
    dst: str


@dataclass
class StateMachineDecl:
    """状态机声明段 (N1.3)"""
    initial: str
    command_events: List[CommandEventDecl]
    deny: dict              # 状态名 -> 动作 ID 列表 (when→deny 同款展开)
    transitions: List[StateTrans]


# ---- LTL 受限语法 AST (N1.3) ----
# 封闭集: G(phi) | F(phi) | G(phi -> psi) | G(phi -> X(psi))
# phi/psi := 原子 ( ! | & | ) 组合; 原子 := 状态名 | 事件名 | allow_<类/动作名>

@dataclass
class LtlAtom:
    kind: str               # "state" / "event" / "allow"
    name: str


@dataclass
class LtlNot:
    child: object


@dataclass
class LtlBin:
    op: str                 # "&" / "|" / "->"
    left: object
    right: object


@dataclass
class LtlTemporal:
    op: str                 # "G" / "F" / "X"
    child: object


@dataclass
class ConstraintSet:
    """一个约束包的完整解析结果"""
    package: str
    title: str
    actions: List[ActionDecl]
    params: List[ParamDecl]
    states: List[str] = field(default_factory=list)
    events: List[str] = field(default_factory=list)
    ranges: List[RangeConstraint] = field(default_factory=list)
    enums: List[EnumConstraint] = field(default_factory=list)
    combines: List[Combine2Constraint] = field(default_factory=list)
    whens: List[WhenConstraint] = field(default_factory=list)
    ltls: List[dict] = field(default_factory=list)      # N1.3 解析
    sm: Optional[StateMachineDecl] = None               # N1.3 状态机段
    coverage: dict = field(default_factory=dict)        # 适用条款清单

    def param(self, name: str) -> ParamDecl:
        for p in self.params:
            if p.name == name:
                return p
        raise DslError(f"未声明的参数: {name}")

    def action_by_name(self, name: str) -> ActionDecl:
        for a in self.actions:
            if a.name == name:
                return a
        raise DslError(f"未声明的动作: {name}")


# ==================== 表达式解析（combine2 封闭模板） ====================
# 模板: {c1} * {v1}^{p1} * {v2}^{p2} [+ {c0}] {op} {C}
# 其中幂 ∈ {0,1,2}, 至多 2 个变量, c1/c0/C 为数字常量。

def _parse_expr(expr: str, cs: ConstraintSet):
    """解析 combine2 表达式, 返回 (c1, c0, p1, p2, op, C, (v1, v2))。"""
    import re
    s = expr.replace(" ", "")
    m = re.fullmatch(r"([0-9.]+)\*([a-zA-Z_][a-zA-Z0-9_]*)(\^([0-9]))?\*"
                     r"([a-zA-Z_][a-zA-Z0-9_]*)(\^([0-9]))?"
                     r"(\+([0-9.]+))?(<=|>=)([0-9.]+)", s)
    if not m:
        raise DslError(f"combine2 表达式超出封闭模板: {expr!r} "
                       f"(支持: c1*a^p1*b^p2[+c0] op C, 幂 0/1/2)")
    c1 = float(m.group(1))
    v1 = m.group(2)
    p1 = int(m.group(4) or 1)
    v2 = m.group(5)
    p2 = int(m.group(7) or 1)
    c0 = float(m.group(9) or 0.0)
    op = m.group(10)
    C = float(m.group(11))
    for v in (v1, v2):
        if v not in {p.name for p in cs.params}:
            raise DslError(f"表达式变量未声明: {v}")
    if p1 not in (0, 1, 2) or p2 not in (0, 1, 2):
        raise DslError(f"幂超出封闭集 {p1},{p2} (允许 0/1/2)")
    return c1, c0, p1, p2, op, C, (v1, v2)


# ==================== YAML 解析 ====================

def _to_fixed(value: float, scale: int) -> int:
    """物理值 → 定点整数（向零方向取整, 工具单测覆盖）"""
    return int(value * scale)


def load_constraints(path: str) -> ConstraintSet:
    with open(path, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)
    if not isinstance(raw, dict):
        raise DslError(f"{path}: 顶层须为映射")

    meta = raw.get("meta") or {}
    cs = ConstraintSet(
        package=meta.get("package", path),
        title=meta.get("title", path),
        actions=[],
        params=[],
    )

    # actions 清单
    for a in raw.get("actions", []):
        cs.actions.append(ActionDecl(
            action_id=int(a["id"]), name=str(a["name"]),
            action_class=str(a.get("class", "default"))))

    # params 声明（scale 缺省 1; unit 仅作报告元数据）
    for p in raw.get("params", []):
        cs.params.append(ParamDecl(
            param_id=int(p["id"]), name=str(p["name"]),
            unit=str(p.get("unit", "1")),
            scale=int(p.get("scale", 1))))

    # 状态/事件（N1.3 使用, 这里仅透传）
    cs.states = [str(s) for s in raw.get("states", [])]
    cs.events = [str(e) for e in raw.get("events", [])]

    # coverage 元数据
    cs.coverage = raw.get("coverage") or {}

    # 约束条目
    for i, c in enumerate(raw.get("constraints", [])):
        cid = c.get("id") or f"#{i}"
        src = c.get("source", "")
        shape = c.get("shape")
        try:
            if shape == "range":
                p = cs.param(c["var"])
                cs.ranges.append(RangeConstraint(
                    id=cid, source=src, param=p.name,
                    lo=_to_fixed(float(c["lo"]), p.scale),
                    hi=_to_fixed(float(c["hi"]), p.scale)))
            elif shape == "enum":
                p = cs.param(c["var"])
                cs.enums.append(EnumConstraint(
                    id=cid, source=src, param=p.name,
                    values=[_to_fixed(float(v), p.scale) for v in c["values"]]))
            elif shape == "combine2":
                c1, c0, p1, p2, op, C, (v1, v2) = _parse_expr(c["expr"], cs)
                cs.combines.append(Combine2Constraint(
                    id=cid, source=src, c1=c1, c0=c0, p1=p1, p2=p2, op=op, C=C,
                    bucket_var=c["bucket_var"],
                    bucket_domain=[float(v) for v in c["bucket_domain"]],
                    out_param=c["out_param"]))
            elif shape == "when":
                when = c["when"]
                if len(when) != 1:
                    raise DslError(f"{cid}: when 须恰含一个条件参数")
                wp = list(when.keys())[0]
                wvals = when[wp]
                p = cs.param(wp)
                wv_fixed = [_to_fixed(float(v), p.scale) for v in wvals]
                then = c["then"]
                if "deny" in then:
                    ids = _expand_deny(then["deny"], cs)
                    cs.whens.append(WhenConstraint(
                        id=cid, source=src, when_param=wp,
                        when_values=wv_fixed, then_kind="deny",
                        deny_actions=ids))
                elif "restrict" in then:
                    r = then["restrict"]
                    rp = cs.param(r["param"])
                    cs.whens.append(WhenConstraint(
                        id=cid, source=src, when_param=wp,
                        when_values=wv_fixed, then_kind="restrict",
                        deny_actions=[],
                        restrict_param=rp.name,
                        restrict_lo=_to_fixed(float(r["lo"]), rp.scale),
                        restrict_hi=_to_fixed(float(r["hi"]), rp.scale)))
                else:
                    raise DslError(f"{cid}: then 须为 deny 或 restrict")
            elif shape == "ltl":
                cs.ltls.append({"id": cid, "source": src,
                                "spec": str(c["spec"]),
                                "ast": _parse_ltl(str(c["spec"]), cs)})
            else:
                raise DslError(f"{cid}: 未知形状 {shape!r} "
                               f"(封闭集: range/enum/combine2/when/ltl)")
        except DslError:
            raise
        except Exception as e:
            raise DslError(f"{cid}: 解析失败: {e}") from e

    # 状态机段（N1.3, 依赖 enums 已解析: 转移参数定点化与域校验）
    if raw.get("state_machine"):
        cs.sm = _parse_state_machine(raw["state_machine"], cs)

    return cs


def _expand_deny(spec, cs: ConstraintSet) -> List[int]:
    """deny 三种写法 → 动作 ID 列表: any_motion / {class: motion} / [a,b]"""
    if spec == "any_motion":
        return [a.action_id for a in cs.actions if a.action_class == "motion"]
    if isinstance(spec, dict) and "class" in spec:
        return [a.action_id for a in cs.actions
                if a.action_class == spec["class"]]
    if isinstance(spec, list):
        ids: List[int] = []
        for n in spec:
            if n == "any_motion":
                ids += [a.action_id for a in cs.actions
                        if a.action_class == "motion"]
            else:
                ids.append(cs.action_by_name(n).action_id)
        return ids
    raise DslError(f"deny 写法不识别: {spec!r} "
                   f"(支持 any_motion / {{class: ..}} / [动作名...])")


# ==================== 状态机段解析 (N1.3) ====================

def _parse_state_machine(raw: dict, cs: ConstraintSet) -> StateMachineDecl:
    initial = str(raw.get("initial") or (cs.states[0] if cs.states else ""))
    if initial not in cs.states:
        raise DslError(f"state_machine.initial {initial!r} 未在 states 声明")
    cevents: List[CommandEventDecl] = []
    for ce in raw.get("command_events", []):
        ev = str(ce.get("event") or "")
        if ev not in cs.events:
            raise DslError(f"command_events 事件 {ev!r} 未在 events 声明")
        pn = str(ce.get("param") or "")
        pid = 0
        if pn:
            pid = cs.param(pn).param_id
        cevents.append(CommandEventDecl(event=ev, param_name=pn, param_id=pid))
    deny: dict = {}
    for st, spec in (raw.get("deny") or {}).items():
        if st not in cs.states:
            raise DslError(f"deny 状态 {st!r} 未在 states 声明")
        deny[st] = _expand_deny(spec, cs)
    # 事件 → 参数名 (transition param 定点化用)
    ev_param = {ce.event: ce.param_name for ce in cevents}
    transitions: List[StateTrans] = []
    for t in raw.get("transitions", []):
        src = str(t.get("from") or "*")
        ev = str(t.get("event") or "")
        dst = str(t.get("to") or "")
        if src != "*" and src not in cs.states:
            raise DslError(f"transition 源状态 {src!r} 未在 states 声明")
        if ev not in cs.events:
            raise DslError(f"transition 事件 {ev!r} 未在 events 声明")
        if dst not in cs.states:
            raise DslError(f"transition 目标状态 {dst!r} 未在 states 声明")
        param = None
        if "param" in t:
            pn = ev_param.get(ev) or ""
            if not pn:
                raise DslError(f"transition {src}-{ev} 带 param 但 command_events "
                               f"未声明该事件参数")
            pdecl = cs.param(pn)
            param = _to_fixed(float(t["param"]), pdecl.scale)
            en = next((x for x in cs.enums if x.param == pn), None)
            if en is not None and param not in en.values:
                raise DslError(f"transition {src}-{ev} 参数值 {param} "
                               f"不在 {pn} 的 ENUM 域内")
        transitions.append(StateTrans(src=src, event=ev, param=param, dst=dst))
    return StateMachineDecl(initial=initial, command_events=cevents,
                            deny=deny, transitions=transitions)


# ==================== LTL 受限语法解析 (N1.3) ====================

def _ltl_atom(tok: str, cs: ConstraintSet) -> LtlAtom:
    if tok.startswith("allow_"):
        name = tok[len("allow_"):]
        if name in {a.name for a in cs.actions} or \
                name in {a.action_class for a in cs.actions}:
            return LtlAtom(kind="allow", name=name)
        raise DslError(f"LTL 原子 allow_{name} 非动作名/动作类")
    if tok in cs.states:
        return LtlAtom(kind="state", name=tok)
    if tok in cs.events:
        return LtlAtom(kind="event", name=tok)
    raise DslError(f"LTL 原子 {tok!r} 非状态/事件/allow_<类或动作>")


def _parse_ltl(spec: str, cs: ConstraintSet):
    """受限 LTL: G(phi) | F(phi) | G(phi -> psi) | G(phi -> X(psi))
    phi/psi := 原子组合 (! & |), 原子 := 状态名 | 事件名 | allow_<类/动作名>"""
    import re
    toks = re.findall(r"G(?![A-Za-z0-9_])|F(?![A-Za-z0-9_])|X(?![A-Za-z0-9_])"
                      r"|->|[()!&|]|allow_[A-Za-z_][A-Za-z0-9_]*"
                      r"|[A-Za-z_][A-Za-z0-9_]*", spec)
    if "".join(toks) != spec.replace(" ", ""):
        raise DslError(f"LTL 含未识别片段: {spec!r} "
                       f"(封闭集: G/F/X/!/&/|/-> + 状态/事件/allow 原子)")
    pos = [0]

    def peek():
        return toks[pos[0]] if pos[0] < len(toks) else None

    def take():
        t = toks[pos[0]]
        pos[0] += 1
        return t

    def parse_atom():
        t = take()
        if t == "!":
            return LtlNot(parse_atom())
        if t == "X":
            return LtlTemporal("X", parse_atom())
        if t == "(":
            node = parse_phi()
            if take() != ")":
                raise DslError("LTL 缺右括号 ')'")
            return node
        return _ltl_atom(t, cs)

    def parse_term():
        node = parse_atom()
        while peek() in ("&", "|"):
            op = take()
            node = LtlBin(op, node, parse_atom())
        return node

    def parse_phi():
        return parse_term()

    op = take()
    if op not in ("G", "F"):
        raise DslError(f"LTL 顶层算子仅支持 G/F, 不支持 {op!r} "
                       f"(X 仅限 G(φ→X(ψ)) 的箭头右侧)")
    if take() != "(":
        raise DslError(f"LTL {op} 后须紧跟 '('")
    if op == "F":
        node = LtlTemporal("F", parse_phi())
    else:                       # G(...) 或 G(phi -> ...)
        left = parse_phi()
        if peek() == "->":
            take()
            right = parse_term()    # 允许 X 前缀原子 (X 语义由验证器解释)
            left = LtlBin("->", left, right)
        node = LtlTemporal("G", left)
    if take() != ")":
        raise DslError("LTL 缺右括号 ')'")
    if pos[0] != len(toks):
        raise DslError(f"LTL 尾部多余记号: {' '.join(toks[pos[0]:])}")
    return node
