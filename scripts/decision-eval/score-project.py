"""Score a .llocr project's blocks with a decision model, the way the app does.

The request is the app's own (the v0 shape of variants.py: the state with the
block type, the candidate text and the notation; one noul `match` question),
the crops are the project's own pages rendered at the app's DPI, and the
verdicts are written back into a .llocr project — the same container the app
writes, so the test stand (OCRtest/decision_models/test 2/evaluate.py) scores
the result like any app run.

The model answers the probability of a match; the profile's thresholds turn it
into a verdict, and the probability itself is stored as the block's confidence
(ADR 173) so a later threshold sweep needs no new run.

Usage:
  python3 scripts/decision-eval/score-project.py \
      --corpus "OCRtest/decision_models/test 2/OCR_TEST_MinerU2.5-Pro.llocr" \
      --model d3-mini --config configs/clef-flash.json \
      --scope-from "OCRtest/decision_models/test 2/iteration6/OCR_TEST_D3-mini.llocr" \
      --out "OCRtest/decision_models/test 2/iteration7/OCR_TEST_clef-flash.llocr"
"""

import argparse
import json
import shutil
import sys
import time
import zipfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

import corpus  # noqa: E402
import run as runner  # noqa: E402
import variants as V  # noqa: E402
from client import SystemOneClient, SystemOneError  # noqa: E402


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--corpus", required=True, help="the project to score (the OCR result)")
    ap.add_argument("--model", required=True, help="the model profile id (its thresholds and its request texts)")
    ap.add_argument("--config", required=True, help="the server config (scripts/decision-eval/configs)")
    ap.add_argument("--out", required=True, help="the project to write (the verdicts)")
    ap.add_argument("--scope-from", default=None,
                    help="a project whose verified blocks define the scope (the rest stay NotChecked)")
    ap.add_argument("--work", default=None, help="work dir (default: <corpus dir>/work-score)")
    ap.add_argument("--base-url", default=None, help="reuse a running server, skip the launch")
    ap.add_argument("--timeout", type=float, default=300.0, help="per-request timeout, s")
    return ap.parse_args()


def noul_probability(answer):
    """The app's readNoul: a bare number, or an object with noul/probability."""
    if isinstance(answer, (int, float)):
        return float(answer)
    if isinstance(answer, dict):
        for key in ("noul", "probability"):
            if isinstance(answer.get(key), (int, float)):
                return float(answer[key])
    return None


def thresholds_of(model_profile):
    """(per-block thresholds, the role's fallback) of the model's decision role."""
    role = model_profile.get("roles", {}).get("decision", {})
    by_block = role.get("matchThresholdByBlock", {})
    base = role.get("matchThreshold")
    aliases = role.get("blockAliases", {})
    return by_block, base, aliases


def main():
    args = parse_args()
    corpus_path = Path(args.corpus)
    out_path = Path(args.out)
    work = Path(args.work) if args.work else corpus_path.parent / "work-score"
    work.mkdir(parents=True, exist_ok=True)

    model_profile_path = REPO_ROOT / "resources" / "profiles" / "models" / f"{args.model}.json"
    model_profile = json.loads(model_profile_path.read_text(encoding="utf-8"))
    request = V.load_app_profile(model_profile_path)
    by_block, base, aliases = thresholds_of(model_profile)

    project = corpus.load_project(corpus_path, work / "extract")
    blocks = corpus.build_blocks(project, work / "extract", work / "pages")
    print(f"corpus: {corpus_path}  blocks: {len(blocks)}")

    scope = None
    if args.scope_from:
        scope = set()
        scope_project = corpus.load_project(Path(args.scope_from), work / "scope")
        for pi, page in enumerate(scope_project["pages"]):
            for bi, box in enumerate(page.get("boxes", [])):
                if box.get("checkStatus", 0) != 0:
                    scope.add((pi, bi))
        print(f"scope: {len(scope)} blocks (from {args.scope_from})")

    app_request = next(v for v in V.make_variants(request) if v.vid == "v0_app_current")

    base_url = args.base_url
    server = None
    if not base_url:
        cfg = runner.load_config(SCRIPT_DIR / args.config)
        runner.guard_no_conflicting_processes()
        server = runner.Server(cfg, work, runner.find_free_port())
        base_url = server.start()
    client = SystemOneClient(base_url, timeout_s=args.timeout)

    scores = {}
    started = time.time()
    try:
        for i, block in enumerate(blocks):
            if scope is not None and (block.page, block.index) not in scope:
                continue
            payload = app_request.build(block)
            try:
                raw = client.ask_raw(payload["state"], [block.crop], payload["questions"])
            except SystemOneError as exc:
                print(f"  block p{block.page}b{block.index} ({block.label}): {exc}")
                continue
            answers = raw.get("answers", {})
            p = noul_probability(answers.get("match"))
            if p is None:
                print(f"  block p{block.page}b{block.index}: no match answer: {json.dumps(raw)[:200]}")
                continue
            scores[(block.page, block.index)] = p
            if (i + 1) % 20 == 0:
                print(f"  {i + 1}/{len(blocks)} blocks, {time.time() - started:.0f}s")
    finally:
        if server is not None:
            server.stop()

    # The verdicts: the profile's thresholds over the stored probability.
    out_project = json.loads(json.dumps(project))
    for pi, page in enumerate(out_project["pages"]):
        for bi, box in enumerate(page.get("boxes", [])):
            p = scores.get((pi, bi))
            if p is None:
                box["checkStatus"] = 0
                box["confidence"] = -1.0
                continue
            label = aliases.get(box.get("label", ""), box.get("label", ""))
            t = by_block.get(label, base)
            box["confidence"] = p
            box["checkStatus"] = 1 if p >= t else 4

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(corpus_path) as src:
        with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
            for name in src.namelist():
                if name == "project.json":
                    continue
                z.writestr(name, src.read(name))
            z.writestr("project.json", json.dumps(out_project, ensure_ascii=False))
    print(f"scored {len(scores)} blocks -> {out_path}")


if __name__ == "__main__":
    main()
