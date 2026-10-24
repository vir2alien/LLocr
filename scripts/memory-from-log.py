#!/usr/bin/env python3
"""Read a llama.cpp server log and print the memory numbers of a model profile.

Usage:
    memory-from-log.py [--app-patches N] LOG [LOG ...]

One log prints the measurements. Two logs of the same model (the first with
flash attention on, the second off) also print the extrapolated vision buffer
for a page of --app-patches patches -- the case of a projector that keeps an
attention matrix over the whole page.

The fields it prints are the fields of the "memory" block of a model profile
(resources/profiles/models/<id>.json); ADR 191 and docs/27-memory-estimate.md
explain what they mean.
"""

import argparse
import os
import re
import sys

MiB = 1024 * 1024


def first(pattern, text, cast=str, default=None):
    match = re.search(pattern, text)
    if not match:
        return default
    return cast(match.group(1))


def unique(values):
    seen = []
    for value in values:
        if value not in seen:
            seen.append(value)
    return seen


class Log:
    def __init__(self, path):
        self.path = path
        with open(path, errors="replace") as handle:
            self.text = handle.read()
        self.draft_at = self.text.find("loading draft model")
        if self.draft_at < 0:
            self.draft_at = len(self.text)

    def before_draft(self):
        return self.text[: self.draft_at]

    def after_draft(self):
        return self.text[self.draft_at :]

    def caches(self, text):
        found = []
        for size, cells, layers in re.findall(
            r"llama_kv_cache: size =\s*([\d.]+) MiB \(\s*(\d+) cells,\s*(\d+) layers", text
        ):
            found.append((float(size), int(cells), int(layers)))
        return unique(found)

    def compute_buffers(self, text):
        found = [
            float(m)
            for m in re.findall(
                r"sched_reserve:\s+CPU compute buffer size =\s*([\d.]+) MiB", text
            )
        ]
        return unique(found)

    def vision_buffers(self):
        found = [
            float(m)
            for m in re.findall(
                r"reserve_compute_meta:\s+CPU compute buffer size =\s*([\d.]+) MiB", self.text
            )
        ]
        return unique(found)

    def image_tokens(self):
        width = first(r"warmup with image size = (\d+) x", self.text, int)
        height = first(r"warmup with image size = \d+ x (\d+)", self.text, int)
        patch = first(r"load_hparams: patch_size:\s+(\d+)", self.text, int)
        merge = first(r"load_hparams: n_merge:\s+(\d+)", self.text, int)
        if None in (width, height, patch, merge) or patch <= 0:
            return None
        patches = (width // patch) * (height // patch)
        tokens = patches // (merge * merge)
        return {
            "width": width,
            "height": height,
            "patch": patch,
            "merge": merge,
            "patches": patches,
            "tokens": tokens,
        }

    def draft_file(self):
        match = re.search(r"loading draft model '([^']+)'", self.text)
        return match.group(1) if match else None


def architecture(text):
    heads = first(r"print_info: n_head\s+= (\d+)", text, int)
    total_k = first(r"print_info: n_embd_head_k\s+= (\d+)", text, int)
    kv_list = first(r"print_info: n_head_kv\s+= \[([^\]]*)\]", text)
    if kv_list is None:
        kv_heads = first(r"print_info: n_head_kv\s+= (\d+)", text, int)
        kv_layers = None
    else:
        values = [int(v) for v in kv_list.split(",") if v.strip()]
        kv_heads = max(values) if values else None
        kv_layers = None
    return {
        "n_layer": first(r"print_info: n_layer\s+= (\d+)", text, int),
        "n_embd": first(r"print_info: n_embd\s+= (\d+)", text, int),
        "n_head": heads,
        "n_head_kv": kv_heads,
        "n_kv_layers": kv_layers,
        "n_embd_head_k": total_k,
        "n_swa": first(r"print_info: n_swa\s+= (\d+)", text, int),
    }


def report(log):
    text = log.before_draft()
    ctx = first(r"llama_context: n_ctx\s+= (\d+)", text, int)
    ubatch = first(r"llama_context: n_ubatch\s+= (\d+)", text, int)
    batch = first(r"llama_context: n_batch\s+= (\d+)", text, int)
    flash = first(r"llama_context: flash_attn\s+= (\w+)", text)

    print(f"== {log.path}")
    print("-- context")
    print(f"   n_ctx {ctx}   n_ubatch {ubatch}   n_batch {batch}   flash_attn {flash}")

    arch = architecture(text)
    print("-- architecture")
    print("   " + "   ".join(f"{k} {v}" for k, v in arch.items()))

    caches = log.caches(text)
    print("-- kv caches (main context)")
    scaling = None
    fixed = 0
    head_dim = None
    kv_layers = None
    kv_heads = arch["n_head_kv"]
    for size, cells, layers in caches:
        if ctx and cells == ctx:
            scaling = size * MiB / cells
            kv_layers = layers
            print(
                f"   {size:9.2f} MiB  {cells:6d} cells  {layers:3d} layers"
                f"   -> {scaling:.0f} B/token (follows the context)"
            )
            if kv_heads and cells and layers:
                head_dim = size * MiB / (cells * layers * 2 * kv_heads * 2)
        else:
            fixed += int(size * MiB)
            print(
                f"   {size:9.2f} MiB  {cells:6d} cells  {layers:3d} layers"
                f"   -> fixed (the sliding window's cells)"
            )
    if head_dim:
        print(f"   head_dim from the cache {head_dim:.0f}   kv layers {kv_layers}")

    buffers = log.compute_buffers(text)
    print("-- working set (compute buffer)")
    for size in buffers:
        print(f"   main      {size:9.2f} MiB")
    if ctx and ubatch and buffers:
        mask = 2
        per_token = (buffers[0] * MiB - ctx * ubatch * mask) / ubatch
        print(
            f"   workingSetBytesPerToken (mask {mask} B) {per_token:.0f}"
            f"   workingSetContextBytes {mask}"
        )

    print("-- vision (clip)")
    geometry = log.image_tokens()
    if geometry:
        print(
            f"   warmup image {geometry['width']}x{geometry['height']}"
            f"   patch {geometry['patch']}   merge {geometry['merge']}"
            f"   -> {geometry['patches']} patches = {geometry['tokens']} tokens"
        )
    vision = log.vision_buffers()
    for index, size in enumerate(vision):
        who = "vision" if index == 0 else "second tower"
        rate = ""
        if geometry and geometry["tokens"]:
            rate = f"-> {size * MiB / geometry['tokens']:.0f} B/token"
        print(f"   {who:12s} {size:9.2f} MiB   {rate}")

    mtmd = first(
        r"estimated worst-case memory usage of mmproj is ([\d.]+) MiB", log.text, float
    )
    if mtmd:
        print(f"-- projector: mtmd worst case {mtmd:.2f} MiB")

    print("-- profile fields")
    if scaling:
        print(f"   kvBytesPerToken {scaling:.0f}")
    if fixed:
        print(f"   kvBytesFixed {fixed}")
    draft_text = log.after_draft()
    draft_caches = log.caches(draft_text)
    draft_buffers = log.compute_buffers(draft_text)
    if draft_caches:
        draft_scaling = None
        draft_fixed = 0
        for size, cells, layers in draft_caches:
            if ctx and cells == ctx:
                draft_scaling = size * MiB / cells
            else:
                draft_fixed += int(size * MiB)
        draft_compute = max(draft_buffers) if draft_buffers else 0
        if draft_scaling:
            print(f"   draftKvBytesPerToken {draft_scaling:.0f}")
        print(f"   draftContextBytes {draft_fixed + int(draft_compute * MiB)}")
        draft_file = log.draft_file()
        if draft_file and os.path.exists(draft_file):
            print(f"   draftBytes {os.path.getsize(draft_file)}   ({draft_file})")
    if vision and geometry and geometry["tokens"]:
        print(f"   visionBytesPerToken {vision[0] * MiB / geometry['tokens']:.0f}")
    print()


def extrapolate(flash_on, flash_off, app_patches):
    """The vision buffer of a page bigger than the dummy image.

    The flash-on reservation holds the linear part of the graph; the flash-off
    reservation holds the same plus the attention matrix over the patches.
    """
    geometry_on = flash_on.image_tokens()
    geometry_off = flash_off.image_tokens()
    if not geometry_on or not geometry_off:
        print("!! the two logs do not share the dummy image geometry")
        return
    if geometry_on["patches"] != geometry_off["patches"]:
        print("!! the two logs ran with different dummy images")
        return
    vision_on = flash_on.vision_buffers()
    vision_off = flash_off.vision_buffers()
    if not vision_on or not vision_off:
        print("!! no vision reservation in one of the logs")
        return
    n0 = geometry_on["patches"]
    linear = vision_on[0] * MiB / n0
    attention = (vision_off[0] - vision_on[0]) * MiB / (n0 * n0)
    total = linear * app_patches + attention * app_patches * app_patches
    print("-- extrapolated vision buffer")
    print(
        f"   dummy {n0} patches: flash on {vision_on[0]:.2f} MiB,"
        f" off {vision_off[0]:.2f} MiB"
    )
    print(f"   linear {linear:.0f} B/patch   attention {attention:.1f} B/patch^2")
    print(f"   {app_patches} patches -> {total / MiB:.0f} MiB ({total} B)")
    print(f"   visionBufferBytes {int(total)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+")
    parser.add_argument("--app-patches", type=int, help="the patches of a real page")
    args = parser.parse_args()

    logs = [Log(path) for path in args.logs]
    for log in logs:
        report(log)
    if len(logs) == 2 and args.app_patches:
        extrapolate(logs[0], logs[1], args.app_patches)


if __name__ == "__main__":
    sys.exit(main())
