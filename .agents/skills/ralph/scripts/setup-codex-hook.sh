#!/usr/bin/env bash
# ralph - Codex CLI setup helper
# Adds ralph loop guidance to:
#  1) developer_instructions in ~/.codex/config.toml
#  2) ~/.codex/prompts/ralph.md
#
# Usage: ./setup-codex-hook.sh [--dry-run]

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
GRAY='\033[0;37m'
NC='\033[0m'

DRY_RUN=false
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=true ;;
    -h|--help)
      echo "Usage: $0 [--dry-run]"
      echo ""
      echo "Configures Codex for ralph loop workflows:"
      echo "  1. Adds ralph instructions to ~/.codex/config.toml developer_instructions"
      echo "  2. Creates ~/.codex/prompts/ralph.md for /prompts:ralph"
      echo ""
      echo "Options:"
      echo "  --dry-run  Show what would change without writing"
      echo "  -h, --help Show this help"
      exit 0
      ;;
    *)
      ;;
  esac
done

CODEX_DIR="$HOME/.codex"
CODEX_CONFIG="$CODEX_DIR/config.toml"
CODEX_PROMPTS="$CODEX_DIR/prompts"
RALPH_PROMPT="$CODEX_PROMPTS/ralph.md"

echo ""
echo -e "${BLUE}╔════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  ralph × Codex setup                     ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════╝${NC}"
echo ""

if ! command -v codex &>/dev/null; then
  echo -e "${YELLOW}⚠ codex CLI not found in PATH${NC}"
  echo -e "${GRAY}  Install via: npm install -g @openai/codex${NC}"
  echo -e "${GRAY}  Continuing setup anyway (config and prompt will be prepared).${NC}"
  echo ""
fi

mkdir -p "$CODEX_DIR" "$CODEX_PROMPTS"

RALPH_INSTRUCTION='When the user task starts with /ralph, enforce completion-loop behavior:
1) Treat `/ralph "<task>" [--completion-promise=TEXT] [--max-iterations=N]` as a contract command.
2) Keep original task text unchanged across retries.
3) Parse completion signal inside XML: <promise>VALUE</promise>.
4) If promise is missing and current iteration < max-iterations, immediately continue with the same original command.
5) If promise is present or max-iterations reached, finish with a clear status report.

Default completion promise is DONE. Default max iterations is 5. Use /ralph:cancel to stop.

If a user asks for Codex-specific operation, call /prompts:ralph before risky tool use and follow the loop contract there.'

echo -e "${BLUE}Step 1: Config.toml developer_instructions${NC}"
if [ -f "$CODEX_CONFIG" ] && grep -q "ralph" "$CODEX_CONFIG" 2>/dev/null; then
  echo -e "${YELLOW}⚠ developer_instructions already contains ralph reference${NC}"
  echo -e "${GRAY}  No changes made to config.toml.${NC}"
else
  if [ "$DRY_RUN" = true ]; then
    echo -e "${YELLOW}[DRY RUN] Would add to developer_instructions in ${CODEX_CONFIG}${NC}"
    echo "  ${RALPH_INSTRUCTION}"
  else
    if [ -f "$CODEX_CONFIG" ] && grep -q "^developer_instructions" "$CODEX_CONFIG"; then
      if command -v python3 &>/dev/null; then
        python3 - "$CODEX_CONFIG" "$RALPH_INSTRUCTION" <<'PYEOF'
import sys
import re

path, addition = sys.argv[1], sys.argv[2]

def escape_toml_value(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')

with open(path) as f:
    content = f.read()

pattern = re.compile(r'^(developer_instructions\s*=\s*")(.+?)(")', re.MULTILINE | re.DOTALL)
match = pattern.search(content)

if match:
    current = match.group(2)
    if "ralph" not in current:
        new_val = current + " " + addition
        content = content[:match.start()] + f'developer_instructions = "{escape_toml_value(new_val)}"' + content[match.end():]
        with open(path, "w") as out:
            out.write(content)
        print("Updated existing developer_instructions.")
    else:
        print("developer_instructions already includes ralph keyword.")
else:
    with open(path, "a") as out:
        out.write('\ndeveloper_instructions = "{}"\n'.format(escape_toml_value(addition)))
    print("Added developer_instructions line.")
PYEOF
      else
        echo -e "${YELLOW}⚠ python3 not found. appending simple developer_instructions.${NC}"
        printf '\ndeveloper_instructions = "%s"\n' "$RALPH_INSTRUCTION" >> "$CODEX_CONFIG"
      fi
    else
      echo "developer_instructions = \"$RALPH_INSTRUCTION\"" > "$CODEX_CONFIG"
    fi
    echo -e "${GREEN}✓ Updated ${CODEX_CONFIG}${NC}"
  fi
fi

echo ""
echo -e "${BLUE}Step 2: ralph prompt file${NC}"
PROMPT_CONTENT='# ralph - Completion Loop Helper

Use this prompt to run Codex-friendly Ralph loops when automatic hooks are unavailable.

## 기본 규칙

1. Preserve task content from the original `/ralph` command.
2. Use the following format for loop exit:

```xml
<promise>DONE</promise>
```

3. Parse flags and keep running until:
   - completion promise appears, or
   - max iterations is reached.

4. Report clear status each retry so the loop is auditable.

## 실행 예시

```text
/prompts:ralph

Use /ralph "Refactor auth module with tests"
with completion promise=QA_OK
max-iterations=8
```

## 빠른 규칙

- Promise default: DONE
- Max iterations default: 5
- 사용: `/ralph:cancel` to stop the active loop
- If ambiguity remains, ask for explicit promise text before continuing.
'

if [ -f "$RALPH_PROMPT" ]; then
  echo -e "${YELLOW}⚠ ${RALPH_PROMPT} already exists${NC}"
  echo -e "${GRAY}  Skipping file creation.${NC}"
elif [ "$DRY_RUN" = true ]; then
  echo -e "${YELLOW}[DRY RUN] Would create ${RALPH_PROMPT}${NC}"
else
  printf '%s\n' "$PROMPT_CONTENT" > "$RALPH_PROMPT"
  echo -e "${GREEN}✓ Created ${RALPH_PROMPT}${NC}"
fi

echo ""
echo -e "${GREEN}ralph Codex setup complete.${NC}"
echo ""
echo -e "${BLUE}Next steps:${NC}"
echo -e "  1. Restart Codex session"
echo -e "  2. In Codex, run: ${GREEN}/prompts:ralph${NC}"
echo -e "  3. Start loop with: ${GREEN}/ralph \"<task>\" --completion-promise=\"DONE\" --max-iterations=10${NC}"
echo ""
