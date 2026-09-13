# Native Runner

## Step 5 — Reproduce on a REAL runner with a throwaway debug workflow

When you need native hardware, add a minimal `workflow_dispatch` job. It downloads the existing
artifact (no rebuild) and runs the failing case under a debugger, many times.

Key tricks:
- `on: workflow_dispatch` only, so it never runs automatically.
- Put `[skip ci]` in the commit message so pushing the workflow file does NOT trigger the heavy
  build matrix. Trigger it manually with `gh workflow run`.
- Match the failing runner's OS (`runs-on: ubuntu-22.04`, etc.).
- Capture core dumps and symbolize them. Loop enough to catch low-probability flakes.

```yaml
name: debug-repro
on:
  workflow_dispatch:
    inputs:
      run_id:     { description: 'run id to pull artifact from', required: false, default: '' }
      iterations: { description: 'repeat count', required: false, default: '80' }
permissions: { contents: read, actions: read }
jobs:
  repro:
    runs-on: ubuntu-22.04            # match the failing runner
    steps:
      - uses: actions/checkout@v4
        with: { path: src }
      - run: sudo apt-get update && sudo apt-get install -y gcc libc6-dev gdb p7zip-full file
      - name: Download artifact (no rebuild)
        env: { GH_TOKEN: '${{ github.token }}' }
        run: |
          set -x
          RID="${{ github.event.inputs.run_id }}"
          [ -z "$RID" ] && RID=$(gh run list --workflow <build>.yml -L 30 \
              --json databaseId,status -q '[.[]|select(.status=="completed")][0].databaseId' \
              -R "${{ github.repository }}")
          gh run download "$RID" -n <artifact> -D dl -R "${{ github.repository }}"
          7z x dl/*.zip -o. >/dev/null; chmod +x <tool-path>
      - name: Reproduce + determinism + backtrace
        run: |
          set +e
          N="${{ github.event.inputs.iterations }}"
          ulimit -c unlimited
          echo "$PWD/core.%e.%p" | sudo tee /proc/sys/kernel/core_pattern   # overrides apport
          echo "nproc=$(nproc)"
          declare -A H; fails=0
          for i in $(seq 1 "$N"); do
            rm -f /tmp/out core.*
            <tool> <EXACT-CI-FLAGS> -o /tmp/out <src> 2>/tmp/cerr
            if [ $? -ne 0 ]; then echo "[#$i] TOOL crash"; head -20 /tmp/cerr
              for c in core.*; do gdb -batch -ex 'thread apply all bt' <tool> "$c" 2>&1|head -60; done
              fails=$((fails+1)); continue; fi
            H["$(sha256sum /tmp/out|cut -c1-16)"]=1     # NOTE: omit -g so hashes are meaningful
            /tmp/out >/dev/null 2>&1
            if [ $? -ne 0 ]; then echo "[#$i] PROGRAM crash"
              for c in core.*; do
                gdb -batch -ex bt -ex 'info registers rip rsp' -ex 'x/i $rip' /tmp/out "$c" 2>&1|head -50
              done; fails=$((fails+1)); fi
          done
          echo "distinct binary hashes=${#H[@]}  fails=$fails"
          [ ${#H[@]} -gt 1 ] && echo "*** NON-DETERMINISTIC OUTPUT ***"
```

Drive it and read results:
```bash
git add .github/workflows/debug-repro.yml
git commit -m "ci: on-demand repro [skip ci]" && git push
gh workflow run debug-repro.yml --ref <branch> -f iterations=80
# wait, then:
gh run view <id> --log | grep -F "Reproduce + determinism + backtrace" | sed -E 's/^[^\t]*\t[^\t]*\t//'
```

Tips: size `iterations` to the observed failure rate (2 of 560 tests ≈ 0.4% → dozens–hundreds of
iterations to catch one). Add a parallel-stress variant (N background workers sharing any on-disk
cache) to surface concurrency/cache races.
