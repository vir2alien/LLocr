Работаем над улучшением системы decision-моделей.
1) Синхронизировать правила эквивалентности между моделями decision и fix
2) Разнести порог matchThreshold по типам блоков (block_type)
3) Использовать главную фичу decision-моделей: несколько вопросов в одном вызове (/v1/systemone) бесплатно по латентности.
Предлагается заготовка профиля с учётом этих пожеланий.
В каждом из 19 типов блоков (roles.decision.blockPrompts[block_type]) теперь лежат:
    statePrompt — правила эквивалентности и строгих различий именно для этого типа блока (например, для code разметка и пробелы — строгий контент; для equation разрешены эквивалентные LaTeX-команды; для table координаты ячеек row × col и rowspan/colspan — строгий контент).
    question — одиночный вопрос match (оставлен для 100% обратной совместимости со старым кодом v5).
    questions — готовый словарь из 4 вопросов, который напрямую передаётся в поле "questions" запроса /v1/systemone:
        match (type: "noul") — совпадает ли распознанный текст с кропом по правилам данного типа блока;
        has_text (type: "noul") — есть ли вообще на кропе читаемый текст/символы (отсекает пустые рамки, линии, шум и галлюцинации детектора макета);
        readable (type: "noul") — читаем ли и не обрезан ли кроп в тех местах, которые нужны для сверки;
        diagnosis (type: "choice") — 4 взаимоисключающих класса с критериями, адаптированными под конкретный тип блока:
            exact_match
            fixable_ocr_error (описаны типичные ошибки именно этого блока: например, сдвиг ячеек в table, индексы/греческие буквы в equation, омоглифы в ref_text)
            unreadable_or_clipped
            empty_crop
Сборка запроса из профиля (код для примера):

const aliasKey = role.blockAliases?.[blockType] ?? blockType;
const blockCfg = role.blockPrompts?.[aliasKey];

const requestBody = {
  state: {
    block_type: blockType,
    recognized_text: candidateText,
    notation: blockCfg?.statePrompt ?? role.statePrompt,
  },
  images: [base64DataUrl],
  // Передаём сразу все 4 вопроса в один вызов /v1/systemone:
  questions: blockCfg?.questions ?? role.questions,
};

Ответ от llama-server (/v1/systemone) приходит за один проход модели в таком виде:
{
  "answers": {
    "match": { "type": "noul", "noul": 0.18 },
    "has_text": { "type": "noul", "noul": 0.99 },
    "readable": { "type": "noul", "noul": 0.94 },
    "diagnosis": {
      "type": "choice",
      "choice": {
        "exact_match": 0.12,
        "fixable_ocr_error": 0.83,
        "unreadable_or_clipped": 0.04,
        "empty_crop": 0.01
      }
    }
  }
}
Логика маршрутизации (4 исхода вместо бинарного Ok / Mismatch)
ts

const pMatch = answers.match.noul;
const pHasText = answers.has_text.noul;
const pReadable = answers.readable.noul;
const diag = answers.diagnosis.choice;
const topDiag = Object.entries(diag).sort((a, b) => b[1] - a[1])[0][0];

const matchThreshold =
  role.matchThresholdByBlock?.[aliasKey] ?? role.matchThreshold ?? 0.70;

if (pHasText < (role.emptyThreshold ?? 0.25) || topDiag === "empty_crop") {
  // 1. Кроп пустой -> если candidateText не пустой, это галлюцинация OCR (EMPTY)
  return { action: "EMPTY" };
}

if (pReadable < (role.readableThreshold ?? 0.45) || topDiag === "unreadable_or_clipped") {
  // 2. Кроп обрезан или размыт -> сразу в REVIEW человеку, НЕ тратим GPU на Qwen3.5-9B!
  return { action: "REVIEW_DIRECT" };
}

if (pMatch >= matchThreshold && topDiag === "exact_match") {
  // 3. Оба индикатора (noul и choice) согласны, что текст точен -> OK
  return { action: "OK" };
}

// 4. Кроп читаемый, но есть исправимая ошибка OCR -> отправляем в Qwen3.5-9B (llocr-fix)
return { action: "SEND_TO_FIXER" };

Такой двойной фильтр (pMatch >= threshold && topDiag === "exact_match") одновременно снижает пропуск реальных ошибок и экономит вызовы Qwen3.5-9B на пустых и заведомо нечитаемых кропах.
