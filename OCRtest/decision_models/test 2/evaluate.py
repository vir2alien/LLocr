#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Оценка decision-моделей на корпусе OCR_TEST.

Берёт все OCR_TEST_*.llocr рядом со скриптом (один и тот же результат
распознавания, проверенный разными decision-моделями), сверяет текст каждого
блока с эталоном (gt.json/gt.py из ../../ocr_models/materials) и считает
ложноположительные (FP) и ложноотрицательные (FN) срабатывания по типам блоков.

Истина по блокам — в truth.json: CER>0 после carrier-нормализации = ошибка,
CER=0 = чистый блок; clean_overrides переопределяют. Отпечатки текстов блоков
выявляют устаревание истины, если результат распознавания сменился.

Использование:
  python3 evaluate.py                # все OCR_TEST_*.llocr рядом и в подпапках (iteration1/, ...)
  python3 evaluate.py iteration2     # только прогоны указанной подпапки
  python3 evaluate.py --verbose      # + списки FN/FP с расхождениями
  python3 evaluate.py --json out.json
  python3 evaluate.py --materials PATH OTHER.llocr ...

Зависимости: python3, rapidfuzz (pip install rapidfuzz).
"""
import argparse
import difflib
import hashlib
import json
import sys
import tempfile
import zipfile
from collections import OrderedDict, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent

BUCKETS = ('Формулы', 'Таблицы', 'Текст с формулами', 'Простой текст', 'Служебные')
FLAG_STATUSES = {4, 5}  # Review и Mismatch — всё, что не Ok


def load_materials(materials_dir):
    sys.path.insert(0, str(materials_dir))
    import compare
    import gt
    return compare, gt


def read_project(path):
    with tempfile.TemporaryDirectory() as tmp:
        with zipfile.ZipFile(path) as z:
            z.extract('project.json', tmp)
        with open(Path(tmp) / 'project.json', encoding='utf-8') as f:
            return json.load(f)


def norm_text_wrapper_aware(cmp, s):
    # norm_text compare.py не вырезает \( \) — они превратились бы в «лишние
    # скобки»; в рендере их нет, поэтому режем до базовой нормализации.
    import re
    s = re.sub(r'\\\(|\\\)|\\\[|\]|$$', ' ', s)
    s = s.replace('$$', ' ').replace('$', ' ')
    s = re.sub(r'\\tag\s*\{([^{}]*)\}', r'(\1)', s)
    return cmp.norm_text(s)


def bucket_for(group, label):
    if group == 'equation':
        return 'Формулы'
    if group == 'table':
        return 'Таблицы'
    if group == 'ru_math_text':
        return 'Текст с формулами'
    if group in ('en_text', 'ru_text'):
        return 'Простой текст'
    if group is None and label == 'text':
        return 'Текст с формулами'
    return 'Служебные'


def build_rows(projects, gtjson, gtmod, cmp):
    first_name, first = next(iter(projects.items()))
    n_pages = len(first['pages'])
    rows = []
    for pi in range(n_pages):
        n_boxes = len(first['pages'][pi]['boxes'])
        if len(gtjson['pages'][pi]['boxes']) != n_boxes:
            raise SystemExit(
                f'{first_name}, стр. {pi}: в проекте {n_boxes} блоков, в эталоне '
                f'{len(gtjson["pages"][pi]["boxes"])} — разметка разошлась с gt.json')
        for bi in range(n_boxes):
            gtbox = gtjson['pages'][pi]['boxes'][bi]
            text = first['pages'][pi]['boxes'][bi]['text']
            group = gtbox['group']
            if group in ('equation', 'table'):
                ngt, nhyp = cmp.norm_eq(gtmod.GT[pi][bi]), cmp.norm_eq(text)
            else:
                ngt, nhyp = norm_text_wrapper_aware(cmp, gtmod.GT[pi][bi]), norm_text_wrapper_aware(cmp, text)
            rows.append({
                'pi': pi, 'bi': bi, 'label': first['pages'][pi]['boxes'][bi]['label'],
                'group': group, 'bucket': bucket_for(group, first['pages'][pi]['boxes'][bi]['label']),
                'gt': gtmod.GT[pi][bi], 'ocr': text,
                'ngt': ngt, 'nhyp': nhyp, 'cer': cmp.cer(ngt, nhyp),
            })
    return rows


def first_name(projects):
    return next(iter(projects))


def show_diff(row, limit=240):
    sm = difflib.SequenceMatcher(None, row['ngt'], row['nhyp'])
    parts = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            continue
        parts.append(f"[{tag}] '{row['ngt'][i1:i2]}' -> '{row['nhyp'][j1:j2]}'")
    return ' '.join(parts)[:limit] if parts else '(после нормализации равны)'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='*', help='.llocr-проекты (по умолчанию OCR_TEST_*.llocr рядом со скриптом)')
    ap.add_argument('--materials', default=str(HERE.parent.parent / 'ocr_models' / 'materials'),
                    help='папка materials с gt.json/gt.py/compare.py')
    ap.add_argument('--truth', default=str(HERE / 'truth.json'))
    ap.add_argument('--verbose', action='store_true', help='печать списков FN/FP с расхождениями')
    ap.add_argument('--json', dest='json_out', help='машинный отчёт в файл')
    args = ap.parse_args()

    materials = Path(args.materials).resolve()
    if not materials.exists():
        raise SystemExit(f'нет папки materials: {materials} (укажите --materials)')

    files = []
    for f in args.files:
        p = Path(f).resolve()
        if p.is_dir():
            files += sorted(p.glob('OCR_TEST_*.llocr'))
        else:
            files.append(p)
    if not files:
        files = sorted(HERE.glob('**/OCR_TEST_*.llocr'))
    if not files:
        raise SystemExit('нет .llocr-проектов для оценки')
    for f in files:
        if not f.exists():
            raise SystemExit(f'нет файла: {f}')

    cmp, gtmod = load_materials(materials)
    gtjson = json.loads((materials / 'gt.json').read_text(encoding='utf-8'))
    truth = json.loads(Path(args.truth).read_text(encoding='utf-8'))

    def model_name(f):
        rel = f.parent.relative_to(HERE) if f.parent != Path('.') else Path('.')
        return (rel / f.stem.replace('OCR_TEST_', '')).as_posix().lstrip('./')

    candidates = [(model_name(f), read_project(f)) for f in files]
    projects = OrderedDict()
    for name, proj in candidates:
        statuses = {b['checkStatus'] for pg in proj['pages'] for b in pg['boxes']}
        if statuses == {0}:
            print(f'(пропущен {name}: нет вердиктов — это исходный проект распознавания, не прогон decision-модели)')
            continue
        projects[name] = proj
    if not projects:
        raise SystemExit('среди проектов нет ни одного прогона decision-модели (все без вердиктов)')
    print()
    rows = build_rows(projects, gtjson, gtmod, cmp)

    fingerprints = truth['text_fingerprints_sha1']
    stale = []
    for r in rows:
        key = f"p{r['pi']}b{r['bi']}"
        if hashlib.sha1(r['ocr'].encode('utf-8')).hexdigest() != fingerprints.get(key):
            stale.append(r)
    if stale:
        stale_names = ', '.join('p%db%d' % (r['pi'], r['bi']) for r in stale[:10])
        print(f'ВНИМАНИЕ: текст {len(stale)} блоков изменился с момента адьюдикации truth.json ({stale_names}...).')
        print('Их истина могла устареть — из оценки исключены, нужно переразобрать диффы.\n')
    stale_keys = {(r['pi'], r['bi']) for r in stale}

    def truth_of(r):
        key = f"p{r['pi']}b{r['bi']}"
        if (r['pi'], r['bi']) in stale_keys:
            return 'stale'
        if key in truth['clean_overrides']:
            return 'clean'
        return 'err' if r['cer'] > 0 else 'clean'

    def is_script_only(r):
        return f"p{r['pi']}b{r['bi']}" in truth['script_only']

    report = {'models': OrderedDict(), 'consensus': {}, 'stale': [f"p{r['pi']}b{r['bi']}" for r in stale]}
    detail = defaultdict(lambda: defaultdict(list))

    for name, proj in projects.items():
        per_bucket = OrderedDict((b, defaultdict(int)) for b in BUCKETS)
        fn_list, fp_list = [], []
        for r in rows:
            st = proj['pages'][r['pi']]['boxes'][r['bi']]['checkStatus']
            if st == 0:
                per_bucket[r['bucket']]['excluded'] += 1
                continue
            t = truth_of(r)
            if t == 'stale':
                continue
            flagged = st in FLAG_STATUSES
            b = per_bucket[r['bucket']]
            if t == 'err':
                b['err'] += 1
                b['err_content'] += 0 if is_script_only(r) else 1
                if flagged:
                    b['TP'] += 1
                else:
                    b['FN'] += 1
                    b['FN_content'] += 0 if is_script_only(r) else 1
                    fn_list.append(r)
            else:
                b['clean'] += 1
                if flagged:
                    b['FP'] += 1
                    fp_list.append(r)
                else:
                    b['TN'] += 1
        for b in per_bucket.values():
            b['FN_pct'] = round(100 * b['FN'] / b['err'], 1) if b['err'] else 0.0
            b['FP_pct'] = round(100 * b['FP'] / b['clean'], 1) if b['clean'] else 0.0
        totals = OrderedDict()
        for field in ('err', 'err_content', 'clean', 'TP', 'FP', 'FN', 'FN_content', 'TN', 'excluded'):
            totals[field] = sum(b[field] for b in per_bucket.values())
        totals['FN_pct'] = round(100 * totals['FN'] / totals['err'], 1) if totals['err'] else 0.0
        totals['FP_pct'] = round(100 * totals['FP'] / totals['clean'], 1) if totals['clean'] else 0.0
        report['models'][name] = {'buckets': per_bucket, 'totals': totals}
        detail[name] = {'FN': fn_list, 'FP': fp_list}

    groups = OrderedDict()
    for name in projects:
        top = name.split('/')[0] if '/' in name else '(корень)'
        groups.setdefault(top, []).append(name)
    report['consensus'] = OrderedDict()
    for gname, names in groups.items():
        runs = [projects[n] for n in names]
        err_rows = [r for r in rows if truth_of(r) == 'err' and all(
            p['pages'][r['pi']]['boxes'][r['bi']]['checkStatus'] != 0 for p in runs)]
        clean_rows = [r for r in rows if truth_of(r) == 'clean' and all(
            p['pages'][r['pi']]['boxes'][r['bi']]['checkStatus'] != 0 for p in runs)]
        flags = lambda r, p: p['pages'][r['pi']]['boxes'][r['bi']]['checkStatus'] in FLAG_STATUSES
        report['consensus'][gname] = {
            'models': names,
            'err_blocks': len(err_rows),
            'missed_by_all': sum(1 for r in err_rows if not any(flags(r, p) for p in runs)),
            'caught_by_all': sum(1 for r in err_rows if all(flags(r, p) for p in runs)),
            'caught_by_any': sum(1 for r in err_rows if any(flags(r, p) for p in runs)),
            'FP_if_any_flag': sum(1 for r in clean_rows if any(flags(r, p) for p in runs)),
            'clean_blocks': len(clean_rows),
        }

    w = max(len(n) for n in projects)
    first_totals = report['models'][next(iter(report['models']))]['totals']
    print(f"Блоков всего: {len(rows)}; с ошибками: {first_totals['err']}, "
          f"чистых: {first_totals['clean']}, вне области проверки: {first_totals['excluded']}\n")
    for name, m in report['models'].items():
        print(f"===== {name}")
        print(f"{'тип':18s} {'ошибок':>6s} {'чистых':>6s} {'FN':>9s} {'FP':>9s} {'FN(видимых)':>11s}")
        for bname, b in m['buckets'].items():
            if b['excluded'] and not (b['err'] or b['clean']):
                continue
            print(f"{bname:18s} {b['err']:6d} {b['clean']:6d} "
                  f"{b['FN']:3d} ({b['FN_pct']:4.1f}%) {b['FP']:3d} ({b['FP_pct']:4.1f}%) {b['FN_content']:6d}")
        t = m['totals']
        print(f"{'ИТОГО':18s} {t['err']:6d} {t['clean']:6d} "
              f"{t['FN']:3d} ({t['FN_pct']:4.1f}%) {t['FP']:3d} ({t['FP_pct']:4.1f}%) {t['FN_content']:6d}\n")
    for gname, c in report['consensus'].items():
        print(f"Консенсус [{gname}]: пропустили все {c['missed_by_all']}/{c['err_blocks']}, "
              f"поймали все {c['caught_by_all']}, хотя бы одна {c['caught_by_any']} "
              f"(FP при правиле «флаг хотя бы одной»: {c['FP_if_any_flag']}).")

    if args.verbose:
        for name in projects:
            for kind in ('FN', 'FP'):
                print(f"\n----- {name} {kind}")
                for r in detail[name][kind]:
                    key = f"p{r['pi']}b{r['bi']}"
                    note = truth['script_only'].get(key) or truth['clean_overrides'].get(key, '')
                    print(f"  p{r['pi']}b{r['bi']} {r['label']} ({r['bucket']}) {note}")
                    print(f"    diff: {show_diff(r)}")

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding='utf-8')
        print(f"\nотчёт: {args.json_out}")


if __name__ == '__main__':
    main()
