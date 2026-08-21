# -*- coding: utf-8 -*-
"""
smt_verify.py — 约束的 SMT 四项验证（z3）

设计文档: docs/ESP32-S3安全监控器设计文档.md §6.2
四项验证:
  ① 可满足性     约束本体 sat（不自相矛盾 / 定义域非空）
  ② 一致性       同参数多约束交集非空
  ③ 编译等价性   table_check(v) ↔ DSL约束(v), 在定点离散域上严格等价
  ④ 降维完备性   combine2 分档降维: bucket ENUM 齐备 + 边界由 z3 Optimize 求解

关键口径（写入验证报告）:
  - 指令参数本身是定点整数, 等价性验证域 = 定点离散域 [0, Vmax]∩Z,
    降维边界由 z3 Optimize 直接求最大/最小允许定点值, 取整方向天然保守正确。
  - range/enum/when 的规则表数据与 DSL 直通（无降维变换）,
    等价性为平凡成立, 验证记录注明"直通"。
"""

from dataclasses import dataclass, field
from typing import List
import z3

from smt_dsl import (ConstraintSet, RangeConstraint, EnumConstraint,
                     Combine2Constraint, WhenConstraint,
                     LtlAtom, LtlNot, LtlBin, LtlTemporal, DslError)


# ==================== 结果结构 ====================

@dataclass
class CheckRecord:
    name: str
    status: str            # PASS / FAIL / N-A(直通)
    detail: str


@dataclass
class ConstraintVerify:
    id: str
    source: str
    shape: str
    checks: List[CheckRecord] = field(default_factory=list)
    # combine2 验证产物: 每档允许的定点上界/下界 (codegen 直接消费)
    lut_bounds: List[int] = field(default_factory=list)
    bound_op: str = ""     # "<="(上界表) 或 ">="(下界表)

    @property
    def ok(self) -> bool:
        # FAIL 是唯一不通过状态; N-A=直通验证项(不适用), 视同通过
        return all(c.status != "FAIL" for c in self.checks)


def _ck(v: ConstraintVerify, name: str, status: str, detail: str):
    v.checks.append(CheckRecord(name, status, detail))


# ==================== 域收集 ====================

def _collect_domain(cs: ConstraintSet, param: str):
    """运行时有效域上界 = 参数 RANGE 约束的 hi（指令判定实际可达的最大定点值）;
    无 RANGE 时用约束中出现最大边界。等价性验证与 Optimize 上界均用此域。"""
    hi = None
    for r in cs.ranges:
        if r.param == param:
            hi = r.hi if hi is None else max(hi, r.hi)
    if hi is not None:
        return hi
    vmax = 0
    for e in cs.enums:
        if e.param == param:
            vmax = max(vmax, max(e.values, default=0))
    for w in cs.whens:
        if w.when_param == param:
            vmax = max(vmax, max(w.when_values, default=0))
        if w.then_kind == "restrict" and w.restrict_param == param:
            vmax = max(vmax, w.restrict_hi)
    if vmax == 0:
        raise DslError(f"参数 {param} 缺少边界信息, 无法确定验证域")
    return vmax


def _expr_to_z3(c: Combine2Constraint, v_real, m_real):
    """DSL 表达式 → z3 Real 表达式（v=out_param, m=bucket_var 的物理值）"""
    terms = [c.c1]
    for var, pw in ((c.bucket_var, c.p1), (c.out_param, c.p2)):
        if pw > 0:
            val = m_real if var == c.bucket_var else v_real
            terms.append(val ** pw if pw > 1 else val)
    lhs = terms[0]
    for t in terms[1:]:
        lhs = lhs * t
    lhs = lhs + c.c0
    return (lhs <= c.C) if c.op == "<=" else (lhs >= c.C)


# ==================== 各项验证 ====================

def _verify_range(cs: ConstraintSet, r: RangeConstraint) -> ConstraintVerify:
    v = ConstraintVerify(r.id, r.source, "range")
    # ① 可满足性
    _ck(v, "sat", "PASS" if r.lo <= r.hi else "FAIL",
        f"区间 [{r.lo}, {r.hi}] {'非空' if r.lo <= r.hi else '为空(下界>上界)'}")
    # ② 一致性: 同参数各 RANGE 交集非空（本条目与同参数其他条目）
    for r2 in cs.ranges:
        if r2 is not r and r2.param == r.param:
            lo = max(r.lo, r2.lo)
            hi = min(r.hi, r2.hi)
            _ck(v, "consistency",
                "PASS" if lo <= hi else "FAIL",
                f"与 {r2.id} 交集 [{lo},{hi}] "
                f"{'非空' if lo <= hi else '为空(条款冲突)'}")
    # ③ 等价性: 直通
    _ck(v, "equiv", "N-A", "RANGE 规则表与 DSL 数据直通, 无降维变换")
    return v


def _verify_enum(cs: ConstraintSet, e: EnumConstraint) -> ConstraintVerify:
    v = ConstraintVerify(e.id, e.source, "enum")
    uniq = len(set(e.values)) == len(e.values)
    _ck(v, "sat", "PASS" if e.values and uniq else "FAIL",
        f"枚举 {len(e.values)} 值"
        f"{', 互异' if uniq else ', 存在重复(定义域歧义)' if e.values else '(空集)'}")
    # ② 一致性: 枚举值须落在同参数全部 RANGE 区间内
    for r in cs.ranges:
        if r.param == e.param:
            outside = [x for x in e.values if not (r.lo <= x <= r.hi)]
            _ck(v, "consistency",
                "PASS" if not outside else "FAIL",
                f"与 {r.id}: 域外值 {outside or '无'}")
    _ck(v, "equiv", "N-A", "ENUM 规则表与 DSL 数据直通, 无降维变换")
    return v


def _verify_combine(cs: ConstraintSet, c: Combine2Constraint) -> ConstraintVerify:
    v = ConstraintVerify(c.id, c.source, "combine2")
    v.bound_op = c.op

    out_scale = cs.param(c.out_param).scale
    b_scale = cs.param(c.bucket_var).scale

    # ④a 降维完备性前置: bucket 参数须有 ENUM 约束且值集 = bucket_domain(定点化)
    domain_fixed = [int(x * b_scale) for x in c.bucket_domain]
    bucket_enum = next((e for e in cs.enums if e.param == c.bucket_var), None)
    if bucket_enum is None:
        _ck(v, "bucket-complete", "FAIL",
            f"分档参数 {c.bucket_var} 缺少 ENUM 约束(须声明值域=bucket_domain)")
        return v
    if sorted(bucket_enum.values) != sorted(domain_fixed):
        _ck(v, "bucket-complete", "FAIL",
            f"ENUM {bucket_enum.id} 值集 {sorted(bucket_enum.values)} "
            f"≠ bucket_domain 定点化 {sorted(domain_fixed)}")
        return v
    _ck(v, "bucket-complete", "PASS",
        f"ENUM {bucket_enum.id} 值集与 bucket_domain 一致, 共 {len(domain_fixed)} 档")

    # ① 可满足性: 每档都存在满足点
    for m_phys, m_fixed in zip(c.bucket_domain, domain_fixed):
        s = z3.Solver()
        v_real = z3.Real("v")
        m_real = z3.Real("m")
        cond = _expr_to_z3(c, v_real, m_real)
        s.add(m_real == z3.RealVal(str(m_phys)))
        s.add(v_real >= 0)
        s.add(cond)
        if s.check() != z3.sat:
            _ck(v, "sat", "FAIL", f"档位 {m_phys} 无满足点 (表达式与常数矛盾)")
            return v
    _ck(v, "sat", "PASS", f"{len(domain_fixed)} 档均存在满足点")

    # ④b 逐档边界求解: z3 Optimize 求最大/最小允许定点值 (离散域, 上界=运行时有效域)
    vmax = _collect_domain(cs, c.out_param)
    bounds = []
    for m_phys in c.bucket_domain:
        o = z3.Optimize()
        v_int = z3.Int("v_int")
        m_real = z3.Real("m")
        v_real = z3.Real("v")
        o.add(m_real == z3.RealVal(str(m_phys)))
        o.add(v_int >= 0, v_int <= vmax)
        o.add(v_real == z3.ToReal(v_int) / out_scale)
        o.add(_expr_to_z3(c, v_real, m_real))
        if c.op == "<=":
            o.maximize(v_int)
        else:
            o.minimize(v_int)
        if o.check() != z3.sat:
            _ck(v, "bound-solve", "FAIL", f"档位 {m_phys} 边界求解失败")
            return v
        bounds.append(o.model()[v_int].as_long())
    v.lut_bounds = bounds
    _ck(v, "bound-solve", "PASS",
        f"逐档{'上' if c.op == '<=' else '下'}界定点: {bounds}")

    # ③ 编译等价性: ∀v_int ∈ [0,Vmax]∩Z. (v_int <= bound_k) ↔ DSL约束(v_int/scale, m_k)
    for k, (m_phys, bound) in enumerate(zip(c.bucket_domain, bounds)):
        s = z3.Solver()
        v_int = z3.Int("v_int")
        m_real = z3.Real("m")
        v_real = z3.Real("v")
        table_ok = (v_int <= bound) if c.op == "<=" else (v_int >= bound)
        dsl_ok = _expr_to_z3(c, v_real, m_real)
        s.add(m_real == z3.RealVal(str(m_phys)))
        s.add(v_real == z3.ToReal(v_int) / out_scale)
        s.add(v_int >= 0, v_int <= vmax)
        s.add(table_ok != dsl_ok)          # 找反例: 表判定与 DSL 判定不一致
        if s.check() != z3.unsat:
            cex = s.model()
            _ck(v, "equiv", "FAIL",
                f"档位 {m_phys} 反例 v_int={cex[v_int]} (表与DSL判定不一致)")
            return v
    _ck(v, "equiv", "PASS",
        f"{len(bounds)} 档 × 域[0,{vmax}] 离散等价, 无反例 (ForAll 证否 unsat)")
    return v


def _verify_when(cs: ConstraintSet, w: WhenConstraint) -> ConstraintVerify:
    v = ConstraintVerify(w.id, w.source, f"when/{w.then_kind}")
    # ① 可满足性: when 值集合非空且互异
    uniq = len(set(w.when_values)) == len(w.when_values)
    _ck(v, "sat", "PASS" if w.when_values and uniq else "FAIL",
        f"when 集合 {len(w.when_values)} 值"
        f"{'' if uniq else ', 存在重复' if w.when_values else '(空集)'}")
    # ② 一致性: when 值须落在 when 参数的 ENUM/RANGE 域内
    param_dom = [e for e in cs.enums if e.param == w.when_param]
    if not param_dom:
        _ck(v, "consistency", "FAIL",
            f"when 参数 {w.when_param} 缺少 ENUM 定义域(DSL 不完整)")
    else:
        in_enum = all(any(x in e.values for e in param_dom)
                      for x in w.when_values)
        _ck(v, "consistency",
            "PASS" if in_enum else "FAIL",
            f"when 值全部落在 {w.when_param} 的 ENUM 域内: {in_enum}")
    if w.then_kind == "restrict":
        # 收紧域与原 RANGE 交集非空（收紧后参数域不为空）
        for r in cs.ranges:
            if r.param == w.restrict_param:
                lo = max(r.lo, w.restrict_lo)
                hi = min(r.hi, w.restrict_hi)
                _ck(v, "consistency",
                    "PASS" if lo <= hi else "FAIL",
                    f"收紧域 [{w.restrict_lo},{w.restrict_hi}] 与 {r.id} "
                    f"交集 {'非空' if lo <= hi else '为空(收紧后无合法值)'}")
    else:
        known_ids = {a.action_id for a in cs.actions}
        bad = [a for a in w.deny_actions if a not in known_ids]
        _ck(v, "consistency",
            "PASS" if not bad else "FAIL",
            f"deny 动作 ID {bad or '全部'} 均在动作清单内")
    _ck(v, "equiv", "N-A", "when 规则表与 DSL 数据直通, 无降维变换")
    return v


# ==================== 状态机与 LTL 验证 (N1.3, z3 BMC) ====================

def _verify_state_machine(cs: ConstraintSet) -> ConstraintVerify:
    """状态机静态检查: 声明/确定性/可达性/deny 域"""
    v = ConstraintVerify("STATE-MACHINE", "state_machine", "sm")
    if cs.sm is None:
        _ck(v, "declared", "N-A", "无 state_machine 段")
        return v
    sm = cs.sm
    # 确定性: 任意两条转移的 (源状态 × 事件 × 参数) 重叠均不允许
    # (运行时按声明顺序首命中, 重叠 = 后条被遮蔽 = 歧义)
    def _overlap(a, b):
        if a.event != b.event:
            return False
        src_ok = (a.src == "*" or b.src == "*" or a.src == b.src)
        par_ok = (a.param is None or b.param is None or a.param == b.param)
        return src_ok and par_ok

    dup = None
    for i in range(len(sm.transitions)):
        for j in range(i + 1, len(sm.transitions)):
            if _overlap(sm.transitions[i], sm.transitions[j]):
                dup = (f"#{i}({sm.transitions[i].src},"
                       f"{sm.transitions[i].event},{sm.transitions[i].param}) 与 "
                       f"#{j}({sm.transitions[j].src},"
                       f"{sm.transitions[j].event},{sm.transitions[j].param}) 重叠")
                break
        if dup:
            break
    _ck(v, "determinism", "PASS" if dup is None else "FAIL",
        f"{len(sm.transitions)} 条转移"
        + (f"; {dup}" if dup else "; 两两无重叠"))
    # 可达性: BFS 自初始态
    reach = {sm.initial}
    frontier = [sm.initial]
    while frontier:
        s = frontier.pop()
        for t in sm.transitions:
            if (t.src == s or t.src == "*") and t.dst not in reach:
                reach.add(t.dst)
                frontier.append(t.dst)
    unreach = [s for s in cs.states if s not in reach]
    _ck(v, "reachability", "PASS" if not unreach else "FAIL",
        f"自 {sm.initial} 可达 {len(reach)}/{len(cs.states)} 状态"
        + (f"; 不可达: {unreach}" if unreach else ""))
    # deny 动作 ID 位图域 (0..31, uint32 位图; 运行时 allows 同域, 防截断 fail-open)
    bad = [a for ids in sm.deny.values() for a in ids if not (0 <= a < 32)]
    _ck(v, "deny-domain", "PASS" if not bad else "FAIL",
        f"deny 动作 ID 全部在 0..31 (uint32 位图域): {bad or '是'}")
    # 运行时必需转移: abort 注入事件 (estop_release) 须有转移条目, 否则锁存静默失效
    if any(t.event == "estop_release" for t in sm.transitions):
        _ck(v, "abort-trans", "PASS",
            "abort 注入事件 estop_release 在转移表中有条目")
    elif "estop_release" in cs.events:
        _ck(v, "abort-trans", "FAIL",
            "abort 注入事件 estop_release 在转移表中无条目 (锁存将静默失效)")
    else:
        _ck(v, "abort-trans", "N-A", "事件表无 estop_release (无 abort 注入)")
    return v


def _verify_ltl(cs: ConstraintSet, entry: dict, k: int) -> ConstraintVerify:
    """LTL 安全属性有界模型检验 (BMC, 深度 k 由总入口按状态数推导):
    G(φ): ∃路径∃i≤k ¬φ → unsat 通过; F(ψ): ∃路径∃i≤k ψ → sat 通过;
    G(φ→ψ)/G(φ→X(ψ)): ∃路径∃i φ∧¬ψ → unsat 通过。
    深度必须覆盖最长最短路径 (直径), 否则 G 类性质产生假 PASS (不健全方向)。"""
    v = ConstraintVerify(entry["id"], entry["source"], "ltl")
    if cs.sm is None:
        _ck(v, "sm", "FAIL", "ltl 约束需要 state_machine 段")
        return v
    sm = cs.sm
    ast = entry["ast"]
    sidx = {s: i for i, s in enumerate(cs.states)}
    eidx = {e: i for i, e in enumerate(cs.events)}
    # 参数化事件: 参数值域 = 对应参数 ENUM (BMC 全枚举)
    ev_dom = {}
    for ce in sm.command_events:
        if ce.param_name:
            en = next((x for x in cs.enums if x.param == ce.param_name), None)
            if en is None:
                _ck(v, "param-domain", "FAIL",
                    f"事件 {ce.event} 参数 {ce.param_name} 缺 ENUM 定义域")
                return v
            ev_dom[ce.event] = sorted(set(en.values))
    deny_by_state = {}
    for st in cs.states:
        bm = 0
        for a in sm.deny.get(st, []):
            bm |= 1 << a
        deny_by_state[st] = bm
    s_vars = [z3.Int(f"s{i}") for i in range(k + 1)]
    e_vars = [z3.Int(f"e{i}") for i in range(k)]
    p_vars = [z3.Int(f"p{i}") for i in range(k)]

    def trans(s, e, p):
        """转移关系: 按 DSL 顺序的 ITE 链, 未定义事件自环(忽略)"""
        expr = s
        for t in reversed(sm.transitions):
            c = z3.BoolVal(True)
            if t.src != "*":
                c = z3.And(c, s == sidx[t.src])
            c = z3.And(c, e == eidx[t.event])
            if t.param is not None:
                c = z3.And(c, p == t.param)
            expr = z3.If(c, sidx[t.dst], expr)
        return expr

    def solve_path(extra):
        sol = z3.Solver()
        sol.add(s_vars[0] == sidx[sm.initial])
        for i in range(k):
            sol.add(e_vars[i] >= 0, e_vars[i] < len(cs.events))
            pc = p_vars[i] == 0xFFFF
            for ev, dom in ev_dom.items():
                dom_ok = z3.Or(*[p_vars[i] == d for d in dom])
                pc = z3.If(e_vars[i] == eidx[ev], dom_ok, pc)
            sol.add(pc)
            sol.add(s_vars[i + 1] == trans(s_vars[i], e_vars[i], p_vars[i]))
        for c in extra:
            sol.add(c)
        return sol

    def eval_prop(node, s, e):
        if isinstance(node, LtlAtom):
            if node.kind == "state":
                return s == sidx[node.name]
            if node.kind == "event":
                return e == eidx[node.name]
            ids = [a.action_id for a in cs.actions
                   if a.name == node.name or a.action_class == node.name]
            # allow = 每个匹配动作都不在当前状态的 deny 集合 (析取式, 无位运算)
            ok = z3.BoolVal(True)
            for a in ids:
                in_deny = z3.BoolVal(False)
                for st, alist in sm.deny.items():
                    if a in alist:
                        in_deny = z3.Or(in_deny, s == sidx[st])
                ok = z3.And(ok, z3.Not(in_deny))
            return ok
        if isinstance(node, LtlNot):
            return z3.Not(eval_prop(node.child, s, e))
        if isinstance(node, LtlBin):
            l = eval_prop(node.left, s, e)
            r = eval_prop(node.right, s, e)
            if node.op == "&":
                return z3.And(l, r)
            if node.op == "|":
                return z3.Or(l, r)
            return z3.Implies(l, r)
        raise DslError(f"命题层不支持节点: {type(node).__name__}")

    def cex_str(m, i):
        st = cs.states[m[s_vars[i]].as_long()]
        ev = cs.events[m[e_vars[min(i, k - 1)]].as_long()] if i < k else "-"
        return f"i={i} s={st} e={ev}"

    if ast.op == "F":
        for i in range(k + 1):
            sol = solve_path([eval_prop(ast.child, s_vars[i],
                                        e_vars[min(i, k - 1)])])
            if sol.check() == z3.sat:
                _ck(v, "bmc", "PASS",
                    f"F(ψ) 在 {i} 步内可达 (路径存在, z3 sat)")
                return v
        _ck(v, "bmc", "FAIL", f"F(ψ) {k} 步内不可达 (全路径证否)")
        return v

    inner = ast.child
    if isinstance(inner, LtlBin) and inner.op == "->":
        lhs, rhs = inner.left, inner.right
        x_next = isinstance(rhs, LtlTemporal) and rhs.op == "X"
        rhs_node = rhs.child if x_next else rhs
        # X 版: i ∈ [0,k-1] (ψ 在 s_{i+1}, 覆盖 s_k); 非 X 版: i ∈ [0,k]
        n = k if x_next else k + 1
        for i in range(n):
            ti = i + 1 if x_next else i
            sol = solve_path([eval_prop(lhs, s_vars[i], e_vars[i]),
                              z3.Not(eval_prop(rhs_node, s_vars[ti],
                                               e_vars[min(ti, k - 1)]))])
            if sol.check() == z3.sat:
                _ck(v, "bmc", "FAIL",
                    f"G(φ→{'X' if x_next else ''}ψ) 反例: {cex_str(sol.model(), i)}")
                return v
        _ck(v, "bmc", "PASS",
            f"G(φ→{'X' if x_next else ''}ψ) {n} 步证否 unsat, 无反例")
        return v

    # G(phi): 检查 i ∈ [0, k] (含第 k 步, 防止深违例假通过)
    for i in range(k + 1):
        sol = solve_path([z3.Not(eval_prop(inner, s_vars[i],
                                           e_vars[min(i, k - 1)]))])
        if sol.check() == z3.sat:
            _ck(v, "bmc", "FAIL", f"G(φ) 反例 (违例状态): {cex_str(sol.model(), i)}")
            return v
    _ck(v, "bmc", "PASS", f"G(φ) {k}+1 步证否 unsat, 无违例状态")
    return v


# ==================== 总入口 ====================

def verify_constraint_set(cs: ConstraintSet) -> List[ConstraintVerify]:
    results: List[ConstraintVerify] = []
    for r in cs.ranges:
        results.append(_verify_range(cs, r))
    for e in cs.enums:
        results.append(_verify_enum(cs, e))
    for c in cs.combines:
        results.append(_verify_combine(cs, c))
    for w in cs.whens:
        results.append(_verify_when(cs, w))
    results.append(_verify_state_machine(cs))
    # BMC 深度 = 2|S|+2: 最长最短路径 ≤ |S|-1, 留一倍余量 (G 类假 PASS 防线)
    k = 2 * len(cs.states) + 2
    for l in cs.ltls:
        results.append(_verify_ltl(cs, l, k))
    return results
