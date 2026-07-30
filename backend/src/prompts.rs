//! System-prompt templates from the embedded `spec/prompts.json`.

use serde::Deserialize;
use std::sync::OnceLock;

const PROMPTS_JSON: &str = include_str!("../../spec/prompts.json");

#[derive(Deserialize)]
struct PromptTemplates {
    phrase: String,
    word: String,
}

#[derive(Deserialize)]
struct PromptsFile {
    prompts: PromptTemplates,
}

fn templates() -> &'static PromptTemplates {
    static TEMPLATES: OnceLock<PromptTemplates> = OnceLock::new();
    TEMPLATES.get_or_init(|| {
        serde_json::from_str::<PromptsFile>(PROMPTS_JSON)
            .expect("spec/prompts.json must parse")
            .prompts
    })
}

/// Plain-translation system prompt with `{target}` replaced by the English
/// target-language name.
pub fn phrase(target: &str) -> String {
    templates().phrase.replace("{target}", target)
}

/// Word-card (dictionary) system prompt with `{target}` replaced.
pub fn word(target: &str) -> String {
    templates().word.replace("{target}", target)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn target_is_replaced_in_both_templates() {
        let phrase = phrase("Simplified Chinese");
        assert!(phrase.contains("Simplified Chinese"));
        assert!(!phrase.contains("{target}"));

        let word = word("French");
        assert!(word.contains("French"));
        assert!(!word.contains("{target}"));
    }

    #[test]
    fn word_template_documents_both_reply_forms() {
        let word = word("German");
        assert!(word.contains(r#""type": "word""#));
        assert!(word.contains(r#""type": "phrase""#));
    }
}
