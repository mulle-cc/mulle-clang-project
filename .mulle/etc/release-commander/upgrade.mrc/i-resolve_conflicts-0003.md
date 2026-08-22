Resolve cherry-pick conflicts and verify all `@mulle-` markers survived.

## When you land here

The `migrate-to-next-release` script stopped after `### 9: Cherry pick` because
of conflicts. The working tree is in a conflicted cherry-pick state on branch
`NEW_MULLE_DEV_BRANCH`.

## Steps

1. Check what's conflicted:
   ```bash
   git status -s
   ```

2. For each conflicted file, open it and resolve. Our changes are always
   marked with `// @mulle-objc@ <tag> >->` … `// @mulle-objc@ <tag> <-<`
   blocks — keep those intact.

3. After resolving all files:
   ```bash
   git add clang/include/ clang/lib/
   git cherry-pick --continue
   ```

4. Run the cleanup/verify step. Use `-f` if paths changed:
   ```bash
   bash /tmp/migrate-to-next-release -f continue
   ```
   This diffs `.before-markers.txt` vs `.after-markers.txt`. Any missing
   `@mulle-` tag is printed — fix and re-run until clean.

## Verify markers (alternate check)

If the `.before-markers.txt` file was lost (e.g. on a tmp branch that was
deleted), do a raw count:
```bash
grep -R '@mulle-' clang/include/ clang/lib/ | wc -l
```

## Same-major patch bumps

Patch bumps within the same major (e.g. 22.1.2 → 22.1.8) typically produce
**zero conflicts**. The cherry-pick auto-merges cleanly because there are no
structural changes between minor LLVM releases.

## Historical: conflicts from llvm 21 → 22

These notes are kept as reference for the kinds of problems to expect during
future major version jumps.

### Cherry-pick conflicts

- **ASTConsumer.h**: HEAD added `OpenACCRoutineDecl` forward decl. Keep HEAD + add our `Parser` forward decl after it.
- **Type.h**: llvm 22 split `Type.h` into `TypeBase.h` + thin `Type.h`. Take HEAD for both conflicts (mulle changes go into `TypeBase.h` instead).
- **Type.cpp**: `isArithmeticType` — keep HEAD's range but add our `ObjCSel`/`ObjCProtocol` conditions.
- **CGException.cpp**: HEAD added `CGOpts` param to `getObjCPersonality`. Keep HEAD's call + our `Mulle:` case.
- **CGObjCRuntime.h**: Keep our `isSuper` param addition to `getMessageSendInfo`.
- **ModuleBuilder.cpp**: HEAD changed `return new` to `return std::make_unique` + added `DemangleTrapReason`. Take HEAD (no mulle changes here).
- **SemaDecl.cpp**: HEAD added `isRedefinitionAllowedFor`. Take HEAD (no mulle changes here).
- **SemaExpr.cpp**: Keep our `GetMulle_paramExpr` functions, but update `PerformObjectMemberConversion` signature to HEAD's value type (`NestedNameSpecifier` not pointer).

### TypeBase.h migration (Type.h split)

After resolving conflicts, manually apply mulle changes from old `Type.h` to new `TypeBase.h`:

1. `isSignedInteger` / `isUnsignedInteger` in `BuiltinType` — add `ObjCSel`/`ObjCProtocol` to return
2. `isObjCSelType` inline — add SEL builtin hack before pointer check
3. `isObjCBuiltinType` — add `|| isObjCProtocolType()`
4. `isIntegralType`, `isScalarType`, `isIntegralOrEnumerationType` — add `ObjCSel`/`ObjCProtocol`
5. Add `isObjCProtocolType()` declaration in `Type` class (after `isObjCSelType`)
6. Add `isObjCProtocolType()` inline definition (after `isObjCClassType` inline)

### LLVM 22 API changes that caused build errors

- `getTagDeclType` renamed to `getTypeDeclType` — affects `CGCall.cpp`, `CGObjC.cpp`, `CGObjCMulleRuntime.cpp`
- `getTypeDeclType(TypedefDecl*)` deleted — cast to `(TypeDecl*)` instead — affects `ASTContext.h`
- `LangOptions::Optimize` / `OptimizeSize` removed — affects `CGDebugInfo.cpp`, `CGObjCMulleRuntime.cpp`
- `EmitLifetimeEnd`/`EmitLifetimeStart` signature changed — affects `CGObjC.cpp`, `CGObjCMulleRuntime.cpp`
- `PredefinedTypeIDs` enum overflow in `ASTBitCodes.h` — our `ObjCProtocol` type ID needs bumping