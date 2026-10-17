# -*- coding: utf-8 -*-
import json, re, unicodedata
from rapidfuzz.distance import Levenshtein
from gt import GT

MODELS = {
 'GLM-OCR': 'projects/OCR_TEST_GLM-ocr',
 'LFM2.5-VL': 'projects/OCR_TEST_LFM2.5-VL',
 'LightOnOCR-3': 'projects/OCR_TEST_LightOnOCR-3',
 'MinerU2.5-Pro': 'projects/OCR_TEST_MinerU2.5-Pro',
 'PaddleOCR-VL-1.6': 'projects/OCR_TEST_PaddleOCR-VL-1.6',
}

# ---------- normalization ----------
GREEK = {
 'alpha':'α','beta':'β','gamma':'γ','delta':'δ','epsilon':'ε','varepsilon':'ε',
 'theta':'θ','vartheta':'θ','lambda':'λ','mu':'μ','nu':'ν','pi':'π','rho':'ρ',
 'sigma':'σ','tau':'τ','varphi':'φ','phi':'φ','omega':'ω','eta':'η','kappa':'κ',
}
CMD = {
 'partial':'∂','nabla':'∇','infty':'∞','approx':'≈','cdot':'·','times':'×',
 'pm':'±','mp':'∓','leq':'≤','le':'≤','geq':'≥','ge':'≥','equiv':'≡',
 'sum':'Σ','prod':'∏','int':'∫','iint':'∬','iiint':'∭','oint':'∮','oiint':'∯',
 'sqrt':'√','dots':'…','ldots':'…','cdots':'⋯','neq':'≠','ne':'≠',
 'prime':'′','circ':'∘','degree':'°','propto':'∝','perp':'⊥','angle':'∠',
}
FUNCS = ['arctg','arctan','sin','cos','tan','ln','lg','log','exp','lim','max','min','div','grad','const']

SUB = {c: '_'+chr(ord('0')+i) for i, c in enumerate('₀₁₂₃₄₅₆₇₈₉')}
SUP = {'⁰':'^0','¹':'^1','²':'^2','³':'^3','⁴':'^4','⁵':'^5','⁶':'^6','⁷':'^7','⁸':'^8','⁹':'^9','⁻':'^-','⁺':'^+','ⁿ':'^n'}

WRAPPERS = ['text','mathrm','mathbf','mathit','mathcal','bm','boldsymbol','operatorname','mathsf','mathtt','textnormal','textrm',
            'bar','overline','dot','hat','vec','tilde','widehat','widetilde','underline']

def _strip_wrappers(s):
    # \text{...}, \mathrm{...}, ... → content (balanced braces, iterate to fixpoint)
    for _ in range(60):
        hit = None
        for cmd in WRAPPERS:
            idx = s.find('\\'+cmd)
            if idx != -1 and (hit is None or idx < hit):
                hit = idx; hit_cmd = cmd
        if hit is None: break
        k = hit + 1 + len(hit_cmd)
        while k < len(s) and s[k] == ' ': k += 1
        if k < len(s) and s[k] == '{':
            content, j = _parse_group(s, k)
            if content is None: break
            s = s[:hit] + content + s[j:]
        elif k < len(s) and s[k].isalnum():
            s = s[:hit] + s[k] + s[k+1:]
        else:
            s = s[:hit] + s[k:]  # dangling command — drop
    return s

def _map_cmds(s):
    # greek + symbols to unicode
    out = []
    i = 0
    while i < len(s):
        if s[i] == '\\':
            m = re.match(r'\\([A-Za-z]+)', s[i:])
            if m:
                name = m.group(1)
                if name in GREEK: out.append(GREEK[name]); i += 1+len(name); continue
                if name in CMD: out.append(CMD[name]); i += 1+len(name); continue
                if name == 'tag':
                    m2 = re.match(r'\\tag\s*\{([^{}]*)\}', s[i:])
                    if m2:
                        out.append('('+m2.group(1)+')'); i += m2.end(); continue
                if name in ['left','right','big','Big','bigg','Bigg','displaystyle','limits','nolimits','operatorname','middle','large','Small','small','eqno','noindent','label','centering']:
                    i += 1+len(name); continue
                if name in ['quad','qquad','enspace','thinspace','; ',':','!']:
                    out.append(' '); i += 1+len(name); continue
                if name == 'ell': out.append('l'); i += 1+len(name); continue
                out.append(name)  # keep name (frac, ln, ...) without backslash
                i += 1+len(name); continue
            else:
                i += 1  # stray backslash (escaped char) — drop
                continue
        out.append(s[i]); i += 1
    return ''.join(out)

def _parse_group(s, i):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == '{': depth += 1
        elif s[j] == '}':
            depth -= 1
            if depth == 0: return s[i+1:j], j+1
    return None, len(s)

def _fracs(s):
    # \frac{A}{B} → (A/B), balanced-brace recursive
    for _ in range(200):
        best = min((p for p in (s.find('\\'+k) for k in ('frac','dfrac','tfrac')) if p != -1), default=-1)
        if best == -1: break
        i = s.find('{', best)
        if i == -1: break
        a, j = _parse_group(s, i)
        if a is None: break
        k = s.find('{', j)
        if k == -1: break
        b, m = _parse_group(s, k)
        if b is None: break
        s = s[:best] + '(' + a.strip() + '/' + b.strip() + ')' + s[m:]
    return s

def norm_eq(s):
    if s is None: return ''
    s = unicodedata.normalize('NFC', s)
    s = re.sub(r'\s+', ' ', s)
    s = s.replace('$$',' ').replace('\\[',' ').replace('\\]',' ').replace('\\(',' ').replace('\\)',' ')
    s = s.replace('$',' ')
    s = re.sub(r'\\(begin|end)\s*\{\s*[a-zA-Z*]+\s*\}(\s*\{[^{}]*\})*', ' ', s)
    s = s.replace('\\\\', ' ; ')
    s = s.replace('&','')
    s = _strip_wrappers(s)
    s = _fracs(s)
    s = _map_cmds(s)
    for a,b in [('ell','l')]: pass
    s = s.replace('ℓ','l')
    # unicode sub/sup
    for k,v in SUB.items(): s = s.replace(k,v)
    for k,v in SUP.items(): s = s.replace(k,v)
    s = re.sub(r'_\s*{', '_', s); s = re.sub(r'\^\s*{', '^', s)
    s = s.replace('{','').replace('}','')
    s = re.sub(r'_+','_',s); s = re.sub(r'\^+','^',s)
    s = re.sub(r'\s+','',s)
    dashes = '\u2010\u2011\u2012\u2013\u2014\u2212\u00AD'
    for d in dashes: s = s.replace(d,'-')
    s = s.replace('Ф','Φ').replace('ё','е')
    return s

def norm_text(s):
    if s is None: return ''
    s = unicodedata.normalize('NFC', s)
    s = re.sub(r'\s+', ' ', s)
    s = _strip_wrappers(s)
    s = _fracs(s)
    s = _map_cmds(s)
    s = s.replace('ℓ','l')
    for k,v in SUB.items(): s = s.replace(k,v)
    for k,v in SUP.items(): s = s.replace(k,v)
    # drop markdown/punctuation decorations
    for ch in ['*','_','`','#','|','$','\\','{','}','~','>']:
        s = s.replace(ch,'')
    for q in ['\u00AB','\u00BB','\u0022','\u2018','\u2019','\u201C','\u201D','\u0027']: s = s.replace(q,'')
    s = re.sub(r'\s+','',s)
    dashes = '\u2010\u2011\u2012\u2013\u2014\u2212\u00AD-'
    for d in dashes: s = s.replace(d,'')
    s = s.replace('Ф','Φ').replace('ё','е')
    return s

def cer(ref, hyp):
    if not ref: return 0.0 if not hyp else 1.0
    if not hyp: return 1.0
    d = Levenshtein.distance(ref, hyp)
    return d/len(ref)

# ---------- table parsing ----------
def parse_table(text, model):
    if not text or not text.strip(): return None
    t = text.strip()
    rows = None
    if t.startswith('<'):
        # fcel/nl or html (LightOn wraps markdown tables in <table>...</table>)
        rows = []
        if '<tr' in t.lower():
            for tr in re.findall(r'<tr[^>]*>(.*?)</tr>', t, re.S|re.I):
                cells = re.findall(r'<t[hd][^>]*>(.*?)</t[hd]>', tr, re.S|re.I)
                rows.append([re.sub(r'<[^>]+>','',c).strip() for c in cells])
        elif '<fcel' in t.lower():
            for line in t.split('<nl>'):
                cells = []
                for c in line.split('<fcel>'):
                    c = re.sub(r'</?[a-zA-Z][^>]*>','',c).strip()
                    if c: cells.append(c)
                if cells: rows.append(cells)
        if not rows:
            rows = []
            for line in t.splitlines():
                line = line.strip()
                if not line.startswith('|'): continue
                cells = [c.strip() for c in line.strip('|').split('|')]
                if all(re.fullmatch(r'[-: ]*', c) for c in cells): continue
                rows.append(cells)
    else:
        rows = []
        for line in t.splitlines():
            line = line.strip()
            if not line.startswith('|'): continue
            cells = [c.strip() for c in line.strip('|').split('|')]
            if all(re.fullmatch(r'[-: ]*', c) for c in cells): continue
            rows.append(cells)
    return rows

def gt_table_cells(gt):
    body = gt[len('TABLE:'):]
    return [r.split('|') for r in body.split(';;')]

def strip_note(cell):
    return re.sub(r'\s*\(объед[^)]*\)','',cell)

# ---------- categories ----------
CATS = {
 'en_text': [(0,[0,1,2,3,4])],
 'ru_text': [(1,[3,4,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20])],
 'ru_math_text': [(2,[1,2,3,4,6,8,10,12]),(3,[0,1,2,4,6,7,8,9,10,13]),
   (4,[3,6,8,10,11,13,14,15,17,18,20,21,23,25,29,30,36,37,40,43,45,47,49]),
   (5,[2,5,12,16,19,20,23,27,28,29,31,32,34,36,38,39]),
   (6,[0,6]),(8,[5,7,9,10,11,13,14,15,16]),(9,[14])],
 'equation': [(2,[5,7,9,11,13]),(3,[3,5,11,12,14]),
   (4,[4,5,7,9,12,16,19,22,24,28,31,32,33,34,35,39,42,44,46,48]),
   (5,[3,4,7,9,10,11,13,15,17,22,24,25,26,30,33,35,37]),
   (8,[6,8,12])],
 'table': [(6,[3,5]),(7,[2]),(8,[3]),(9,[3,6,8,10,12])],
 'service': [(0,[5]),(1,[0,1,2,5]),(2,[0,15,16,17]),(3,[15]),
   (4,[0,1,2,26,38,41,50]),(5,[0,1,6,8,14,21]),(6,[1,2,4,7]),(7,[0,1]),
   (8,[0,1,2,4]),(9,[0,1,2,4,5,7,9,11,13])],
}

def is_eq(pi, bi, cats=None):
    return any(pi==p and bi in bs for p,bs in CATS['equation'])
def is_tab(pi, bi):
    return any(pi==p and bi in bs for p,bs in CATS['table'])

# ---------- run ----------
def main():

    projects = {}
    for name, folder in MODELS.items():
        d = json.load(open(f'{folder}/project.json', encoding='utf-8'))
        projects[name] = d

    results = {}   # model -> cat -> [ (pi,bi,cer) ]
    boxdump = {}   # model -> (pi,bi) -> normalized hyp for inspection
    for name, d in projects.items():
        results[name] = {}
        boxdump[name] = {}
        for cat, plist in CATS.items():
            acc = []
            for pi, bis in plist:
                for bi in bis:
                    gt = GT[pi][bi]
                    hyp = d['pages'][pi]['boxes'][bi]['text']
                    if cat == 'table':
                        acc.append((pi,bi,None))  # handled separately
                        continue
                    if is_eq(pi,bi):
                        ngt, nhyp = norm_eq(gt), norm_eq(hyp)
                    else:
                        ngt, nhyp = norm_text(gt), norm_text(hyp)
                    c = cer(ngt, nhyp)
                    acc.append((pi,bi,c))
                    boxdump[name][(pi,bi)] = (ngt, nhyp, c)
            results[name][cat] = acc

    # length-weighted aggregate
    def agg(entries):
        # entries: list of (pi,bi,c) — recompute weighted using gt normalized length
        tot_edits = 0.0; tot_len = 0; n = 0; s = 0.0
        for pi,bi,c in entries:
            gt = GT[pi][bi]
            ng = norm_eq(gt) if is_eq(pi,bi) else norm_text(gt)
            L = max(1,len(ng))
            tot_edits += c*L; tot_len += L; s += c; n += 1
        return tot_edits/tot_len, s/n, n

    print('='*100)
    summary = {}
    for name in MODELS:
        print(f'### {name}')
        summary[name] = {}
        for cat in ['en_text','ru_text','ru_math_text','equation','service']:
            w,m,n = agg(results[name][cat])
            summary[name][cat] = (w,m,n)
            print(f'  {cat:14s} WER-CER(weighted)={w*100:6.2f}%  mean={m*100:6.2f}%  n={n}')
        print()

    json.dump({m:{c:summary[m][c] for c in summary[m]} for m in summary}, open('results/summary.json','w'), ensure_ascii=False, indent=1)

    # worst blocks per model per category
    print('='*100)
    print('WORST BLOCKS (per model, CER>=0.35):')
    for name in MODELS:
        print(f'### {name}')
        for (pi,bi,c) in sorted(results[name]['ru_math_text']+results[name]['en_text']+results[name]['ru_text']+results[name]['equation'], key=lambda e:-e[2]):
            if c >= 0.35:
                print(f'  p{pi} b{bi}: CER={c*100:.0f}%')


if __name__ == '__main__':
    main()
