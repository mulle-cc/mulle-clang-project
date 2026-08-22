Check if another major version iteration is needed.

## Steps

1. Re-run step 1 (`s-determine_target-0001.sh`) from the now-updated repo.

2. If it produces no output (no tags for current+1 major) → **done**.

3. If a newer major is available, repeat steps 1–5 for the next
   major version (current+1 again from the new baseline).

4. Same-major patch bumps (e.g. 22.1.2 → 22.1.8) do not need iteration —
   step 1 always looks for current+1 major, not same-major patches.

## Example: upgrading 21 → 23

- Iteration 1: 21 → 22 (steps 1–5)
- Iteration 2: 22 → 23 (steps 1–5 again)
- Step 6 check: no more majors → done