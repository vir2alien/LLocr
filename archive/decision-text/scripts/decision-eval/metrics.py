"""Aggregation, metrics and the Markdown report."""

import csv
import statistics
from collections import defaultdict

YES_CLASSES = {"match", "notation"}
NO_CLASSES = {"mismatch", "sanity_blank", "sanity_cross", "real_ismatch"}


def pct(values, q):
    if not values:
        return None
    s = sorted(values)
    idx = min(len(s) - 1, max(0, round(q * (len(s) - 1))))
    return s[idx]


def summarize(values):
    if not values:
        return {"n": 0}
    return {
        "n": len(values),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "p10": pct(values, 0.10),
        "p90": pct(values, 0.90),
        "min": min(values),
        "max": max(values),
    }


def auc(pos, neg):
    pos = [v for v in pos if v is not None]
    neg = [v for v in neg if v is not None]
    if not pos or not neg:
        return None
    ranked = sorted([(v, 1) for v in pos] + [(v, 0) for v in neg], key=lambda t: t[0])
    i = 0
    rank_sums = {1: 0.0, 0: 0.0}
    while i < len(ranked):
        j = i
        while j < len(ranked) and ranked[j][0] == ranked[i][0]:
            j += 1
        avg_rank = (i + 1 + j) / 2.0
        for k in range(i, j):
            rank_sums[ranked[k][1]] += avg_rank
        i = j
    u = rank_sums[1] - len(pos) * (len(pos) + 1) / 2.0
    return u / (len(pos) * len(neg))


def best_threshold(pos, neg):
    pos = sorted(v for v in pos if v is not None)
    neg = sorted(v for v in neg if v is not None)
    if not pos or not neg:
        return None
    cands = {0.0, 1.0}
    for v in pos + neg:
        cands.add(max(0.0, v - 1e-9))
        cands.add(min(1.0, v + 1e-9))
    best = (None, -1.0)
    for t in sorted(cands):
        acc = sum(1 for v in pos if v >= t) + sum(1 for v in neg if v < t)
        acc /= len(pos) + len(neg)
        if acc > best[1]:
            best = (t, acc)
    return best


def rates_at(pos, neg, t):
    pos = [v for v in pos if v is not None]
    neg = [v for v in neg if v is not None]
    if not pos or not neg:
        return None
    return {
        "missed_error": sum(1 for v in neg if v >= t) / len(neg),
        "false_alarm": sum(1 for v in pos if v < t) / len(pos),
    }


def group(rows, key_fn):
    out = defaultdict(list)
    for r in rows:
        out[key_fn(r)].append(r)
    return out


def variant_metrics(rows, variant, policy="as_is"):
    vr = [r for r in rows if r["variant"] == variant and r["policy"] == policy and not r["error"]]
    by_cls = group(vr, lambda r: r["cls"])
    p = lambda cls: [r["p"] for r in by_cls.get(cls, []) if r["p"] is not None]

    match_p = p("match")
    mismatch_rows = [r for r in by_cls.get("mismatch", [])]
    mismatch_p = [r["p"] for r in mismatch_rows if r["p"] is not None]
    subtle_p = [r["p"] for r in mismatch_rows if r["severity"] == "subtle" and r["p"] is not None]

    m = {
        "variant": variant,
        "n": len(vr),
        "failures": sum(1 for r in rows if r["variant"] == variant and r["policy"] == policy and r["error"]),
        "match": summarize(match_p),
        "mismatch": summarize(mismatch_p),
        "subtle": summarize(subtle_p),
        "notation": summarize(p("notation")),
        "sanity_blank": summarize(p("sanity_blank")),
        "sanity_cross": summarize(p("sanity_cross")),
        "auc": auc(match_p, mismatch_p),
        "auc_subtle": auc(match_p, subtle_p),
    }
    if match_p and mismatch_p:
        m["gap"] = statistics.fmean(match_p) - statistics.fmean(mismatch_p)
        bt = best_threshold(match_p, mismatch_p)
        m["best_thr"], m["best_acc"] = bt
        for t in (0.4, 0.5, 0.6):
            r = rates_at(match_p, mismatch_p, t)
            m[f"missed@{t}"] = r["missed_error"]
            m[f"alarm@{t}"] = r["false_alarm"]
        if by_cls.get("notation"):
            m["notation_fn@best"] = sum(1 for v in p("notation") if v < m["best_thr"]) / max(1, len(p("notation")))
        if p("sanity_blank") or p("sanity_cross"):
            san = p("sanity_blank") + p("sanity_cross")
            m["sanity_pass"] = sum(1 for v in san if v < 0.2) / len(san)
    toks = [r["input_tokens"] for r in vr if r["input_tokens"]]
    lat = [r["latency_s"] for r in vr if r["latency_s"] is not None]
    m["tokens_max"] = max(toks) if toks else None
    m["tokens_p90"] = pct(toks, 0.9) if toks else None
    m["latency_mean"] = statistics.fmean(lat) if lat else None
    return m


def _fmt(v, digits=3):
    if v is None:
        return "—"
    if isinstance(v, float):
        return f"{v:.{digits}f}"
    return str(v)


def class_table(by_cls_summary):
    lines = ["| класс | n | mean | median | p10 | p90 |", "|---|---|---|---|---|---|"]
    for cls in ("match", "notation", "mismatch", "subtle", "sanity_blank", "sanity_cross", "real_ismatch"):
        s = by_cls_summary.get(cls)
        if not s or s["n"] == 0:
            continue
        lines.append(f"| {cls} | {s['n']} | {_fmt(s['mean'])} | {_fmt(s['median'])} | "
                     f"{_fmt(s['p10'])} | {_fmt(s['p90'])} |")
    return "\n".join(lines)


def render_report(rows, stages_meta, out_path):
    variants = sorted({r["variant"] for r in rows})
    all_metrics = {v: variant_metrics(rows, v) for v in variants}

    rank = sorted(variants, key=lambda v: (all_metrics[v]["auc"] is None,
                                            -(all_metrics[v]["auc"] or 0)))
    lines = ["# Decision model — оценка разделения «да»/«нет»", ""]
    for k, v in stages_meta.items():
        lines.append(f"- {k}: {v}")
    lines += ["", "## Рейтинг вариантов (AUC: match vs mismatch)", "",
              "| вариант | запр. | AUC | AUC subtle | разрыв средних | лучший порог | acc@порог | "
              "пропуск ошибок@0.5 | ложн. тревога@0.5 | нотация FN | sanity<0.2 | отказы |",
              "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for v in rank:
        m = all_metrics[v]
        lines.append(
            f"| `{v}` | {m['n']} | {_fmt(m['auc'])} | {_fmt(m['auc_subtle'])} | {_fmt(m.get('gap'))} | "
            f"{_fmt(m.get('best_thr'), 2)} | {_fmt(m.get('best_acc'))} | "
            f"{_fmt(m.get('missed@0.5'), 1)} | {_fmt(m.get('alarm@0.5'), 1)} | "
            f"{_fmt(m.get('notation_fn@best'), 2)} | {_fmt(m.get('sanity_pass'), 2)} | {m['failures']} |")
    lines.append("")
    lines.append("Пороги 0.5/0.6 и крайние случаи: `missed@T` — доля испорченных блоков, "
                 "принятых как верные (p ≥ T); `alarm@T` — доля верных блоков, отвергнутых (p < T).")
    for v in rank:
        m = all_metrics[v]
        if m["n"] == 0:
            continue
        lines += ["", f"## `{v}`", "",
                  class_table({k: m.get(k) for k in
                               ("match", "notation", "mismatch", "subtle",
                                "sanity_blank", "sanity_cross", "real_ismatch")}),
                  "", f"input_tokens: p90 = {_fmt(m['tokens_p90'], 0)}, max = {_fmt(m['tokens_max'], 0)}; "
                      f"latency: mean = {_fmt(m['latency_mean'], 2)} s"]

        by_label = group([r for r in rows if r["variant"] == v and not r["error"]], lambda r: r["label"])
        lbl_rows = []
        for lbl, rs in sorted(by_label.items()):
            yp = [r["p"] for r in rs if r["cls"] == "match" and r["p"] is not None]
            np_ = [r["p"] for r in rs if r["cls"] == "mismatch" and r["p"] is not None]
            if yp and np_:
                lbl_rows.append((lbl, len(yp), statistics.fmean(yp), statistics.fmean(np_)))
        if lbl_rows:
            lines += ["", "| тип блока | n(match) | mean match | mean mismatch |", "|---|---|---|---|"]
            for lbl, n, ymean, nmean in lbl_rows:
                lines.append(f"| {lbl} | {n} | {ymean:.3f} | {nmean:.3f} |")

    bad = [r for r in rows if not r["error"] and r["p"] is not None and
           ((r["cls"] in NO_CLASSES and r["p"] >= 0.6) or (r["cls"] in YES_CLASSES and r["p"] < 0.4))]
    bad.sort(key=lambda r: abs(0.5 - r["p"]), reverse=True)
    if bad:
        lines += ["", "## Худшие кейсы (уверенно неверные вердикты)", ""]
        for r in bad[:15]:
            excerpt = (r["text"] or "").replace("\n", " ")[:70]
            lines.append(f"- `{r['variant']}` {r['cls']}/{r['severity'] or '-'} p={r['p']:.2f} "
                         f"{r['label']} стр.{r['page']} [{r['op'] or 'original'}] «{excerpt}»")

    res_rows = [r for r in rows if r["policy"] != "as_is"]
    if res_rows:
        lines += ["", "## Ось разрешения изображений", ""]
        for pol in sorted({r["policy"] for r in res_rows}):
            for v in sorted({r["variant"] for r in res_rows}):
                m = variant_metrics(rows, v, pol)
                if m["n"]:
                    lines.append(f"- `{v}` / {pol}: n={m['n']}, AUC={_fmt(m['auc'])}, "
                                 f"match={_fmt(m['match'].get('mean'))}, mismatch={_fmt(m['mismatch'].get('mean'))}")

    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return all_metrics


def write_csv(rows, out_path):
    fields = ["variant", "policy", "case_id", "uid", "page", "label", "cls", "severity",
              "op", "expected", "p", "input_tokens", "latency_s", "error", "text"]
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            row = dict(r)
            if isinstance(row.get("text"), (dict, list)):
                row["text"] = str(row["text"])
            row["text"] = (row.get("text") or "")[:400]
            w.writerow(row)
