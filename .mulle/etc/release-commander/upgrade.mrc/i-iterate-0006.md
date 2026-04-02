Check if another iteration is needed to reach the ultimate target major version.

## Steps

1. Re-run step 1 (`s-determine_target-0001.sh`) from the now-updated repo.
   If it prints `already at latest major` → done.

2. If a newer major is still available, repeat steps 1–5 for the next
   major version (current+1 again from the new baseline).

## Example: upgrading 21 → 23

- Iteration 1: 21 → 22 (steps 1–5)
- Iteration 2: 22 → 23 (steps 1–5 again)
- Step 6 check: no more majors → done
