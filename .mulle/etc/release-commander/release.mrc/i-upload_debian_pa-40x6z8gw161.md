> **SUPERSEDED (22.1.8.8+):** macOS bottle building/upload is now automated by
> `.github/workflows/bottle.yml` (task *"Build macOS bottles via GitHub Actions"*).
> This task stays `skipped`; do NOT run it.

Upload debian/rpm packages to release

We assume the packages are still in the VM.
So we download them first from the VM and then upload them with gh
