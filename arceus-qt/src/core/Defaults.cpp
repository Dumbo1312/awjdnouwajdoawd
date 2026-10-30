#include "Defaults.h"
#include "Settings.h"
#include <QUuid>

namespace arceus::Defaults {

static Provider mkOAI(const char *id, const char *name, const char *url,
                     bool images, const QStringList &models)
{
    Provider p;
    p.id = id;
    p.name = name;
    p.kind = ProviderKind::OpenAICompat;
    p.baseUrl = url;
    p.supportsImages = images;
    p.builtin = true;
    p.models = models;
    return p;
}

QVector<Provider> providers()
{
    QVector<Provider> out;

    out.append(mkOAI("openai", "OpenAI", "https://api.openai.com/v1", true, {
        "gpt-4o", "gpt-4o-mini", "gpt-4.1", "gpt-4.1-mini", "o3", "o4-mini"
    }));
    out.append(mkOAI("groq", "Groq (free)", "https://api.groq.com/openai/v1", false, {
        "llama-3.3-70b-versatile", "llama-3.1-8b-instant",
        "qwen-2.5-coder-32b", "deepseek-r1-distill-llama-70b", "mixtral-8x7b-32768"
    }));
    out.append(mkOAI("openrouter", "OpenRouter", "https://openrouter.ai/api/v1", true, {
        "openrouter/auto",
        "anthropic/claude-3.7-sonnet",
        "openai/gpt-4o",
        "google/gemini-2.0-flash-001",
        "deepseek/deepseek-chat",
        "meta-llama/llama-3.3-70b-instruct:free",
        "qwen/qwen-2.5-coder-32b-instruct:free"
    }));
    out.append(mkOAI("together", "Together.ai", "https://api.together.xyz/v1", false, {
        "meta-llama/Llama-3.3-70B-Instruct-Turbo",
        "Qwen/Qwen2.5-Coder-32B-Instruct",
        "deepseek-ai/DeepSeek-V3",
        "mistralai/Mixtral-8x7B-Instruct-v0.1"
    }));
    out.append(mkOAI("deepseek", "DeepSeek", "https://api.deepseek.com/v1", false, {
        "deepseek-chat", "deepseek-reasoner", "deepseek-coder"
    }));
    out.append(mkOAI("mistral", "Mistral", "https://api.mistral.ai/v1", false, {
        "mistral-large-latest", "mistral-small-latest",
        "codestral-latest", "open-mixtral-8x22b"
    }));
    out.append(mkOAI("fireworks", "Fireworks", "https://api.fireworks.ai/inference/v1", false, {
        "accounts/fireworks/models/llama-v3p3-70b-instruct",
        "accounts/fireworks/models/deepseek-v3",
        "accounts/fireworks/models/qwen2p5-coder-32b-instruct"
    }));
    out.append(mkOAI("perplexity", "Perplexity (Sonar)", "https://api.perplexity.ai", false, {
        "sonar", "sonar-pro", "sonar-reasoning", "sonar-reasoning-pro"
    }));
    out.append(mkOAI("xai", "xAI (Grok)", "https://api.x.ai/v1", false, {
        "grok-2-latest", "grok-2-mini-latest", "grok-beta"
    }));
    out.append(mkOAI("cerebras", "Cerebras", "https://api.cerebras.ai/v1", false, {
        "llama-3.3-70b", "llama-3.1-8b", "qwen-3-32b"
    }));
    out.append(mkOAI("sambanova", "SambaNova", "https://api.sambanova.ai/v1", false, {
        "Meta-Llama-3.3-70B-Instruct",
        "Qwen2.5-Coder-32B-Instruct",
        "DeepSeek-R1"
    }));
    out.append(mkOAI("github_models", "GitHub Models", "https://models.inference.ai.azure.com", true, {
        "gpt-4o", "gpt-4o-mini", "Phi-3.5-MoE-instruct", "Llama-3.3-70B-Instruct"
    }));
    out.append(mkOAI("nvidia", "NVIDIA NIM", "https://integrate.api.nvidia.com/v1", false, {
        "meta/llama-3.3-70b-instruct",
        "qwen/qwen2.5-coder-32b-instruct",
        "deepseek-ai/deepseek-r1"
    }));
    out.append(mkOAI("ai21", "AI21", "https://api.ai21.com/studio/v1", false, {
        "jamba-1.5-large", "jamba-1.5-mini"
    }));
    out.append(mkOAI("cohere_compat", "Cohere (compat)", "https://api.cohere.com/compatibility/v1", false, {
        "command-r-plus-08-2024", "command-r-08-2024"
    }));
    out.append(mkOAI("ollama", "Ollama (local)", "http://localhost:11434/v1", false, {
        "qwen2.5-coder:14b", "qwen2.5-coder:7b",
        "llama3.3:70b", "llama3.1:8b", "deepseek-r1:14b", "deepseek-coder-v2:16b"
    }));
    out.append(mkOAI("lmstudio", "LM Studio (local)", "http://localhost:1234/v1", false, {
        "local-model"
    }));
    out.append(mkOAI("llamacpp", "llama.cpp (local)", "http://localhost:8080/v1", false, {
        "local-model"
    }));
    out.append(mkOAI("vllm", "vLLM (local)", "http://localhost:8000/v1", false, {
        "local-model"
    }));

    {
        Provider p;
        p.id = "anthropic";
        p.name = "Anthropic (Claude)";
        p.kind = ProviderKind::Anthropic;
        p.baseUrl = "https://api.anthropic.com";
        p.supportsImages = true;
        p.builtin = true;
        p.models = { "claude-opus-4-5", "claude-sonnet-4-5", "claude-3-7-sonnet-latest",
                     "claude-3-5-sonnet-latest", "claude-3-5-haiku-latest" };
        out.append(p);
    }
    {
        Provider p;
        p.id = "gemini";
        p.name = "Google Gemini";
        p.kind = ProviderKind::Gemini;
        p.baseUrl = "https://generativelanguage.googleapis.com";
        p.supportsImages = true;
        p.builtin = true;
        p.models = { "gemini-2.5-pro", "gemini-2.5-flash", "gemini-2.0-flash",
                     "gemini-2.0-flash-exp-image-generation" };
        out.append(p);
    }
    {
        Provider p;
        p.id = "huggingface";
        p.name = "Hugging Face (text + image)";
        p.kind = ProviderKind::HuggingFace;
        p.baseUrl = "https://api-inference.huggingface.co";
        p.supportsImages = true;
        p.builtin = true;
        p.models = {
            "meta-llama/Meta-Llama-3-8B-Instruct",
            "Qwen/Qwen2.5-Coder-32B-Instruct",
            "black-forest-labs/FLUX.1-schnell",
            "black-forest-labs/FLUX.1-dev",
            "stabilityai/stable-diffusion-xl-base-1.0",
            "stabilityai/stable-diffusion-3.5-large"
        };
        out.append(p);
    }

    return out;
}

static Preset mkPreset(const Settings *s, const char *name, const char *icon,
                       const QString &prov, const QString &model,
                       bool tools, bool images, double temp, const char *sys)
{
    Preset p;
    p.id = s ? s->newId() : QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name;
    p.icon = icon;
    p.defaultProviderId = prov;
    p.defaultModel = model;
    p.enableTools = tools;
    p.enableImages = images;
    p.temperature = temp;
    p.maxTokens = 4096;
    p.systemPrompt = sys;
    return p;
}

QVector<Preset> presets(const Settings *s)
{
    QVector<Preset> out;

    out.append(mkPreset(s, "General", "chat", "groq", "llama-3.3-70b-versatile", false, false, 0.7,
        "You are Arceus, a sharp, helpful personal AI assistant. "
        "You answer concisely by default, expand only when depth is actually useful, and never pad with filler. "
        "If the user asks for an opinion, give one. If the user asks for code, give clean, working code. "
        "Admit uncertainty rather than invent facts. Prefer the simplest correct answer."));

    out.append(mkPreset(s, "Coding", "code", "groq", "qwen-2.5-coder-32b", true, false, 0.2,
        "You are Arceus-Code, an elite senior software engineer. "
        "Deliver production-quality code: idiomatic, well-structured, correct, with no dead code or stray comments. "
        "When you edit files, read first, keep style consistent, change only what the task requires, and do not fabricate APIs. "
        "Prefer the simplest change that works; never add speculative abstractions or defensive code for impossible states. "
        "After changes, state clearly what you did and how to verify it. "
        "When agentic tools are available (read_file, write_file, list_dir, run_command), use them freely to inspect the project before coding."));

    out.append(mkPreset(s, "Agentic (Claude-style)", "bolt", "openrouter", "anthropic/claude-3.7-sonnet", true, false, 0.3,
        "You are Arceus-Agent, an autonomous coding agent inspired by Claude Code. "
        "Plan briefly, then act: use tools (read_file, write_file, list_dir, run_command) in a tight loop until the task is done. "
        "Prefer a few tool calls that make real progress over long chains of guesses. "
        "Before editing a file, read it. Before running destructive commands, pause and ask unless the user set permission_mode=bypass. "
        "At the end, summarize what changed in 2-5 lines."));

    out.append(mkPreset(s, "Image Generation", "image", "huggingface", "black-forest-labs/FLUX.1-schnell", false, true, 0.9,
        "You are Arceus-Image. For every user prompt, produce an improved, highly visual image prompt "
        "(subject, composition, lighting, lens, mood, style keywords) and request the image via the image tool. "
        "Keep spoken responses to one short line; the image is the deliverable."));

    out.append(mkPreset(s, "Research", "search", "perplexity", "sonar-pro", false, false, 0.4,
        "You are Arceus-Research. For every answer: state the claim, then back it with sources. "
        "Distinguish well-established facts from recent, uncertain, or contested ones. "
        "If search tools or citations are unavailable, say so and answer from training with that caveat."));

    out.append(mkPreset(s, "Brainstorm", "spark", "groq", "llama-3.3-70b-versatile", false, false, 1.0,
        "You are Arceus-Brainstorm. Generate many angles fast. "
        "Numbered lists. Short lines. Mix safe and wild ideas. Never apologize for being creative."));

    return out;
}

}
