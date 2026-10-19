# d1-3B decision model — оценка разделения «да»/«нет»

- корпус: /Users/gladskikh/Git/LLocr/OCRtest/TEST.llocr
- модель: Cloudflare_clef-flash-Q5_K_M.gguf + mmproj-Cloudflare_clef-flash-f16.gguf, ctx 8192
- дата: 2026-10-13 23:51
- кейсов в отчёте: 5904
- порог в приложении сейчас: 0.6 (decision/matchThreshold)

## Рейтинг вариантов (AUC: match vs mismatch)

| вариант | запр. | AUC | AUC subtle | разрыв средних | лучший порог | acc@порог | пропуск ошибок@0.5 | ложн. тревога@0.5 | нотация FN | sanity<0.2 | отказы |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `v5_structured_single` | 437 | 0.830 | 0.827 | 0.313 | 0.86 | 0.810 | 0.5 | 0.1 | 0.58 | 0.50 | 0 |
| `v3_multi_question` | 1101 | 0.750 | 0.750 | 0.171 | 0.81 | 0.780 | 0.4 | 0.2 | 0.88 | 0.06 | 0 |
| `v2_structured_state` | 1101 | 0.741 | 0.730 | 0.182 | 0.85 | 0.784 | 0.5 | 0.2 | 0.85 | 0.06 | 0 |
| `v1_minimal_noul` | 347 | 0.733 | 0.730 | 0.114 | 0.78 | 0.777 | 0.7 | 0.1 | 0.84 | 0.00 | 0 |
| `v1b_minimal_crit` | 347 | 0.711 | 0.708 | 0.083 | 0.78 | 0.781 | 0.8 | 0.0 | 0.84 | 0.00 | 0 |
| `v4_choice_4way` | 347 | 0.709 | 0.717 | 0.101 | 0.47 | 0.774 | 0.0 | 0.9 | 0.98 | 0.12 | 0 |
| `v0_app_current` | 617 | 0.695 | 0.686 | 0.077 | 0.92 | 0.757 | 0.9 | 0.0 | 1.00 | 0.00 | 0 |
| `v9_eq_two_images` | 225 | 0.632 | 0.602 | 0.052 | 0.71 | 0.685 | 0.6 | 0.3 | 0.95 | 0.00 | 0 |
| `v6_type_aware` | 367 | 0.621 | 0.617 | 0.068 | 0.81 | 0.757 | 0.7 | 0.2 | 0.86 | 0.00 | 0 |
| `v8_eq_symbols` | 322 | 0.612 | 0.608 | 0.066 | 0.91 | 0.758 | 0.7 | 0.2 | 0.95 | 0.00 | 0 |
| `v7_tab_rows` | 45 | 0.539 | 0.475 | 0.011 | 0.83 | 0.750 | 1.0 | 0.0 | 0.89 | — | 0 |

Пороги 0.5/0.6 и крайние случаи: `missed@T` — доля испорченных блоков, принятых как верные (p ≥ T); `alarm@T` — доля верных блоков, отвергнутых (p < T).

## `v5_structured_single`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 90 | 0.764 | 0.846 | 0.497 | 0.908 |
| notation | 83 | 0.760 | 0.835 | 0.509 | 0.895 |
| mismatch | 262 | 0.451 | 0.455 | 0.094 | 0.847 |
| subtle | 88 | 0.474 | 0.516 | 0.098 | 0.822 |
| sanity_blank | 1 | 0.835 | 0.835 | 0.835 | 0.835 |
| sanity_cross | 1 | 0.108 | 0.108 | 0.108 | 0.108 |

input_tokens: p90 = 1238, max = 5059; latency: mean = 6.75 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 64 | 0.805 | 0.460 |
| footer | 1 | 0.888 | 0.335 |
| header | 1 | 0.825 | 0.466 |
| image_caption | 1 | 0.640 | 0.488 |
| page_number | 4 | 0.810 | 0.179 |
| table | 9 | 0.667 | 0.503 |
| text | 6 | 0.589 | 0.452 |
| title | 2 | 0.787 | 0.400 |

## `v3_multi_question`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 221 | 0.634 | 0.616 | 0.402 | 0.888 |
| notation | 207 | 0.567 | 0.554 | 0.344 | 0.832 |
| mismatch | 657 | 0.462 | 0.458 | 0.232 | 0.703 |
| subtle | 219 | 0.459 | 0.450 | 0.209 | 0.693 |
| sanity_blank | 8 | 0.763 | 0.785 | 0.643 | 0.848 |
| sanity_cross | 8 | 0.571 | 0.645 | 0.477 | 0.719 |

input_tokens: p90 = 1644, max = 11520; latency: mean = 6.24 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 71 | 0.629 | 0.552 |
| footer | 2 | 0.889 | 0.335 |
| header | 12 | 0.790 | 0.502 |
| image_caption | 2 | 0.665 | 0.492 |
| page_number | 22 | 0.881 | 0.176 |
| table | 14 | 0.600 | 0.563 |
| table_caption | 17 | 0.719 | 0.429 |
| table_footnote | 7 | 0.614 | 0.489 |
| text | 117 | 0.600 | 0.427 |
| title | 9 | 0.803 | 0.475 |

## `v2_structured_state`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 221 | 0.687 | 0.695 | 0.430 | 0.950 |
| notation | 207 | 0.622 | 0.606 | 0.390 | 0.913 |
| mismatch | 657 | 0.505 | 0.513 | 0.237 | 0.778 |
| subtle | 219 | 0.513 | 0.521 | 0.237 | 0.779 |
| sanity_blank | 8 | 0.892 | 0.923 | 0.860 | 0.952 |
| sanity_cross | 8 | 0.636 | 0.712 | 0.450 | 0.855 |

input_tokens: p90 = 508, max = 3800; latency: mean = 5.62 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 71 | 0.625 | 0.542 |
| footer | 2 | 0.912 | 0.280 |
| header | 12 | 0.838 | 0.533 |
| image_caption | 2 | 0.755 | 0.570 |
| page_number | 22 | 0.910 | 0.166 |
| table | 14 | 0.715 | 0.672 |
| table_caption | 17 | 0.769 | 0.455 |
| table_footnote | 7 | 0.701 | 0.566 |
| text | 117 | 0.688 | 0.501 |
| title | 9 | 0.844 | 0.503 |

## `v1_minimal_noul`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 70 | 0.694 | 0.682 | 0.557 | 0.840 |
| notation | 57 | 0.669 | 0.669 | 0.556 | 0.779 |
| mismatch | 204 | 0.580 | 0.601 | 0.386 | 0.735 |
| subtle | 68 | 0.588 | 0.600 | 0.421 | 0.712 |
| sanity_blank | 8 | 0.575 | 0.601 | 0.473 | 0.687 |
| sanity_cross | 8 | 0.541 | 0.539 | 0.411 | 0.685 |

input_tokens: p90 = 1316, max = 3711; latency: mean = 2.48 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 10 | 0.689 | 0.633 |
| footer | 1 | 0.673 | 0.383 |
| header | 6 | 0.765 | 0.649 |
| image_caption | 1 | 0.605 | 0.599 |
| page_number | 11 | 0.784 | 0.469 |
| table | 9 | 0.669 | 0.648 |
| table_caption | 8 | 0.696 | 0.570 |
| table_footnote | 3 | 0.685 | 0.602 |
| text | 15 | 0.612 | 0.549 |
| title | 4 | 0.815 | 0.655 |

## `v1b_minimal_crit`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 70 | 0.707 | 0.701 | 0.616 | 0.816 |
| notation | 57 | 0.687 | 0.690 | 0.599 | 0.791 |
| mismatch | 204 | 0.624 | 0.637 | 0.475 | 0.745 |
| subtle | 68 | 0.627 | 0.631 | 0.489 | 0.739 |
| sanity_blank | 8 | 0.740 | 0.750 | 0.656 | 0.805 |
| sanity_cross | 8 | 0.656 | 0.679 | 0.601 | 0.709 |

input_tokens: p90 = 1375, max = 3770; latency: mean = 2.73 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 10 | 0.677 | 0.639 |
| footer | 1 | 0.616 | 0.453 |
| header | 6 | 0.770 | 0.673 |
| image_caption | 1 | 0.730 | 0.703 |
| page_number | 11 | 0.753 | 0.580 |
| table | 9 | 0.718 | 0.699 |
| table_caption | 8 | 0.705 | 0.595 |
| table_footnote | 3 | 0.698 | 0.627 |
| text | 15 | 0.642 | 0.584 |
| title | 4 | 0.808 | 0.695 |

## `v4_choice_4way`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 70 | 0.331 | 0.276 | 0.195 | 0.649 |
| notation | 57 | 0.255 | 0.251 | 0.180 | 0.322 |
| mismatch | 204 | 0.230 | 0.223 | 0.121 | 0.333 |
| subtle | 68 | 0.225 | 0.217 | 0.116 | 0.336 |
| sanity_blank | 8 | 0.380 | 0.365 | 0.276 | 0.494 |
| sanity_cross | 8 | 0.227 | 0.227 | 0.184 | 0.264 |

input_tokens: p90 = 1401, max = 3796; latency: mean = 2.93 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 10 | 0.281 | 0.248 |
| footer | 1 | 0.170 | 0.171 |
| header | 6 | 0.288 | 0.211 |
| image_caption | 1 | 0.300 | 0.292 |
| page_number | 11 | 0.628 | 0.227 |
| table | 9 | 0.267 | 0.255 |
| table_caption | 8 | 0.303 | 0.202 |
| table_footnote | 3 | 0.282 | 0.221 |
| text | 15 | 0.245 | 0.211 |
| title | 4 | 0.361 | 0.300 |

## `v0_app_current`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 124 | 0.710 | 0.716 | 0.601 | 0.812 |
| notation | 111 | 0.689 | 0.696 | 0.585 | 0.786 |
| mismatch | 366 | 0.634 | 0.640 | 0.469 | 0.776 |
| subtle | 122 | 0.639 | 0.653 | 0.490 | 0.770 |
| sanity_blank | 8 | 0.598 | 0.567 | 0.458 | 0.827 |
| sanity_cross | 8 | 0.607 | 0.648 | 0.509 | 0.715 |

input_tokens: p90 = 1253, max = 4210; latency: mean = 5.94 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 71 | 0.705 | 0.628 |
| footer | 2 | 0.784 | 0.509 |
| header | 12 | 0.747 | 0.667 |
| image_caption | 2 | 0.777 | 0.734 |
| page_number | 22 | 0.855 | 0.699 |
| table | 14 | 0.727 | 0.710 |
| table_caption | 17 | 0.687 | 0.585 |
| table_footnote | 7 | 0.675 | 0.621 |
| text | 20 | 0.613 | 0.557 |
| title | 9 | 0.747 | 0.670 |

## `v9_eq_two_images`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 55 | 0.572 | 0.565 | 0.447 | 0.713 |
| notation | 58 | 0.521 | 0.501 | 0.384 | 0.656 |
| mismatch | 110 | 0.520 | 0.515 | 0.380 | 0.656 |
| subtle | 34 | 0.530 | 0.533 | 0.391 | 0.664 |
| sanity_blank | 1 | 0.628 | 0.628 | 0.628 | 0.628 |
| sanity_cross | 1 | 0.599 | 0.599 | 0.599 | 0.599 |

input_tokens: p90 = 413, max = 3874; latency: mean = 7.56 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 55 | 0.572 | 0.520 |

## `v6_type_aware`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 73 | 0.650 | 0.644 | 0.445 | 0.855 |
| notation | 73 | 0.645 | 0.657 | 0.445 | 0.850 |
| mismatch | 219 | 0.582 | 0.573 | 0.394 | 0.794 |
| subtle | 73 | 0.584 | 0.587 | 0.394 | 0.809 |
| sanity_blank | 1 | 0.789 | 0.789 | 0.789 | 0.789 |
| sanity_cross | 1 | 0.619 | 0.619 | 0.619 | 0.619 |

input_tokens: p90 = 2155, max = 3886; latency: mean = 10.25 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 64 | 0.660 | 0.587 |
| table | 9 | 0.582 | 0.545 |

## `v8_eq_symbols`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 64 | 0.652 | 0.652 | 0.455 | 0.868 |
| notation | 64 | 0.646 | 0.647 | 0.440 | 0.883 |
| mismatch | 192 | 0.586 | 0.568 | 0.374 | 0.830 |
| subtle | 64 | 0.585 | 0.592 | 0.363 | 0.839 |
| sanity_blank | 1 | 0.797 | 0.797 | 0.797 | 0.797 |
| sanity_cross | 1 | 0.626 | 0.626 | 0.626 | 0.626 |

input_tokens: p90 = 561, max = 2151; latency: mean = 6.82 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| equation | 64 | 0.652 | 0.586 |

## `v7_tab_rows`

| класс | n | mean | median | p10 | p90 |
|---|---|---|---|---|---|
| match | 9 | 0.664 | 0.663 | 0.537 | 0.748 |
| notation | 9 | 0.658 | 0.656 | 0.534 | 0.747 |
| mismatch | 27 | 0.653 | 0.659 | 0.529 | 0.775 |
| subtle | 9 | 0.678 | 0.669 | 0.533 | 0.775 |

input_tokens: p90 = 3687, max = 3687; latency: mean = 46.11 s

| тип блока | n(match) | mean match | mean mismatch |
|---|---|---|---|
| table | 9 | 0.664 | 0.653 |

## Худшие кейсы (уверенно неверные вердикты)

- `v2_structured_state` mismatch/gross p=0.96 equation стр.3 [drop word 'qquad'] «$$\beta = 1 / T ; \ \qquad \qquad \qquad ( 6 8 )$$»
- `v2_structured_state` sanity_blank/- p=0.96 image_caption стр.1 [blank_image] «Рис. 8.7. Цилиндрическая стенка с граничными условиями III рода»
- `v2_structured_state` mismatch/gross p=0.95 equation стр.2 [drop word 'mathrm'] «$$S = \eta \, W , \qquad \qquad \qquad \qquad \qquad \qquad \qquad \qq»
- `v2_structured_state` sanity_blank/- p=0.95 title стр.0 [blank_image] «1. Introduction»
- `v2_structured_state` sanity_blank/- p=0.95 page_number стр.0 [blank_image] «3»
- `v2_structured_state` mismatch/subtle p=0.94 equation стр.2 ['.'->','] «$$s = S n - p n , \qquad \qquad { \mathrm { ( 6 ) } }$$»
- `v8_eq_symbols` mismatch/gross p=0.94 equation стр.3 [drop word 'qquad'] «$$\beta = 1 / T ; \ \qquad \qquad \qquad ( 6 8 )$$»
- `v2_structured_state` mismatch/moderate p=0.94 text стр.9 [swap 'in long-horizon'] «- We conduct a preliminary validation of MLLM architectures with linea»
- `v8_eq_symbols` mismatch/gross p=0.94 equation стр.2 [drop word 'mathrm'] «$$S = \eta \, W , \qquad \qquad \qquad \qquad \qquad \qquad \qquad \qq»
- `v2_structured_state` mismatch/gross p=0.94 text стр.9 [append word 'in'] «- We conduct a preliminary validation of MLLM architectures with linea»
- `v2_structured_state` sanity_blank/- p=0.94 title стр.1 [blank_image] «8.2.2. ЦИЛИНДРИЧЕСКАЯ СТЕНКА С ГРАНИЧНЫМИ УСЛОВИЯМИ III РОДА»
- `v2_structured_state` mismatch/subtle p=0.94 header стр.8 [homoglyph 'М'->'M'] «ИСПОЛЬЗУЕMЫЕ ОБОЗНАЧЕНИЯ»
- `v2_structured_state` mismatch/moderate p=0.93 text стр.0 [duplicate word 'Nor'] «Consider the act of transcribing a document. As we copy each character»
- `v2_structured_state` mismatch/subtle p=0.93 equation стр.2 ['.'->','] «$$h = h ( p , T ) , \qquad \qquad { \mathrm { ( 3 ) } }$$»
- `v2_structured_state` match/- p=0.08 image стр.1 [original] «»

## Ось разрешения изображений

- `v0_app_current` / down1024: n=36, AUC=0.658, match=0.729, mismatch=0.674
- `v2_structured_state` / down1024: n=36, AUC=0.700, match=0.766, mismatch=0.653
- `v3_multi_question` / down1024: n=36, AUC=0.733, match=0.651, mismatch=0.542
- `v0_app_current` / up2x_small: n=180, AUC=0.740, match=0.734, mismatch=0.632
- `v2_structured_state` / up2x_small: n=180, AUC=0.900, match=0.785, mismatch=0.403
- `v3_multi_question` / up2x_small: n=179, AUC=0.904, match=0.746, mismatch=0.387
