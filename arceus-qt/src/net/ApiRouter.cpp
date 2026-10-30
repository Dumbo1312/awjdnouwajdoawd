#include "ApiRouter.h"
#include "OpenAICompatClient.h"
#include "AnthropicClient.h"
#include "GeminiClient.h"
#include "HuggingFaceClient.h"

namespace arceus {

ApiClient *ApiRouter::make(const Provider &p, QObject *parent) {
    switch (p.kind) {
    case ProviderKind::Anthropic:    return new AnthropicClient(parent);
    case ProviderKind::Gemini:       return new GeminiClient(parent);
    case ProviderKind::HuggingFace:  return new HuggingFaceClient(parent);
    case ProviderKind::OpenAICompat:
    default:                         return new OpenAICompatClient(parent);
    }
}

}
