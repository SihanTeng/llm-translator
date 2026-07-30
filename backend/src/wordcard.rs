//! Lenient parsing of the word-card JSON the model returns in json_mode
//! (mirrors `core/WordFormatter.cpp::extractJsonPayload`).

use serde::Deserialize;

/// Strips markdown fences/prose around the payload: returns the slice from
/// the first `{` to the last `}`. Without a brace pair, returns the trimmed
/// input.
pub fn extract_json_payload(raw: &str) -> String {
    match (raw.find('{'), raw.rfind('}')) {
        (Some(first), Some(last)) if last > first => raw[first..=last].to_string(),
        _ => raw.trim().to_string(),
    }
}

/// The model's structured reply: `{"type": "word", ...}` or
/// `{"type": "phrase", "translation": ...}`. All fields optional strings.
#[derive(Debug, Default, Deserialize)]
pub struct WordCard {
    #[serde(default, rename = "type")]
    pub kind: Option<String>,
    #[serde(default)]
    pub word: Option<String>,
    #[serde(default)]
    pub phonetic: Option<String>,
    #[serde(default)]
    pub pos: Option<String>,
    #[serde(default)]
    pub meaning: Option<String>,
    #[serde(default)]
    pub explanation: Option<String>,
    #[serde(default)]
    pub example: Option<String>,
    #[serde(default)]
    pub translation: Option<String>,
}

impl WordCard {
    /// Parses leniently: invalid JSON yields `None`.
    pub fn parse(payload: &str) -> Option<WordCard> {
        serde_json::from_str(payload).ok()
    }

    pub fn is_phrase(&self) -> bool {
        self.kind.as_deref() == Some("phrase")
    }

    /// Plain-text rendering: word/phonetic/pos on one line, then meaning,
    /// explanation and example; empty fields are skipped.
    pub fn plain_text(&self) -> String {
        let mut lines: Vec<&str> = Vec::new();
        let header = [&self.word, &self.phonetic, &self.pos]
            .into_iter()
            .filter_map(|field| field.as_deref())
            .filter(|field| !field.is_empty())
            .collect::<Vec<_>>()
            .join(" ");
        if !header.is_empty() {
            lines.push(header.as_str());
        }
        for field in [&self.meaning, &self.explanation, &self.example] {
            if let Some(text) = field.as_deref().filter(|text| !text.is_empty()) {
                lines.push(text);
            }
        }
        lines.join("\n")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extract_plain_object() {
        assert_eq!(
            extract_json_payload(r#"{"type":"word"}"#),
            r#"{"type":"word"}"#
        );
    }

    #[test]
    fn extract_code_fence() {
        assert_eq!(
            extract_json_payload("```json\n{\"type\":\"word\"}\n```"),
            r#"{"type":"word"}"#
        );
    }

    #[test]
    fn extract_prose_around() {
        assert_eq!(
            extract_json_payload(r#"Here is the card: {"type":"word"} hope it helps"#),
            r#"{"type":"word"}"#
        );
    }

    #[test]
    fn extract_nested_braces_kept() {
        assert_eq!(extract_json_payload(r#"{"a":{"b":1}}"#), r#"{"a":{"b":1}}"#);
    }

    #[test]
    fn extract_no_braces_returns_trimmed() {
        assert_eq!(extract_json_payload("  no json here  "), "no json here");
    }

    #[test]
    fn plain_text_full_card() {
        let card = WordCard::parse(
            r#"{
                "type": "word", "word": "bank", "phonetic": "/bæŋk/", "pos": "n.",
                "meaning": "the land next to a river",
                "explanation": "Bank here means the side of a river.",
                "example": "We sat on the bank."
            }"#,
        )
        .unwrap();
        assert!(!card.is_phrase());
        assert_eq!(
            card.plain_text(),
            "bank /bæŋk/ n.\nthe land next to a river\nBank here means the side of a river.\nWe sat on the bank."
        );
    }

    #[test]
    fn plain_text_skips_missing_fields() {
        let card = WordCard::parse(r#"{"word": "bank"}"#).unwrap();
        assert_eq!(card.plain_text(), "bank");
    }

    #[test]
    fn plain_text_empty_card_is_empty() {
        let card = WordCard::parse("{}").unwrap();
        assert_eq!(card.plain_text(), "");
    }

    #[test]
    fn phrase_card() {
        let card = WordCard::parse(r#"{"type": "phrase", "translation": "你好"}"#).unwrap();
        assert!(card.is_phrase());
        assert_eq!(card.translation.as_deref(), Some("你好"));
    }

    #[test]
    fn invalid_json_parses_to_none() {
        assert!(WordCard::parse("not json").is_none());
    }
}
