# Update README.md version info

The README.md contains a line referencing the runtime version. Update it:

```
It corresponds to mulle-objc-runtime vX.YY or better (load version N).
```

| Field | Source |
|---|---|
| `vX.YY` | `MULLE_RELEASE_TAG` from parent .mrc (e.g. v0.29) |
| `load version N` | `COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION` from `clang/include/clang/Basic/ObjCRuntime.h` |

## Procedure

1. Read `COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION` from `clang/include/clang/Basic/ObjCRuntime.h:28`
2. Update the line in README.md
3. Commit and push to `mulle` and `origin`