#!/usr/bin/env python3
"""Верификация скачанных GGUF: размер + sha256 против tree API HF."""
import hashlib, json, os, subprocess, sys, time

BASE = os.path.dirname(os.path.abspath(__file__))
REPOS = {
    'prithivMLmods/Xiaomi-OCR-0-GGUF': 'xiaomi-ocr-0',
    'michalland/OvisOCR3-GGUF': 'ovisocr3',
    'prithivMLmods/HunyuanOCR-1.5-GGUF-Updated': 'hunyuanocr-15',
    'aenawi/MonkeyOCRv2-B-GGUF': 'monkeyocrv2-b',
    'lodrick-the-lafted/dots.mocr-gguf': 'dots-mocr',
    'SandLogicTechnologies/DeepSeek-OCR-2-GGUF': 'deepseek-ocr-2',
    'mradermacher/PepperOCR-VL-GGUF': 'pepperocr-vl',
}
WANT = {
    'xiaomi-ocr-0': ['Xiaomi-OCR-0.Q6_K.gguf', 'Xiaomi-OCR-0.mmproj-bf16.gguf'],
    'ovisocr3': ['OvisOCR3-Q8_0.gguf', 'mmproj-OvisOCR3-F16.gguf'],
    'hunyuanocr-15': ['HunyuanOCR.Q8_0.gguf', 'HunyuanOCR.mmproj-f16.gguf'],
    'monkeyocrv2-b': ['MonkeyOCRv2-B.q8_0.gguf', 'MonkeyOCRv2-B.mmproj-f16.gguf'],
    'dots-mocr': ['dotsmocr-1.8b-q8_0.gguf', 'mmproj-dotsmocr-bf16.gguf'],
    'deepseek-ocr-2': ['DeepSeek-OCR-2-IQ4_XS.gguf', 'mmproj-deepseek-ocr-2-bf16.gguf'],
    'pepperocr-vl': ['PepperOCR-VL.Q8_0.gguf', 'PepperOCR-VL.mmproj-f16.gguf'],
}

def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()

results = {}
for repo, d in REPOS.items():
    meta = None
    for attempt in range(5):
        out = subprocess.run(['curl', '-s', '-m', '40',
                              f'https://huggingface.co/api/models/{repo}/tree/main'],
                             capture_output=True, text=True).stdout
        try:
            meta = json.loads(out)
            break
        except Exception:
            time.sleep(15)
    if not meta:
        print(f'!! {repo}: tree API unavailable')
        continue
    want = {f: None for f in WANT[d]}
    for item in meta:
        if item['path'] in want:
            want[item['path']] = (item['size'], item.get('lfs', {}).get('oid', ''))
    for f, expect in want.items():
        p = os.path.join(BASE, d, f)
        if expect is None:
            print(f'?? {d}/{f}: not in repo listing')
            continue
        size, oid = expect
        if not os.path.exists(p):
            print(f'MISSING {d}/{f}')
            results[f'{d}/{f}'] = 'missing'
            continue
        actual = os.path.getsize(p)
        if actual != size:
            print(f'BAD SIZE {d}/{f}: {actual} != {size}')
            results[f'{d}/{f}'] = 'bad-size'
            continue
        ok = oid and sha256(p) == oid
        print(f'{"OK " if ok else "BAD SHA"} {d}/{f} ({size} bytes)')
        results[f'{d}/{f}'] = 'ok' if ok else 'bad-sha'
bad = [k for k, v in results.items() if v != 'ok']
print('\nRESULT:', 'ALL OK' if not bad else f'PROBLEMS: {bad}')
sys.exit(0 if not bad else 1)
