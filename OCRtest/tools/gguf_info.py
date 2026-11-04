#!/usr/bin/env python3
"""Мини-читалка GGUF-метаданных: arch, имя, EOS-токен, наличие chat template."""
import struct, sys, pathlib

TYPES = {0:('u8',1),1:('i8',1),2:('u16',2),3:('i16',2),4:('u32',4),5:('i32',4),
         6:('f32',4),7:('bool',1),8:('str',None),9:('arr',None),10:('u64',8),11:('i64',8),12:('f64',8)}

def read_str(f):
    n = struct.unpack('<Q', f.read(8))[0]
    return f.read(n).decode('utf-8', 'replace')

def read_val(f, t):
    name, size = TYPES[t]
    if name == 'str':
        return read_str(f)
    if name == 'arr':
        et = struct.unpack('<I', f.read(4))[0]
        n = struct.unpack('<Q', f.read(8))[0]
        vals = []
        for _ in range(min(n, 4)):  # длинные массивы (токенайзер) не читаем целиком
            vals.append(read_val(f, et))
        if n > 4:
            # промотать остальные
            skip_sizes = {0:1,1:1,2:2,3:2,4:4,5:4,6:4,7:1,10:8,11:8,12:8}
            if et == 8:
                for _ in range(n-4):
                    f.seek(struct.unpack('<Q', f.read(8))[0], 1)
            elif et == 9:
                raise SystemExit('nested array skip not supported')
            else:
                f.seek(skip_sizes[et]*(n-4), 1)
            vals.append(f'...({n} items)')
        return vals
    return struct.unpack('<b' if name=='i8' else '<B' if name=='u8' else '<h' if name=='i16' else '<H' if name=='u16' else '<i' if name=='i32' else '<I' if name=='u32' else '<f' if name=='f32' else '<?' if name=='bool' else '<q' if name=='i64' else '<Q' if name=='u64' else '<d', f.read(size))[0]

def info(path):
    f = open(path, 'rb')
    magic = f.read(4)
    assert magic == b'GGUF', 'not gguf'
    ver, = struct.unpack('<I', f.read(4))
    tensors, kvs = struct.unpack('<QQ', f.read(16))
    out = {'_tensors': tensors}
    for _ in range(kvs):
        key = read_str(f)
        t = struct.unpack('<I', f.read(4))[0]
        try:
            v = read_val(f, t)
        except Exception as e:
            out[key] = f'<err {e}>'
            break
        if key in ('general.architecture','general.name','general.size_label','general.file_type',
                   'tokenizer.ggml.eos_token_id','tokenizer.ggml.bos_token_id') or 'chat_template' in key or 'eos' in key.lower():
            if key == 'tokenizer.chat_template':
                v = v[:220].replace('\n', ' ')
            out[key] = v
    return out

for p in sys.argv[1:]:
    print('==', pathlib.Path(p).name, f'({pathlib.Path(p).stat().st_size/1e9:.2f} GB)')
    for k, v in info(pathlib.Path(p)).items():
        print('  ', k, '=', v)
