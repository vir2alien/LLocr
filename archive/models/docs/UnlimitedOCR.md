# UnlimitedOCR

## Links:

- https://huggingface.co/baidu/Unlimited-OCR

- https://github.com/baidu/Unlimited-OCR

## List of tokens:

text, title, image (figure), table, formula (equation/isolate_formula), caption (figure_caption / table_caption), figure_footnote / table_footnote, header, footer, page_number, list, reference, code, abstract, seal

Current llama.cpp run

```bash
cd software/llama.cpp && ./build/bin/llama-server \
  --model ~/models/ocr/Unlimited-OCR-GGUF/Unlimited-OCR-Q8_0.gguf \
  --mmproj ~/models/ocr/Unlimited-OCR-GGUF/mmproj-Unlimited-OCR-F16.gguf \
  --special \
  --temp 0 \
  --n-predict 8192 --ctx-size 16384 \
  --parallel 1 --cache-reuse 0 --no-context-shift \
  --cache-type-k f32 --cache-type-v f32 \
  --image-min-tokens 456 --image-max-tokens 1156 \
  --no-warmup --flash-attn off \
  --dry-sequence-breaker none
```

Server request (with optimal dry-parameters):

```json
{
  "model": "Unlimited-OCR",
  "messages": [
    { "role": "user", "content": [
      { "type": "image_url", "image_url": { "url": "data:image/png;base64,..." } },
      { "type": "text", "text": "document parsing." }
    ]}
  ],
  "dry_multiplier": 0.8,
  "dry_base": 1.75,
  "dry_allowed_length": 35,
  "dry_penalty_last_n": 128,
  "dry_sequence_breakers": [none],
  "temperature": 0,
  "max_tokens": 4096,
  "skip_special_tokens": false,
  "stream": false
}
```

