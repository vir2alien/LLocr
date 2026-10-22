#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Варианты запроса fix-задачи (гипотезы).

Вариант — описание одного способа отправить блок модели. Одинаковые у всех
вариантов поля соответствуют тому, что приложение шлёт сейчас (ADR 180/181):
systemPrompt из профиля запросов, typePrompt по типу блока, кандидат в
<CANDIDATE>, картинка кропа, chat_template_kwargs.enable_thinking = false,
параметры запроса из профиля модели.

Ключи варианта:
  name        — имя в отчёте;
  image       — {"scale": float, "resample": "bicubic|lanczos|nearest", "pad_px": int};
  request     — переопределения параметров запроса (temperature, max_tokens, ...);
  extra_body  — добавка к телу запроса (например, chat_template_kwargs);
  repetition  — "none" | "x2" | "x2_verbose" | "x3" (arXiv 2512.14982, prompt repetition);
  repeat_image— при repetition дублировать ли картинку (False = только текст);
  order       — порядок частей user-сообщения: "app" | "candidate_first" | "image_last";
  system/type_prompt — добавка к промптам (null = как в приложении);
  server      — серверные параметры (требуют перезапуска сервера):
                {"image_min_tokens": int, "reasoning_budget": int, ...}.
"""

SCRIPT_HINT_GENERIC = (
    "The document is written in {{LANGUAGE}}. Every subscript, index and designation in the image "
    "is a letter of that language's alphabet, not a Latin look-alike and not a transliteration: "
    "read the glyph as the letter of the document's language that it is, and keep that script. "
    "Do not convert a Cyrillic letter into a Latin one, and do not write a Latin transliteration "
    "of an index."
)


SCRIPT_HINT_RULE = (
    "The document is written in {{LANGUAGE}}. Every subscript, index and designation in the image is "
    "written in the document's script ({{LANGUAGE}} letters), and a Latin transliteration of an index "
    "(H.Bnp for н.впр, Pi for п, T for т, B for в, v for у) is an OCR error, not a spelling of the "
    "image. Read each letter of an index as the {{LANGUAGE}} letter whose shape it has, and keep the "
    "document's script."
)


#: Подсказка о скрипте: диагностика (вариант read_only) показала, что модель
#: читает кириллицу в тексте (M_п — верно), но индексы в формуле переводит в
#: латиницу (н.впр -> H.BIpp). Ей не хватает знания, что это кириллица.
SCRIPT_HINT = (
    "The document is Russian. Every subscript, index and designation in the image is written "
    "in Cyrillic letters, not in Latin look-alikes: read н.впр as н.впр (not H.Bnp or H.BIpp), "
    "п as п (not Pi or n), т as т (not T), в as в (not B), у as у (not v or y). The Latin "
    "transliterations H.Bnp, Pi, T, B, v are never what the image shows. Compare the letters "
    "in the image with Russian letters, and keep the Cyrillic ones."
)

BASE = {
    "image": {"scale": 1.0, "resample": "bicubic", "pad_px": 0, "gray": False,
              "autocontrast": False, "binarize": False},
    "request": {},
    "extra_body": {},
    "repetition": "none",
    "repeat_image": True,
    "order": "app",
    "system": None,
    "type_prompt": None,
    "server": {},
    "task": "fix",          # fix | transcribe | self_check
    "extra_image": None,    # None | "page" (страница целиком как второй кадр)
}


def _v(name, **kwargs):
    variant = {k: (dict(v) if isinstance(v, dict) else v) for k, v in BASE.items()}
    variant["name"] = name
    for key, value in kwargs.items():
        if key in ("image", "request", "extra_body", "server"):
            variant[key].update(value)
        else:
            variant[key] = value
    return variant


#: Базовые варианты первого прогона: как в приложении + масштаб картинки +
#: budget картинки + повтор промпта + режим мышления.
VARIANTS = [
    _v("v0_app"),
    # Теория 2: увеличение кропа перед отправкой (бикубическая интерполяция).
    _v("img_1.5x", image={"scale": 1.5}),
    _v("img_2x", image={"scale": 2.0}),
    _v("img_3x", image={"scale": 3.0}),
    _v("img_2x_lanczos", image={"scale": 2.0, "resample": "lanczos"}),
    # Теория 1: бюджет vision-токенов (серверный параметр).
    _v("vision_2048", server={"image_min_tokens": 2048}),
    _v("vision_4096", server={"image_min_tokens": 4096}),
    # Теория 4: режим мышления с ограниченным бюджетом.
    _v("think_512", server={"reasoning_budget": 512}, extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    _v("think_1024", server={"reasoning_budget": 1024}, extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    # Теория 3: повтор промпта (arXiv 2512.14982).
    _v("repeat_x2", repetition="x2"),
    _v("repeat_x2_verbose", repetition="x2_verbose"),
    _v("repeat_x2_noimg", repetition="x2", repeat_image=False),
    _v("repeat_x3", repetition="x3"),
    # Свои гипотезы.
    # Диагностика: умеет ли модель прочитать индексы вообще (без кандидата,
    # который можно скопировать)?
    _v("read_only", task="transcribe"),
    # Два шага: сначала модель сама читает формулу, потом исправляет кандидат,
    # опираясь на своё чтение.
    _v("self_check", task="self_check"),
    # Предобработка: серый + контраст, бинаризация.
    _v("img_2x_gray", image={"scale": 2.0, "gray": True, "autocontrast": True}),
    _v("img_2x_bin", image={"scale": 2.0, "binarize": True}),
    # Контекст страницы: модель видит страницу и кроп — может догадаться,
    # что индексы кириллические.
    _v("page_ctx", extra_image="page"),
    _v("page_ctx_2x", image={"scale": 2.0}, extra_image="page"),
    # Комбинация: источник увеличен И бюджет vision-токенов поднят (иначе
    # llama.cpp ужмёт картинку обратно и увеличение не даст ничего).
    _v("img_2x_vision_4096", image={"scale": 2.0}, server={"image_min_tokens": 4096}),
    _v("img_3x_vision_4096", image={"scale": 3.0}, server={"image_min_tokens": 4096}),
    _v("img_2x_vision_8192", image={"scale": 2.0}, server={"image_min_tokens": 8192}),
    _v("img_1x_vision_4096", server={"image_min_tokens": 4096}),
    _v("img_1x_vision_2048", server={"image_min_tokens": 2048}),
    # Мышление в комбинации с увеличением и с контекстом страницы.
    _v("think_512_2x", image={"scale": 2.0}, server={"reasoning_budget": 512},
       extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    _v("think_2048", server={"reasoning_budget": 2048},
       extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    _v("think_512_page", extra_image="page", server={"reasoning_budget": 512},
       extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    _v("self_check_think", task="self_check", server={"reasoning_budget": 512},
       extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    # Скрипт-подсказка: диагностика показала, что модель читает кириллицу в
    # тексте (M_п — верно), но в формуле переводит индексы в латиницу
    # (н.впр -> H.BIpp). Значит, ей не хватает знания, что это кириллица.
    _v("script_hint", system=SCRIPT_HINT),
    _v("script_hint_think", system=SCRIPT_HINT, server={"reasoning_budget": 512},
       extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    _v("script_hint_2x", system=SCRIPT_HINT, image={"scale": 2.0}),
    _v("read_script_hint", task="transcribe", system=SCRIPT_HINT),
    # Комбинации лучших по отдельности.
    _v("combo_hint_think_2x", system=SCRIPT_HINT, image={"scale": 2.0},
       server={"reasoning_budget": 512}, extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    _v("combo_hint_think_vision", system=SCRIPT_HINT, server={"image_min_tokens": 4096,
       "reasoning_budget": 512}, extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    _v("combo_think_2x_vision", image={"scale": 2.0}, server={"image_min_tokens": 4096,
       "reasoning_budget": 512}, extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    # Языко-независимая версия подсказки (в приложении язык подставляется в
    # {{LANGUAGE}} — подсказка должна работать для любого языка).
    _v("combo_hint_generic_think_2x", system=SCRIPT_HINT_GENERIC, image={"scale": 2.0},
       server={"reasoning_budget": 512}, extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
    _v("combo_hint_rule_think_2x", system=SCRIPT_HINT_RULE, image={"scale": 2.0},
       server={"reasoning_budget": 512}, extra_body={"chat_template_kwargs": {"enable_thinking": True}}),
]


def by_name(name):
    for variant in VARIANTS:
        if variant["name"] == name:
            return variant
    raise SystemExit(f"нет варианта {name}; есть: {', '.join(v['name'] for v in VARIANTS)}")
