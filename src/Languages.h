#pragma once

#include <QList>
#include <QString>

// Target languages offered in Settings. Translation is LLM-powered, so any
// mainstream language works — the system prompt just names the language.
// Flag PNGs are embedded at :/flags/<flag>.png (from flagcdn.com, the
// lipis/flag-icons set, MIT licensed).
struct TargetLanguage {
    const char *code; // value stored in settings ("zh-CN")
    const char *englishName; // used in LLM prompts ("Simplified Chinese")
    const char *nativeName; // shown in the settings combo ("简体中文")
    const char *flag; // :/flags/<flag>.png
};

// Sorted by English name; Settings keeps "Auto" above these.
inline const QList<TargetLanguage> &targetLanguages() {
    static const QList<TargetLanguage> list {
        { "ar", "Arabic", "العربية", "sa" },
        { "cs", "Czech", "Čeština", "cz" },
        { "da", "Danish", "Dansk", "dk" },
        { "nl", "Dutch", "Nederlands", "nl" },
        { "en", "English", "English", "gb" },
        { "fi", "Finnish", "Suomi", "fi" },
        { "fr", "French", "Français", "fr" },
        { "de", "German", "Deutsch", "de" },
        { "hi", "Hindi", "हिन्दी", "in" },
        { "id", "Indonesian", "Bahasa Indonesia", "id" },
        { "it", "Italian", "Italiano", "it" },
        { "ja", "Japanese", "日本語", "jp" },
        { "ko", "Korean", "한국어", "kr" },
        { "pl", "Polish", "Polski", "pl" },
        { "pt", "Portuguese", "Português", "pt" },
        { "ru", "Russian", "Русский", "ru" },
        { "zh-CN", "Simplified Chinese", "简体中文", "cn" },
        { "es", "Spanish", "Español", "es" },
        { "sv", "Swedish", "Svenska", "se" },
        { "th", "Thai", "ไทย", "th" },
        { "zh-TW", "Traditional Chinese", "繁體中文", "tw" },
        { "tr", "Turkish", "Türkçe", "tr" },
        { "uk", "Ukrainian", "Українська", "ua" },
        { "vi", "Vietnamese", "Tiếng Việt", "vn" },
    };
    return list;
}

// English name for a language code ("zh-CN" -> "Simplified Chinese"), or
// empty when the code is unknown (callers fall back to auto behavior).
inline QString languageEnglishName(const QString &code) {
    for (const TargetLanguage &lang : targetLanguages()) {
        if (code == QLatin1StringView(lang.code))
            return QString::fromUtf8(lang.englishName);
    }
    return { };
}
