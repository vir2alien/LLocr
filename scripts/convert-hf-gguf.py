#!/usr/bin/env python3
"""Convert a Hugging Face checkpoint to GGUF quants and print the profile fragment.

The maintainer-side half of "a model with no curated GGUFs" (HunyuanOCR-1.5
was the first): download the checkpoint, run llama.cpp's convert_hf_to_gguf.py
inside a venv, quantize with llama-quantize built from the same checkout, then
hash the artifacts — the sha256/sizeBytes a model profile carries. Upload the
outputs to a repository under your control; the profile pins it by revision
and hashes, and from there the app's own install pipeline takes over.

Usage:
  scripts/convert-hf-gguf.py tencent/HunyuanOCR --exclude "v1.0/*" --mmproj
  scripts/convert-hf-gguf.py tencent/HunyuanOCR --draft dflash --mmproj
  scripts/convert-hf-gguf.py ORG/REPO --revision <sha> --quants q8_0,q4_k_m

or:
  scripts/convert-hf-gguf.py tencent/HunyuanOCR --exclude "v1.0/*" --mmproj --llamacpp-ref b11572


Everything is cached under --work; reruns skip the finished steps.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"command failed ({result.returncode}): {' '.join(str(c) for c in cmd)}")


def venv_python(work):
    venv = work / "venv"
    py = venv / ("Scripts" if os.name == "nt" else "bin") / ("python.exe" if os.name == "nt" else "python")
    if (venv / ".deps-installed").exists():
        return py
    run([sys.executable, "-m", "venv", str(venv)])
    run([str(py), "-m", "pip", "install", "-U", "pip"])
    # torch is only needed by convert_hf_to_gguf.py; it is not a project dependency.
    run([str(py), "-m", "pip", "install", "-U", "torch", "transformers", "huggingface_hub"])
    (venv / ".deps-installed").touch()
    return py


def download(py, repo, revision, excludes, model_dir):
    if (model_dir / ".downloaded").exists():
        return
    code = (
        "from huggingface_hub import snapshot_download; "
        f"snapshot_download(repo_id={repo!r}, "
        f"revision={revision!r} or None, "
        f"local_dir={str(model_dir)!r}, ignore_patterns={excludes!r})"
    )
    run([str(py), "-c", code])
    (model_dir / ".downloaded").touch()


def ensure_llamacpp(work, ref, override):
    if override:
        return Path(override)
    tree = work / "llama.cpp"
    if not tree.exists():
        cmd = ["git", "clone", "--depth", "1"]
        if ref != "master":
            cmd += ["--branch", ref]
        run(cmd + ["https://github.com/ggml-org/llama.cpp", str(tree)])
    return tree


def ensure_quantize(llamacpp):
    quantize = llamacpp / "build" / "bin" / "llama-quantize"
    if not quantize.exists():
        run(["cmake", "-S", str(llamacpp), "-B", str(llamacpp / "build")])
        run(["cmake", "--build", str(llamacpp / "build"), "--target", "llama-quantize",
             "-j", str(os.cpu_count() or 4)])
    return quantize


def convert(py, llamacpp, model_dir, out, name, with_mmproj, draft_dir):
    script = llamacpp / "convert_hf_to_gguf.py"
    made = {}
    model_f16 = out / f"{name}-f16.gguf"
    run([str(py), str(script), "--outfile", str(model_f16), "--outtype", "f16", str(model_dir)])
    made["model_f16"] = model_f16
    if with_mmproj:
        mmproj = out / f"mmproj-{name}-f16.gguf"
        run([str(py), str(script), "--outfile", str(mmproj), "--outtype", "f16", "--mmproj", str(model_dir)])
        made["mmproj_f16"] = mmproj
    if draft_dir:
        draft = out / f"{name}-draft-bf16.gguf"
        run([str(py), str(script), "--outfile", str(draft), "--outtype", "bf16",
             "--target-model-dir", str(model_dir), str(model_dir / draft_dir)])
        made["draft"] = draft
    return made


def quantize(quantize_bin, model_f16, quants, out, name):
    files = []
    for quant in quants:
        target = out / f"{name}-{quant.upper()}.gguf"
        if target.exists():
            target.unlink()
        run([str(quantize_bin), str(model_f16), str(target), quant.upper()])
        files.append((quant, target))
    return files


def digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fragment(quants, mmproj):
    doc = {"quants": [{"id": q, "file": p.name, "sha256": digest(p), "sizeBytes": p.stat().st_size}
                      for q, p in quants]}
    if mmproj is not None:
        doc["mmproj"] = {"id": "f16", "file": mmproj.name, "sha256": digest(mmproj),
                         "_note": "Mandatory: without the projector the model sees no image."}
    return json.dumps(doc, indent=2, ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("repo", help="Hugging Face repo id, e.g. tencent/HunyuanOCR")
    parser.add_argument("--revision", default="", help="pin the checkpoint revision")
    parser.add_argument("--exclude", action="append", default=[],
                        help="checkpoint path pattern to skip, repeatable (e.g. \"v1.0/*\")")
    parser.add_argument("--mmproj", action="store_true", help="also convert the vision projector (f16)")
    parser.add_argument("--draft", help="checkpoint subfolder of a speculative-decoding draft (converted to bf16)")
    parser.add_argument("--quants", default="q8_0,q4_k_m", help="comma-separated quant ids for llama-quantize")
    parser.add_argument("--name", help="base name of the output files (default: last repo segment)")
    parser.add_argument("--work", default=".hf-convert", help="cache directory (default: ./.hf-convert)")
    parser.add_argument("--out", help="output directory (default: WORK/out)")
    parser.add_argument("--llamacpp-ref", default="master",
                        help="llama.cpp tag to clone for the converter (pin it for a reproducible conversion)")
    parser.add_argument("--llamacpp-dir", help="reuse this llama.cpp checkout instead of cloning")
    args = parser.parse_args()

    work = Path(args.work).resolve()
    out = Path(args.out).resolve() if args.out else work / "out"
    out.mkdir(parents=True, exist_ok=True)
    model_dir = work / "model"
    name = args.name or args.repo.rsplit("/", 1)[-1]

    py = venv_python(work)
    download(py, args.repo, args.revision, args.exclude, model_dir)
    llamacpp = ensure_llamacpp(work, args.llamacpp_ref, args.llamacpp_dir)
    made = convert(py, llamacpp, model_dir, out, name, args.mmproj, args.draft)
    quants = quantize(ensure_quantize(llamacpp), made["model_f16"], [q for q in args.quants.split(",") if q], out, name)

    print("\nProduced files:")
    extras = [path for key, path in made.items() if key != "model_f16"]
    for path in [path for _, path in quants] + [made["model_f16"]] + extras:
        print(f"  {path}  ({path.stat().st_size:,} bytes)")
    print("\nProfile fragment (merge into resources/profiles/models/<id>.json → files):")
    print(fragment(quants, made.get("mmproj_f16")))


if __name__ == "__main__":
    main()
