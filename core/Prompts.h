#pragma once

#include <QString>

// System-prompt templates, loaded from spec/prompts.json (embedded as
// :/spec/prompts.json) — the single source of truth shared with future
// non-C++ clients. {target} is replaced with the English language name
// ("Simplified Chinese"); see spec/integration.md for the full contract.
namespace Prompts {

// Plain sentence/paragraph translation.
QString phrase(const QString &targetLanguageName);

// Short-selection mode: the model decides word (dictionary card JSON) vs
// phrase (translation JSON).
QString word(const QString &targetLanguageName);

} // namespace Prompts
