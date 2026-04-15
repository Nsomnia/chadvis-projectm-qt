# Nvidia Provider Models Reference

> **Generated:** 2026-03-31
> **Source:** `opencode models --refresh | grep nvidia`
> **Purpose:** Quick reference for sub-agent model selection

---

## Nvidia API Provider Models

### Vision-Capable Models ⭐
These models support image input for visual analysis tasks:

| Model ID | Notes |
|----------|-------|
| `nvidia/meta/llama-3.2-11b-vision-instruct` | Llama 3.2 with vision |
| `nvidia/microsoft/phi-3-vision-128k-instruct` | Phi-3 Vision (128K context) |
| `nvidia/microsoft/phi-3.5-vision-instruct` | Phi-3.5 Vision |

### DeepSeek Models
| Model ID | Notes |
|----------|-------|
| `nvidia/deepseek-ai/deepseek-coder-6.7b-instruct` | Code-specialized |
| `nvidia/deepseek-ai/deepseek-r1` | Reasoning model |
| `nvidia/deepseek-ai/deepseek-r1-0528` | R1 variant |
| `nvidia/deepseek-ai/deepseek-v3.1` | General purpose |
| `nvidia/deepseek-ai/deepseek-v3.1-terminus` | V3.1 variant |
| `nvidia/deepseek-ai/deepseek-v3.2` | Latest V3 |

### Google/Gemma Models
| Model ID | Notes |
|----------|-------|
| `nvidia/google/codegemma-1.1-7b` | Code-specialized |
| `nvidia/google/codegemma-7b` | Code model |
| `nvidia/google/gemma-2-27b-it` | Large instruct |
| `nvidia/google/gemma-2-2b-it` | Compact instruct |
| `nvidia/google/gemma-3-12b-it` | Gemma 3 medium |
| `nvidia/google/gemma-3-1b-it` | Gemma 3 compact |
| `nvidia/google/gemma-3-27b-it` | Gemma 3 large |
| `nvidia/google/gemma-3n-e2b-it` | Gemma 3n efficient |
| `nvidia/google/gemma-3n-e4b-it` | Gemma 3n efficient |

### Meta/Llama Models
| Model ID | Notes |
|----------|-------|
| `nvidia/meta/codellama-70b` | Code-specialized |
| `nvidia/meta/llama-3.1-405b-instruct` | Massive model |
| `nvidia/meta/llama-3.1-70b-instruct` | Large instruct |
| `nvidia/meta/llama-3.2-1b-instruct` | Compact |
| `nvidia/meta/llama-3.3-70b-instruct` | Latest 70B |
| `nvidia/meta/llama-4-maverick-17b-128e-instruct` | Llama 4 Maverick |
| `nvidia/meta/llama-4-scout-17b-16e-instruct` | Llama 4 Scout |
| `nvidia/meta/llama3-70b-instruct` | Llama 3 70B |
| `nvidia/meta/llama3-8b-instruct` | Llama 3 8B |

### Microsoft/Phi Models
| Model ID | Notes |
|----------|-------|
| `nvidia/microsoft/phi-3-medium-128k-instruct` | Medium, long context |
| `nvidia/microsoft/phi-3-medium-4k-instruct` | Medium, standard context |
| `nvidia/microsoft/phi-3-small-128k-instruct` | Small, long context |
| `nvidia/microsoft/phi-3-small-8k-instruct` | Small, standard context |
| `nvidia/microsoft/phi-3.5-moe-instruct` | Mixture of Experts |
| `nvidia/microsoft/phi-4-mini-instruct` | Phi-4 compact |

### Mistral Models
| Model ID | Notes |
|----------|-------|
| `nvidia/mistralai/codestral-22b-instruct-v0.1` | Code-specialized |
| `nvidia/mistralai/devstral-2-123b-instruct-2512` | Dev-focused |
| `nvidia/mistralai/mamba-codestral-7b-v0.1` | Mamba architecture |
| `nvidia/mistralai/ministral-14b-instruct-2512` | Compact Mistral |
| `nvidia/mistralai/mistral-large-2-instruct` | Large v2 |
| `nvidia/mistralai/mistral-large-3-675b-instruct-2512` | Massive v3 |
| `nvidia/mistralai/mistral-small-3.1-24b-instruct-2503` | Small v3.1 |

### MiniMax Models
| Model ID | Notes |
|----------|-------|
| `nvidia/minimaxai/minimax-m2.1` | M2.1 |
| `nvidia/minimaxai/minimax-m2.5` | M2.5 |

### Moonshot/Kimi Models
| Model ID | Notes |
|----------|-------|
| `nvidia/moonshotai/kimi-k2-instruct` | Kimi K2 |
| `nvidia/moonshotai/kimi-k2-instruct-0905` | K2 variant |
| `nvidia/moonshotai/kimi-k2-thinking` | Reasoning |
| `nvidia/moonshotai/kimi-k2.5` | Latest K2 |

### Nvidia Nemotron Models (Nvidia's Own)
| Model ID | Notes |
|----------|-------|
| `nvidia/nvidia/cosmos-nemotron-34b` | Cosmos variant |
| `nvidia/nvidia/llama-3.1-nemotron-51b-instruct` | Nemotron on Llama |
| `nvidia/nvidia/llama-3.1-nemotron-70b-instruct` | 70B Nemotron |
| `nvidia/nvidia/llama-3.1-nemotron-ultra-253b-v1` | Massive Nemotron |
| `nvidia/nvidia/llama-3.3-nemotron-super-49b-v1` | Super 49B |
| `nvidia/nvidia/llama-3.3-nemotron-super-49b-v1.5` | Super 49B v1.5 |
| `nvidia/nvidia/llama-embed-nemotron-8b` | Embedding model |
| `nvidia/nvidia/llama3-chatqa-1.5-70b` | ChatQA |
| `nvidia/nvidia/nemoretriever-ocr-v1` | OCR-specialized |
| `nvidia/nvidia/nemotron-3-nano-30b-a3b` | Nano variant |
| `nvidia/nvidia/nemotron-3-super-120b-a12b` | Super 120B |
| `nvidia/nvidia/nemotron-4-340b-instruct` | Nemotron 4 |
| `nvidia/nvidia/nvidia-nemotron-nano-9b-v2` | Nano v2 |
| `nvidia/nvidia/parakeet-tdt-0.6b-v2` | Audio/speech |

### Qwen Models
| Model ID | Notes |
|----------|-------|
| `nvidia/qwen/qwen2.5-coder-32b-instruct` | Code model |
| `nvidia/qwen/qwen2.5-coder-7b-instruct` | Compact code |
| `nvidia/qwen/qwen3-235b-a22b` | Large Qwen 3 |
| `nvidia/qwen/qwen3-coder-480b-a35b-instruct` | Massive coder |
| `nvidia/qwen/qwen3-next-80b-a3b-instruct` | Next 80B |
| `nvidia/qwen/qwen3-next-80b-a3b-thinking` | Reasoning |
| `nvidia/qwen/qwen3.5-397b-a17b` | Qwen 3.5 |
| `nvidia/qwen/qwq-32b` | Reasoning model |

### Other Models
| Model ID | Notes |
|----------|-------|
| `nvidia/black-forest-labs/flux.1-dev` | Image generation |
| `nvidia/openai/gpt-oss-120b` | Open-source GPT |
| `nvidia/openai/whisper-large-v3` | Speech-to-text |
| `nvidia/stepfun-ai/step-3.5-flash` | StepFun flash |

### Z-AI GLM Models (Current Default)
| Model ID | Notes |
|----------|-------|
| `nvidia/z-ai/glm4.7` | GLM 4.7 |
| `nvidia/z-ai/glm5` | **Current default** - No vision support |

---

## OpenRouter Nvidia Models (Free Tier Available)

| Model ID | Free Tier |
|----------|-----------|
| `openrouter/nvidia/nemotron-3-nano-30b-a3b` | ✅ |
| `openrouter/nvidia/nemotron-3-super-120b-a12b` | ✅ |
| `openrouter/nvidia/nemotron-nano-12b-v2-vl` | ✅ (Vision!) |
| `openrouter/nvidia/nemotron-nano-9b-v2` | ✅ |

---

## Recommendations

### For Vision Tasks (Mockups, UI Review)
```
nvidia/meta/llama-3.2-11b-vision-instruct
nvidia/microsoft/phi-3-vision-128k-instruct
nvidia/microsoft/phi-3.5-vision-instruct
openrouter/nvidia/nemotron-nano-12b-v2-vl  # Free option
```

### For Code Tasks
```
nvidia/deepseek-ai/deepseek-coder-6.7b-instruct
nvidia/qwen/qwen3-coder-480b-a35b-instruct
nvidia/meta/codellama-70b
```

### For General Reasoning
```
nvidia/deepseek-ai/deepseek-r1
nvidia/qwen/qwq-32b
```

### For Audio/Transcription
```
nvidia/openai/whisper-large-v3
nvidia/nvidia/parakeet-tdt-0.6b-v2
```

---

> **Note:** Model availability may change. Run `opencode models --refresh | grep nvidia` for latest list.
