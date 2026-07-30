#pragma once

#include <QString>

// Renders a dictionary-mode JSON payload as styled HTML for the popup.
// Palette-driven (follows the system theme). Falls back to the escaped raw
// payload when it is not a JSON object. (The payload arrives canonicalized
// from the Rust backend — fence/prose stripping happens there.)
//
// Expected (all optional) string fields: word, phonetic, pos, meaning,
// explanation, example.
QString formatWordCardHtml(const QString &jsonPayload);
