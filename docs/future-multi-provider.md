# Future work: accounts from other AI providers

Planned for this fork, not built. Today the device switches between Claude
accounts only (`config_dirs`, the Switch account button action).

## Goal

Watch more than two accounts, from any provider, and keep switching between
them with the same Switch account button. First additions: OpenAI (GPT) and
DeepSeek. The design should let a later provider plug in without touching the
others.

## Starting point

- Upstream (v2.7.1 at the time of writing) reads OpenAI Codex usage as a
  trial: a Codex home (`~/.codex`, `$CODEX_HOME`, `codex_dirs`) is an account
  beside the Claude config dirs, read from the endpoint the Codex CLI uses
  with its stored sign-in. The payload marks it `pv:"codex"` and the device
  draws a Codex screen. This covers ChatGPT plans used through the Codex CLI.
- This branch is behind upstream and doesn't have that yet. Upstream changed
  the same files as the account switch (`engine.rs`, `selector.rs`,
  `payload.rs`, `ui.cpp`), so merging it is the first and largest step.

## Plan

1. **Merge upstream.** Codex homes join this fork's account list; an account
   chosen with the button stays shown over upstream's "Claude first" rule.
2. **Provider interface (app).** One interface per provider: read its usage
   and return a standard reading, either a percentage with its window or a
   balance. Claude and Codex move behind it; a new provider implements it and
   registers its config key. `account_labels` covers accounts of every
   provider.
3. **DeepSeek.** Its API exposes only the prepaid balance
   (`GET https://api.deepseek.com/user/balance`, API key), no usage limits.
4. **Protocol.** Every `o` entry carries its provider (`pv`). With four or
   more accounts the payload passes the 500-byte write limit, so the other
   accounts go out in a write of their own when they change, instead of
   being trimmed.
5. **Firmware.** Keep up to 6 accounts (4 today) and draw each by provider:
   Claude as today, Codex with upstream's screen, DeepSeek as a balance card.
   Switch account cycles through all of them in order.
6. **Verification.** Tests per provider, simulator captures, the fork's
   Windows build, then hardware.

## Open decisions

- **GPT source:** a ChatGPT plan through the Codex CLI (what upstream
  supports; needs the CLI installed and signed in), or OpenAI API spend
  (needs an organization admin key).
- **DeepSeek on the device:** the balance as a number alone, or with a bar
  against a budget the user sets.
- **DeepSeek API key storage:** the OS credential store (recommended) or the
  plain-text config file.
- **Upstream:** if the provider interface ends up clean, propose it upstream
  as a pull request, so each new provider doesn't add to the cost of merging.

## Constraints

- Build and test the DeepSeek provider on a personal computer, not the one
  this fork is deployed on today.
