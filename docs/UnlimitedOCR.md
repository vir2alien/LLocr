# UnlimitedOCR

## Links:

- https://huggingface.co/baidu/Unlimited-OCR

- https://github.com/baidu/Unlimited-OCR

## List of tokens:

text, title, image (figure), table, formula (equation/isolate_formula), caption (figure_caption / table_caption), figure_footnote / table_footnote, header, footer, page_number, list, reference, code, abstract, seal

Cli-request (for tests):

```bash
build/bin/llama-mtmd-cli \
  -m ~/models/ocr/Unlimited-OCR-GGUF/Unlimited-OCR-BF16.gguf \
  --mmproj ~/models/ocr/Unlimited-OCR-GGUF/mmproj-Unlimited-OCR-F16.gguf \
  --image '~/Desktop/OCR DEBUG/Unlimited-OCR/Unlimited-OCR-03.png' -p "document parsing." \
  --chat-template deepseek-ocr --no-jinja \
  --temp 0 --flash-attn off --no-warmup \
  --n-predict 4096 --ctx-size 16384 \
  --dry-multiplier 0.8 --dry-base 1.75 --dry-allowed-length 35 \
  --dry-penalty-last-n 128 --dry-sequence-breaker none
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

