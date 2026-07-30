#pragma once

#include <QString>

// Renders a dictionary-mode JSON payload as styled HTML for the popup.
// Palette-driven (follows the system theme). Falls back to the escaped raw
// payload when it is not a JSON object.
//
// Expected (all optional) string fields: word, phonetic, pos, meaning,
// explanation, example.
QString formatWordCardHtml(const QString &jsonPayload);

// Extracts the JSON object from a model response: strips markdown code
// fences and any prose around it (providers without response_format —
// Gemini, Anthropic — sometimes wrap the answer). Returns the raw string,
// trimmed, when no braces are found.
QString extractJsonPayload(const QString &raw);
