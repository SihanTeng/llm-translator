//! Provider registry, parsed from the embedded `spec/providers.json`.

use serde::Deserialize;
use std::sync::OnceLock;

const PROVIDERS_JSON: &str = include_str!("../../spec/providers.json");

/// Wire protocol the provider speaks.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
pub enum ApiStyle {
    #[serde(rename = "openai-compatible")]
    OpenAiCompatible,
    #[serde(rename = "anthropic")]
    Anthropic,
}

/// How structured (word-card) output is requested from the provider.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
pub enum JsonMode {
    /// Send `response_format: {"type": "json_object"}` (non-streamed).
    #[serde(rename = "response_format")]
    ResponseFormat,
    /// Rely on the prompt only; stream and buffer the deltas.
    #[serde(rename = "prompt_only")]
    PromptOnly,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProviderInfo {
    pub id: String,
    pub name: String,
    pub style: ApiStyle,
    pub json_mode: JsonMode,
    pub base_url: String,
    pub default_model: String,
    #[serde(default)]
    pub alt_model: String,
    #[serde(default)]
    pub env_var: String,
    #[serde(default)]
    pub key_page: String,
    #[serde(default)]
    pub disable_thinking: bool,
}

#[derive(Deserialize)]
struct ProvidersFile {
    providers: Vec<ProviderInfo>,
}

/// All providers from the embedded registry.
pub fn providers() -> &'static [ProviderInfo] {
    static PROVIDERS: OnceLock<Vec<ProviderInfo>> = OnceLock::new();
    PROVIDERS.get_or_init(|| {
        serde_json::from_str::<ProvidersFile>(PROVIDERS_JSON)
            .expect("spec/providers.json must parse")
            .providers
    })
}

/// Looks up a provider by its stable id (`deepseek`, `openai`, ...).
pub fn provider_by_id(id: &str) -> Option<&'static ProviderInfo> {
    providers().iter().find(|provider| provider.id == id)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn has_at_least_eight_providers_with_unique_ids() {
        assert!(providers().len() >= 8);
        let mut ids: Vec<&str> = providers().iter().map(|p| p.id.as_str()).collect();
        ids.sort_unstable();
        ids.dedup();
        assert_eq!(ids.len(), providers().len(), "provider ids must be unique");
    }

    #[test]
    fn required_providers_are_present() {
        for id in ["deepseek", "openai", "anthropic", "custom"] {
            assert!(provider_by_id(id).is_some(), "missing provider {id}");
        }
    }

    #[test]
    fn non_custom_base_urls_are_https() {
        for provider in providers() {
            if provider.id == "custom" {
                continue;
            }
            assert!(
                provider.base_url.starts_with("https://"),
                "provider {} has a non-https base URL: {}",
                provider.id,
                provider.base_url
            );
        }
    }

    #[test]
    fn anthropic_style_implies_prompt_only() {
        for provider in providers() {
            if provider.style == ApiStyle::Anthropic {
                assert_eq!(provider.json_mode, JsonMode::PromptOnly, "{}", provider.id);
            }
        }
    }

    #[test]
    fn deepseek_disables_thinking() {
        let deepseek = provider_by_id("deepseek").unwrap();
        assert!(deepseek.disable_thinking);
        assert_eq!(deepseek.style, ApiStyle::OpenAiCompatible);
        assert_eq!(deepseek.json_mode, JsonMode::ResponseFormat);
    }

    #[test]
    fn unknown_id_returns_none() {
        assert!(provider_by_id("bogus").is_none());
        assert!(provider_by_id("").is_none());
    }
}
