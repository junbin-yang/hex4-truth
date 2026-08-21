# -*- coding: utf-8 -*-
"""
smt_report.py — 验证报告与覆盖率报告生成

设计文档: docs/ESP32-S3安全监控器设计文档.md §6.2/§6.5
输出:
  docs/reports/smt_verify_report.md          逐条约束四项验证记录
  docs/reports/constraint_coverage_report.md 条款→约束映射 + 覆盖率统计
覆盖率口径: 覆盖率 = 已形式化条款数 / 适用条款总数 (适用条款集来自 yaml coverage 段)
"""

import os

VERIFY_REPORT = "smt_verify_report.md"
COVERAGE_REPORT = "constraint_coverage_report.md"


def _md_table(headers, rows):
    out = ["| " + " | ".join(headers) + " |",
           "|" + "|".join("---" for _ in headers) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(c) for c in r) + " |")
    return "\n".join(out)


def write_verify_report(cs, verifies, out_dir: str):
    os.makedirs(out_dir, exist_ok=True)
    rows = []
    for v in verifies:
        status = "PASS" if v.ok else "**FAIL**"
        for k, chk in enumerate(v.checks):
            rows.append([
                v.id if k == 0 else "", v.source if k == 0 else "",
                v.shape if k == 0 else "", chk.name, chk.status, chk.detail,
            ])
    body = _md_table(["约束 ID", "条款来源", "形状", "验证项",
                      "结果", "说明"], rows)

    n_fail = sum(1 for v in verifies if not v.ok)
    text = f"""# SMT 验证报告

> 约束包: {cs.package}（{cs.title}）
> 工具: tools/smt_compile.py（z3 {_z3_version()}）
> 口径: 等价性验证域 = 定点离散域; range/enum/when 与 DSL 直通记为 N-A;
> combine2 降维边界由 z3 Optimize 求解, 逐档 ForAll 证否 unsat。

**结论: {len(verifies)} 条约束, 全部通过 = {len(verifies) - n_fail} 条, 失败 = {n_fail} 条**

{body}

## 验证项说明

| 验证项 | 内容 |
|---|---|
| sat | 约束本体可满足（不自相矛盾/定义域非空） |
| consistency | 同参数多约束交集非空（条款冲突检出） |
| equiv | 编译等价性: 表判定 ↔ DSL 约束（离散域; 直通形状记 N-A） |
| bucket-complete | combine2 分档 ENUM 齐备且与 bucket_domain 一致 |
| bound-solve | z3 Optimize 逐档边界求解（取整方向保守正确） |
"""
    path = os.path.join(out_dir, VERIFY_REPORT)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return path


def _clause_hit(clause_id: str, source: str) -> bool:
    """条款号匹配: 标准前缀与条款号须相邻出现在 source (如 "10218-1 §5.5.5"
    不匹配仅含 "TS 15066 §5.5.5" 的 source; "§5.5.5" 不误匹配 "§5.5.5.1")"""
    import re
    m = re.search(r"^(.*?)\s*(§\d+(?:\.\d+)*)", clause_id)
    std, tok = (m.group(1), m.group(2)) if m else ("", clause_id)
    if std and tok:
        # 标准前缀数字标识 ("10218-1" / "TS 15066"→"15066") 与条款号相邻
        stdtok = std.replace("TS", "").strip()
        return (re.search(re.escape(stdtok) + r"\s+" + re.escape(tok)
                          + r"(?![0-9.])", source) is not None)
    if tok:
        return re.search(re.escape(tok) + r"(?![0-9.])", source) is not None
    return clause_id in source


def write_coverage_report(cs, verifies, out_dir: str):
    os.makedirs(out_dir, exist_ok=True)
    cov = cs.coverage
    standard = cov.get("standard", cs.package)
    # N1.4: applicable_clauses 支持 {id, topic} 对象或纯字符串
    applicable = []
    for c in cov.get("applicable_clauses", []):
        if isinstance(c, dict):
            applicable.append((str(c.get("id", "")), str(c.get("topic", ""))))
        else:
            applicable.append((str(c), ""))

    # 条款→约束映射（source 文本含独立条款号即计为已形式化）
    covered = {}
    for v in verifies:
        if v.ok:
            for clause_id, _topic in applicable:
                if _clause_hit(clause_id, v.source):
                    covered.setdefault(clause_id, []).append(v.id)
    n = len(covered)
    rate = (n / len(applicable)) * 100 if applicable else 0.0

    rows = []
    for clause_id, topic in applicable:
        cids = covered.get(clause_id, [])
        rows.append([clause_id, topic, ", ".join(cids) if cids else "**未覆盖**"])
    body = _md_table(["适用条款", "主题", "形式化约束 ID"], rows)

    excl_rows = []
    for e in cov.get("exclusions", []):
        if isinstance(e, dict):
            excl_rows.append([e.get("id", ""), e.get("topic", ""),
                              e.get("reason", "")])
        else:
            excl_rows.append([str(e), "", ""])
    excl_body = (_md_table(["排除条款", "主题", "原因"], excl_rows)
                 if excl_rows else "（无）")

    text = f"""# 物理约束形式化覆盖率报告

> 约束包: {cs.package}（{cs.title}）
> 标准: {standard}
> 口径: 覆盖率 = 已形式化条款数 / 适用条款总数（适用条款集为人工筛选的
> 场景范围内"可表为数值/逻辑约束的规范性要求条款", 见约束源 yaml 的
> coverage 段; 完整条款矩阵与范围界定见 docs/iso_clause_matrix.md）

**覆盖率: {n} / {len(applicable)} = {rate:.1f}%**（目标 ≥ 90%）

{body}

## 排除与未覆盖条款

{excl_body}

## 说明

- 条款号匹配为独立 token（"§5.5" 不误匹配 "§5.5.5"）;
- 条款内容基于标准公开结构整理, 正式申报前需对照标准原文逐条核定
  （见条款矩阵文档的置信度标注）。
"""
    path = os.path.join(out_dir, COVERAGE_REPORT)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return path


def _z3_version():
    import z3
    return z3.get_version_string()
