//===-- RewriteMulleObjC.cpp - ObjC-to-C rewriter for mulle-objc ----------===//
//
// Rewrites Objective-C source to equivalent C code targeting the mulle-objc
// runtime. Activated via -rewrite-mulle-objc.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include <algorithm>
#include "clang/Basic/TokenKinds.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Rewrite/Frontend/ASTConsumers.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <set>
#include <vector>

using namespace clang;
using llvm::RewriteBuffer;

// Defined in ExprConstant.cpp — shared with CGObjCMulleRuntime
extern "C" uint32_t MulleObjCUniqueIdHashForString(std::string s);
extern "C" int      MulleObjCChar5StringIs64Bit(char *src, size_t len);
extern "C" uint64_t MulleObjCChar5StringEncode64(char *src, size_t len);
extern "C" int      MulleObjCChar7StringIs64Bit(char *src, size_t len);
extern "C" uint64_t MulleObjCChar7StringEncode64(char *src, size_t len);

namespace {

class RewriteMulleObjC : public ASTConsumer {
  Rewriter                    Rewrite;
  DiagnosticsEngine          &Diags;
  const LangOptions          &LangOpts;
  ASTContext                 *Context = nullptr;
  SourceManager              *SM      = nullptr;
  std::unique_ptr<raw_ostream> OutFile;
  std::string                 InFileName;
  bool                        InMethod = false;
  ObjCMethodDecl             *CurrentMethod = nullptr;
  ObjCInterfaceDecl          *CurrentClass = nullptr;
  unsigned                    TryCount = 0;
  unsigned                    NSStringCount = 0;
  std::string                 NSStringDefs;
  std::vector<std::string>    NSStringPtrs;
  std::vector<ObjCImplementationDecl *> LoadClasses;
  std::vector<ObjCCategoryImplDecl *>   LoadCategories;
  std::set<std::string>       EmittedIvarStructs;
  struct SuperEntry { uint32_t superid; std::string name; uint32_t classid; uint32_t methodid; };
  std::vector<SuperEntry>     LoadSupers; // collected [super msg] calls
  bool                        HasObjCContent = false; // any ObjC seen?

public:
  RewriteMulleObjC(const std::string &InFile,
                   std::unique_ptr<raw_ostream> OS,
                   DiagnosticsEngine &Diags,
                   const LangOptions &LOpts,
                   bool /*SilenceRewriteMacroWarning*/)
      : Diags(Diags), LangOpts(LOpts),
        OutFile(std::move(OS)), InFileName(InFile) {}

  void Initialize(ASTContext &Ctx) override {
    Context = &Ctx;
    SM      = &Ctx.getSourceManager();
    Rewrite.setSourceMgr(*SM, LangOpts);
  }

  bool HandleTopLevelDecl(DeclGroupRef D) override {
    for (Decl *I : D)
      HandleTopLevelSingleDecl(I);
    return true;
  }

  void HandleTranslationUnit(ASTContext &C) override {
    if (Diags.hasErrorOccurred())
      return;

    std::string Result;
    if (const RewriteBuffer *Buf =
            Rewrite.getRewriteBufferFor(SM->getMainFileID()))
      Result = std::string(Buf->begin(), Buf->end());
    else
      Result = SM->getBufferData(SM->getMainFileID()).str();

    // Replace #import with #include so the output is valid C.
    // Also replace ObjC umbrella imports (import.h, import-private.h) with
    // the runtime header — the rewriter has already processed all ObjC decls.
    {
      size_t pos = 0;
      while ((pos = Result.find("#import", pos)) != std::string::npos) {
        Result.replace(pos, 7, "#include");
        pos += 8;
      }
      // Replace mulle-sde import.h / import-private.h with runtime header
      auto replaceObjCImport = [&](const std::string &pat) {
        size_t p = 0;
        while ((p = Result.find(pat, p)) != std::string::npos) {
          size_t lineEnd = Result.find('\n', p);
          Result.replace(p, lineEnd - p,
              "#include <mulle-objc-runtime/mulle-objc-runtime.h>");
          p += 50;
        }
      };
      replaceObjCImport("#include \"import.h\"");
      replaceObjCImport("#include \"import-private.h\"");

      // Prepend TPS/FCS/TAO defines before the runtime header so the C output
      // is self-contained (no need for -D flags when compiling the .c file).
      // Mirror the actual lang options so the runtime header sees the right values.
      const std::string runtimeInclude = "#include <mulle-objc-runtime/mulle-objc-runtime.h>";
      size_t rp = Result.find(runtimeInclude);
      if (rp != std::string::npos) {
        // @mulle-objc@ emit correct TPS/FCS/TAO based on lang options >
        std::string tps = LangOpts.ObjCDisableTaggedPointers
            ? "#ifndef __MULLE_OBJC_NO_TPS__\n# define __MULLE_OBJC_NO_TPS__\n#endif\n"
            : "#ifndef __MULLE_OBJC_TPS__\n# define __MULLE_OBJC_TPS__\n#endif\n";
        std::string fcs = LangOpts.ObjCDisableFastCalls
            ? "#ifndef __MULLE_OBJC_NO_FCS__\n# define __MULLE_OBJC_NO_FCS__\n#endif\n"
            : "#ifndef __MULLE_OBJC_FCS__\n# define __MULLE_OBJC_FCS__\n#endif\n";
        std::string tao = LangOpts.ObjCEnableThreadAffineObjects
            ? "#ifndef __MULLE_OBJC_TAO__\n# define __MULLE_OBJC_TAO__\n#endif\n"
            : "#ifndef __MULLE_OBJC_NO_TAO__\n# define __MULLE_OBJC_NO_TAO__\n#endif\n";
        Result.insert(rp, tps + fcs + tao);
        // @mulle-objc@ emit correct TPS/FCS/TAO based on lang options <
      }
    }

    // Strip ARC ownership qualifiers — not valid in plain C.
    for (const char *kw : {"__unsafe_unretained ", "__strong ", "__weak ", "__autoreleasing "}) {
      std::string s(kw);
      size_t p = 0;
      while ((p = Result.find(s, p)) != std::string::npos)
        Result.erase(p, s.size());
    }
    // Replace any remaining bare `id ` type uses (e.g. after stripping __unsafe_unretained)
    // Only replace `id ` when preceded by whitespace/newline (i.e. used as a type).
    {
      size_t p = 0;
      while ((p = Result.find("id ", p)) != std::string::npos) {
        bool atWordBoundary = (p == 0 || !isalnum((unsigned char)Result[p-1]) && Result[p-1] != '_');
        if (atWordBoundary)
          Result.replace(p, 3, "void *");
        else
          p += 3;
      }
    }

    if (!NSStringDefs.empty()) {
      // Insert static NSConstantString globals after the last #include line
      size_t pos = Result.rfind("\n#include");
      if (pos != std::string::npos) {
        pos = Result.find('\n', pos + 1) + 1;
        Result.insert(pos, NSStringDefs);
      } else {
        Result = NSStringDefs + Result;
      }
    }
    // Insert fastenumeration header if needed (for mulle_objc_enumeration_mutation)
    if (Result.find("mulle_objc_enumeration_mutation") != std::string::npos ||
        Result.find("NSFastEnumerationState") != std::string::npos) {
      size_t pos = Result.rfind("\n#include");
      if (pos != std::string::npos)
        pos = Result.find('\n', pos + 1) + 1;
      else if (Result.substr(0, 8) == "#include")
        pos = Result.find('\n') + 1;
      else
        pos = 0;
      Result.insert(pos,
        "#include <mulle-objc-runtime/mulle-objc-fastenumeration.h>\n"
        "#ifndef NSUInteger\n"
        "typedef unsigned long NSUInteger;\n"
        "#endif\n"
        "#ifndef NSFastEnumerationState\n"
        "typedef struct { NSUInteger state; void **itemsPtr; NSUInteger *mutationsPtr; NSUInteger extra[5]; } NSFastEnumerationState;\n"
        "#define NSFastEnumerationState NSFastEnumerationState\n"
        "#endif\n");
    }

    // Insert call-function macros after the last #include — only if ObjC was used.
    // These select inline vs non-inline variants based on optimization level,
    // mirroring CGObjCMulleRuntime's INLINE_CALL_LEVEL logic.
    if (HasObjCContent) {
      // @mulle-objc@ full 5-level inline support + forceLevel emits direct calls >
      unsigned forceLevel = LangOpts.ObjCInlineMethodCalls;
      std::string macros;

      if (forceLevel != 0) {
        // Level known at rewrite time — emit direct calls, no macro needed.
        struct { const char *call, *callSuper, *lookup; } fns[] = {
          { "mulle_objc_object_call",
            "mulle_objc_object_call_super",
            "mulle_objc_global_lookup_infraclass_nofail" },           // 1: none
          { "mulle_objc_object_call_inline_minimal",
            "mulle_objc_object_call_super_inline",
            "mulle_objc_global_lookup_infraclass_nofail" },           // 2: minimal
          { "mulle_objc_object_call_inline_partial",
            "mulle_objc_object_call_super_inline_partial",
            "mulle_objc_global_lookup_infraclass_inline_nofail" },    // 3: partial
          { "mulle_objc_object_call_inline",
            "mulle_objc_object_call_super_inline",
            "mulle_objc_global_lookup_infraclass_inline_nofail" },    // 4: default
          { "mulle_objc_object_call_inline_full",
            "mulle_objc_object_call_super_inline_full",
            "mulle_objc_global_lookup_infraclass_inline_nofail" },    // 5: full
        };
        unsigned idx = (forceLevel < 1 ? 0 : forceLevel > 5 ? 4 : forceLevel - 1);
        macros =
          std::string("#define mulle_objc_rewrite_call(obj,sel,param)            ") + fns[idx].call        + "(obj,sel,param)\n"
                     "#define mulle_objc_rewrite_call_super(obj,sel,param,sid)  "  + fns[idx].callSuper   + "(obj,sel,param,sid)\n"
                     "#define mulle_objc_rewrite_lookup_class(u,cid)            "  + fns[idx].lookup      + "(u,cid)\n";
      } else {
        // Level unknown — defer to downstream C compiler via __MULLE_OBJC_INLINE_METHOD_CALLS__.
        macros =
          "#ifndef __MULLE_OBJC_INLINE_METHOD_CALLS__\n"
          "# ifdef __OPTIMIZE_SIZE__\n"
          "#  define __MULLE_OBJC_INLINE_METHOD_CALLS__ 2\n"
          "# elif defined(__OPTIMIZE__)\n"
          "#  define __MULLE_OBJC_INLINE_METHOD_CALLS__ 4\n"
          "# else\n"
          "#  define __MULLE_OBJC_INLINE_METHOD_CALLS__ 1\n"
          "# endif\n"
          "#endif\n"
          "#if __MULLE_OBJC_INLINE_METHOD_CALLS__ <= 1\n"
          "# define mulle_objc_rewrite_call(obj,sel,param)            mulle_objc_object_call(obj,sel,param)\n"
          "# define mulle_objc_rewrite_call_super(obj,sel,param,sid)  mulle_objc_object_call_super(obj,sel,param,sid)\n"
          "# define mulle_objc_rewrite_lookup_class(u,cid)            mulle_objc_global_lookup_infraclass_nofail(u,cid)\n"
          "#elif __MULLE_OBJC_INLINE_METHOD_CALLS__ == 2\n"
          "# define mulle_objc_rewrite_call(obj,sel,param)            mulle_objc_object_call_inline_minimal(obj,sel,param)\n"
          "# define mulle_objc_rewrite_call_super(obj,sel,param,sid)  mulle_objc_object_call_super_inline(obj,sel,param,sid)\n"
          "# define mulle_objc_rewrite_lookup_class(u,cid)            mulle_objc_global_lookup_infraclass_nofail(u,cid)\n"
          "#elif __MULLE_OBJC_INLINE_METHOD_CALLS__ == 3\n"
          "# define mulle_objc_rewrite_call(obj,sel,param)            mulle_objc_object_call_inline_partial(obj,sel,param)\n"
          "# define mulle_objc_rewrite_call_super(obj,sel,param,sid)  mulle_objc_object_call_super_inline_partial(obj,sel,param,sid)\n"
          "# define mulle_objc_rewrite_lookup_class(u,cid)            mulle_objc_global_lookup_infraclass_inline_nofail(u,cid)\n"
          "#elif __MULLE_OBJC_INLINE_METHOD_CALLS__ == 4\n"
          "# define mulle_objc_rewrite_call(obj,sel,param)            mulle_objc_object_call_inline(obj,sel,param)\n"
          "# define mulle_objc_rewrite_call_super(obj,sel,param,sid)  mulle_objc_object_call_super_inline(obj,sel,param,sid)\n"
          "# define mulle_objc_rewrite_lookup_class(u,cid)            mulle_objc_global_lookup_infraclass_inline_nofail(u,cid)\n"
          "#else\n"
          "# define mulle_objc_rewrite_call(obj,sel,param)            mulle_objc_object_call_inline_full(obj,sel,param)\n"
          "# define mulle_objc_rewrite_call_super(obj,sel,param,sid)  mulle_objc_object_call_super_inline_full(obj,sel,param,sid)\n"
          "# define mulle_objc_rewrite_lookup_class(u,cid)            mulle_objc_global_lookup_infraclass_inline_nofail(u,cid)\n"
          "#endif\n";
      }
      // @mulle-objc@ full 5-level inline support + forceLevel emits direct calls <

      size_t pos = Result.rfind("\n#include");
      if (pos != std::string::npos)
        pos = Result.find('\n', pos + 1) + 1;
      else
        pos = 0;
      Result.insert(pos, macros);
    }

    *OutFile << Result;

    // Emit load info and constructor
    std::string Load;
    llvm::raw_string_ostream LOS(Load);

    std::string ClassListStr = EmitLoadClassList();
    if (!ClassListStr.empty())
      LOS << ClassListStr;

    std::string CatListStr = EmitLoadCategoryList();
    if (!CatListStr.empty())
      LOS << CatListStr;

    std::string HashNameStr = EmitHashNameList();
    if (!HashNameStr.empty())
      LOS << HashNameStr;

    if (!NSStringPtrs.empty()) {
      LOS << "static struct {\n"
          << "  unsigned int n_loadstrings;\n"
          << "  struct _mulle_objc_object *loadstrings[" << NSStringPtrs.size() << "];\n"
          << "} OBJC_STATICSTRING_LOADS __attribute__((used, section(\".data.objc.objc_load_info\"))) = {\n"
          << "  " << NSStringPtrs.size() << ",\n  {";
      for (unsigned i = 0; i < NSStringPtrs.size(); ++i) {
        if (i) LOS << ",";
        LOS << "\n    " << NSStringPtrs[i];
      }
      LOS << "\n  }\n};\n";
    }

    if (!LoadSupers.empty()) {
      LOS << "static struct {\n"
          << "  unsigned int n_supers;\n"
          << "  struct _mulle_objc_super supers[" << LoadSupers.size() << "];\n"
          << "} OBJC_SUPER_LOADS __attribute__((used, section(\".data.objc.objc_load_info\"))) = {\n"
          << "  " << LoadSupers.size() << ",\n  {\n";
      for (auto &SE : LoadSupers) {
        LOS << "    { (mulle_objc_superid_t) 0x";
        LOS.write_hex(SE.superid);
        LOS << "U, \"" << SE.name << "\","
            << " (mulle_objc_classid_t) 0x";
        LOS.write_hex(SE.classid);
        LOS << "U,"
            << " (mulle_objc_methodid_t) 0x";
        LOS.write_hex(SE.methodid);
        LOS << "UL },\n";
      }
      LOS << "  }\n};\n";
    }

    if (HasObjCContent) {
    LOS << "static struct _mulle_objc_loadinfo OBJC_IMAGE_INFO"
        << " __attribute__((used, section(\".data.objc.objc_load_info\"))) = {\n"
        << "  { MULLE_OBJC_RUNTIME_LOAD_VERSION, MULLE_OBJC_RUNTIME_VERSION, 0, 0,\n"
        << "    0\n"
        << "#ifdef __MULLE_OBJC_NO_TPS__\n"
        << "    | _mulle_objc_loadinfo_notaggedptrs\n"
        << "#endif\n"
        << "#ifdef __MULLE_OBJC_NO_FCS__\n"
        << "    | _mulle_objc_loadinfo_nofastcalls\n"
        << "#endif\n"
        << "#ifdef __MULLE_OBJC_TAO__\n"
        << "    | _mulle_objc_loadinfo_threadaffineobjects\n"
        << "#endif\n"
        << "  },\n"
        << "  0,\n"  // loaduniverse
        << "  " << (LoadClasses.empty()    ? "0" : "(struct _mulle_objc_loadclasslist *)&OBJC_CLASS_LOADS") << ",\n"
        << "  " << (LoadCategories.empty() ? "0" : "(struct _mulle_objc_loadcategorylist *)&OBJC_CATEGORY_LOADS") << ",\n"
        << "  " << (LoadSupers.empty()     ? "0" : "(struct _mulle_objc_superlist *)&OBJC_SUPER_LOADS") << ",\n"
        << "  " << (NSStringPtrs.empty() ? "0" : "(struct _mulle_objc_loadstringlist *)&OBJC_STATICSTRING_LOADS") << ",\n"
        << "  " << (LoadClasses.empty() ? "0" : "(struct _mulle_objc_loadhashedstringlist *)&OBJC_HASHNAME_LOADS") << "\n"
        << "};\n";

    LOS << "\nstatic void __attribute__((constructor))\n"
           "__load_mulle_objc(void)\n"
           "{\n"
           "  mulle_objc_loadinfo_enqueue_nofail(&OBJC_IMAGE_INFO);\n"
           "}\n";
    } // HasObjCContent

    *OutFile << Load;
    OutFile->flush();
  }

private:
  void HandleTopLevelSingleDecl(Decl *D);
  void RewriteStmt(Stmt *S);
  void RewriteDeclStmt(DeclStmt *S);
  void RewriteTryStmt(ObjCAtTryStmt *S);
  void RewriteThrowStmt(ObjCAtThrowStmt *S);
  void RewriteForCollectionStmt(ObjCForCollectionStmt *S);

  // ObjC declaration handlers
  void RewriteInterfaceDecl(ObjCInterfaceDecl *D);
  void RewriteForwardClassDecl(ObjCInterfaceDecl *D);
  void RewriteImplementationDecl(ObjCImplementationDecl *D);
  void RewriteMethodDecl(ObjCMethodDecl *M, const ObjCContainerDecl *CD);
  void RewriteCategoryDecl(ObjCCategoryDecl *D);
  void RewriteCategoryImplDecl(ObjCCategoryImplDecl *D);
  void RewriteProtocolDecl(ObjCProtocolDecl *D);

  // Helpers
  std::string MethodCName(const ObjCMethodDecl *M, const ObjCContainerDecl *CD);
  std::string PrintType(QualType T);
  std::string ObjCEncodeType(QualType T);

  // ObjC expression/statement handlers
  void RewriteReturnStmt(ReturnStmt *S);
  void RewriteMessageExpr(ObjCMessageExpr *E);
  std::string EmitLoadClassList();
  std::string EmitLoadCategoryList();
  std::string EmitHashNameList();
  void RewriteStringLiteral(ObjCStringLiteral *E);
  void RewriteSelectorExpr(ObjCSelectorExpr *E);

  // Helper: replace source range with text
  void ReplaceText(SourceRange R, StringRef Text) {
    Rewrite.ReplaceText(R.getBegin(),
                        Rewrite.getRangeSize(R),
                        Text);
  }

  // Helper: insert TODO comment in place of an ObjC node
  void TodoComment(SourceRange R, StringRef NodeKind) {
    std::string S = "/* TODO: ";
    S += NodeKind;
    S += " */";
    ReplaceText(R, S);
  }
};

// ---------------------------------------------------------------------------
// Top-level dispatch
// ---------------------------------------------------------------------------

void RewriteMulleObjC::HandleTopLevelSingleDecl(Decl *D) {
  if (!D || D->isInvalidDecl()) return;
  if (SM->isInSystemHeader(D->getLocation())) return;

  switch (D->getKind()) {
  case Decl::ObjCInterface: {
    auto *ID = cast<ObjCInterfaceDecl>(D);
    if (ID->isThisDeclarationADefinition())
      RewriteInterfaceDecl(ID);
    else
      RewriteForwardClassDecl(ID);  // @class Foo;
    break;
  }
  case Decl::ObjCImplementation:
    RewriteImplementationDecl(cast<ObjCImplementationDecl>(D));
    LoadClasses.push_back(cast<ObjCImplementationDecl>(D));
    HasObjCContent = true;
    break;
  case Decl::ObjCCategory:
    RewriteCategoryDecl(cast<ObjCCategoryDecl>(D));
    break;
  case Decl::ObjCCategoryImpl:
    RewriteCategoryImplDecl(cast<ObjCCategoryImplDecl>(D));
    break;
  case Decl::ObjCProtocol:
    RewriteProtocolDecl(cast<ObjCProtocolDecl>(D));
    break;
  case Decl::Function:
    if (auto *FD = cast<FunctionDecl>(D)) {
      // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef >
      // Rewrite ObjC pointer types in C function parameter declarations
      for (auto *P : FD->parameters()) {
        QualType T = P->getType().getUnqualifiedType();
        if (!T->isObjCObjectPointerType() && !T->isObjCIdType()) continue;
        SourceLocation Start   = P->getBeginLoc();
        SourceLocation NameLoc = P->getLocation();
        if (Start.isInvalid() || NameLoc.isInvalid()) continue;
        unsigned Len = SM->getFileOffset(NameLoc) - SM->getFileOffset(Start);
        if (Len == 0) continue;
        Rewrite.ReplaceText(Start, Len, PrintType(T) + " ");
      }
      // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef <
      if (FD->isThisDeclarationADefinition() && FD->getBody())
        RewriteStmt(FD->getBody());
    }
    break;
  case Decl::ObjCCompatibleAlias: {
    // @compatibility_alias Foo Bar  ->  typedef OBJC_CLASS_Bar OBJC_CLASS_Foo;
    // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef >
    auto *AD = cast<ObjCCompatibleAliasDecl>(D);
    std::string S = "typedef OBJC_CLASS_" + AD->getClassInterface()->getNameAsString()
                  + " OBJC_CLASS_" + AD->getNameAsString() + ";";
    // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef <
    // getBeginLoc() points to the alias name, not '@' — scan back
    const char *p = SM->getCharacterData(D->getBeginLoc());
    while (*p != '@') --p;
    const char *q = SM->getCharacterData(D->getBeginLoc());
    // scan forward past the original class name to ';'
    while (*q && *q != ';') ++q;
    if (*q == ';') ++q;
    SourceLocation AtLoc = D->getBeginLoc().getLocWithOffset(p - SM->getCharacterData(D->getBeginLoc()));
    Rewrite.ReplaceText(AtLoc, q - p, S);
    break;
  }
  case Decl::Record: {
    auto *RD = cast<RecordDecl>(D);
    if (!RD->isThisDeclarationADefinition()) break;
    for (auto *Field : RD->fields()) {
      if (!isa<ObjCAtDefsFieldDecl>(Field)) continue;
      // Find @defs( in the source buffer near the record's opening brace
      SourceLocation LBrace = RD->getBraceRange().getBegin();
      if (LBrace.isInvalid()) break;
      const char *Buf = SM->getCharacterData(LBrace);
      // scan forward for '@defs('
      const char *p = Buf;
      while (*p && strncmp(p, "@defs(", 6) != 0) ++p;
      if (strncmp(p, "@defs(", 6) != 0) break;
      const char *nameStart = p + 6;
      while (*nameStart == ' ') ++nameStart;
      const char *nameEnd = nameStart;
      while (*nameEnd && *nameEnd != ')' && *nameEnd != ' ') ++nameEnd;
      std::string ClassName(nameStart, nameEnd - nameStart);
      const char *end = nameEnd;
      while (*end && *end != ')') ++end;
      if (*end == ')') ++end;
      SourceLocation DefsLoc = LBrace.getLocWithOffset(p - Buf);
      Rewrite.ReplaceText(DefsLoc, end - p, "OBJC_CLASS_" + ClassName + "_IVARS");
      break;
    }
    break;
  }
  }
}

// ---------------------------------------------------------------------------
// Statement/expression walker — recurse and rewrite ObjC nodes bottom-up
// ---------------------------------------------------------------------------

void RewriteMulleObjC::RewriteStmt(Stmt *S) {
  if (!S) return;

  // Recurse into children first (bottom-up)
  for (Stmt *Child : S->children())
    RewriteStmt(Child);

  // Now handle this node
  if (auto *E = dyn_cast<ObjCMessageExpr>(S))
    RewriteMessageExpr(E);
  else if (auto *E = dyn_cast<ObjCStringLiteral>(S))
    RewriteStringLiteral(E);
  else if (auto *E = dyn_cast<ObjCSelectorExpr>(S))
    RewriteSelectorExpr(E);
  else if (auto *RS = dyn_cast<ReturnStmt>(S))
    RewriteReturnStmt(RS);
  else if (auto *EE = dyn_cast<ObjCEncodeExpr>(S)) {
    std::string enc;
    Context->getObjCEncodingForType(EE->getEncodedType(), enc);
    ReplaceText(EE->getSourceRange(), "\"" + enc + "\"");
  }
  else if (auto *AP = dyn_cast<ObjCAutoreleasePoolStmt>(S))
    Rewrite.ReplaceText(AP->getAtLoc(), 16, "/* @autoreleasepool */");
  else if (auto *DS = dyn_cast<DeclStmt>(S))
    RewriteDeclStmt(DS);
  else if (auto *CE = dyn_cast<CStyleCastExpr>(S)) {
    QualType T = CE->getType();
    std::string CastStr;
    if (T->isObjCObjectPointerType() || T->isObjCIdType())
      CastStr = "(void *)";
    else if (auto *PT = T->getAs<PointerType>()) {
      QualType Pointee = PT->getPointeeType();
      if (Pointee->isObjCObjectPointerType() || Pointee->isObjCIdType())
        CastStr = "(void **)";
    }
    if (!CastStr.empty()) {
      SourceRange TR = CE->getLParenLoc().isValid()
          ? SourceRange(CE->getLParenLoc(), CE->getRParenLoc())
          : SourceRange();
      if (TR.isValid())
        Rewrite.ReplaceText(TR.getBegin(), Rewrite.getRangeSize(TR), CastStr);
    }
  }
  else if (auto *TS = dyn_cast<ObjCAtTryStmt>(S))
    RewriteTryStmt(TS);
  else if (auto *TS = dyn_cast<ObjCAtThrowStmt>(S))
    RewriteThrowStmt(TS);
  else if (auto *FE = dyn_cast<ObjCForCollectionStmt>(S))
    RewriteForCollectionStmt(FE);
  else if (auto *IV = dyn_cast<ObjCIvarRefExpr>(S)) {
    // bare ivar access -> self->ivar
    std::string R = "self->" + IV->getDecl()->getNameAsString();
    Rewrite.ReplaceText(IV->getSourceRange().getBegin(),
        Rewrite.getRangeSize(IV->getSourceRange()), R);
  }
}
// ObjC declaration implementations
// ---------------------------------------------------------------------------

void RewriteMulleObjC::RewriteForwardClassDecl(ObjCInterfaceDecl *D) {
  // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef >
  // @class Foo;  or  @protocolclass Foo;  ->  typedef struct { } OBJC_CLASS_Foo;
  std::string Name = D->getNameAsString();
  std::string S = "typedef struct { } OBJC_CLASS_" + Name + ";";
  SourceLocation End = Lexer::findLocationAfterToken(
      D->getEndLoc(), tok::semi, *SM, LangOpts, false);
  SourceLocation Begin = D->getBeginLoc();
  SourceRange R = End.isValid()
      ? SourceRange(Begin, End.getLocWithOffset(-1))
      : D->getSourceRange();
  ReplaceText(R, S);
  // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef <
}

void RewriteMulleObjC::RewriteInterfaceDecl(ObjCInterfaceDecl *D) {
  std::string Name = D->getNameAsString();
  std::string Guard = "OBJC_CLASS_" + Name + "_IVARS";

  // Own ivars as flat token list for the macro value
  std::string OwnIvars;
  for (auto *IV : D->ivars()) {
    QualType T = IV->getType();
    std::string ivarName = IV->getNameAsString();
    // For array types, emit "base_type name[N]" not "base_type[N] name"
    if (auto *AT = Context->getAsArrayType(T)) {
      std::string base = PrintType(AT->getElementType());
      std::string suffix;
      if (auto *CAT = dyn_cast<ConstantArrayType>(AT))
        suffix = "[" + std::to_string(CAT->getZExtSize()) + "]";
      else
        suffix = "[]";
      OwnIvars += base + " " + ivarName + suffix;
    } else {
      OwnIvars += PrintType(T) + " " + ivarName;
    }
    if (IV->isBitField()) {
      unsigned width = IV->getBitWidthValue();
      OwnIvars += ":";
      OwnIvars += std::to_string(width);
    }
    OwnIvars += "; ";
  }

  // The macro value inlines the superclass macro (expands at use-time)
  // Only include super macro if super has ivars (not a protocolclass).
  std::string MacroVal;
  if (ObjCInterfaceDecl *Super = D->getSuperClass()) {
    // Check if super has any ivars anywhere in its hierarchy
    bool superHasIvars = false;
    for (ObjCInterfaceDecl *C = Super; C; C = C->getSuperClass())
      if (C->ivar_begin() != C->ivar_end()) { superHasIvars = true; break; }
    if (superHasIvars)
      MacroVal += "OBJC_CLASS_" + Super->getNameAsString() + "_IVARS ";
  }
  MacroVal += OwnIvars;

  std::string S;
  llvm::raw_string_ostream OS(S);
  // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef >
  OS << "#ifndef " << Guard << "\n"
     << "#define " << Guard << " " << MacroVal << "\n"
     << "#endif\n"
     << "typedef struct { " << Guard << " } OBJC_CLASS_" << Name << ";";
  // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef <

  SourceRange R(D->getAtStartLoc(), D->getEndLoc().getLocWithOffset(3));
  ReplaceText(R, S);
  EmittedIvarStructs.insert(Name);
}

void RewriteMulleObjC::RewriteImplementationDecl(ObjCImplementationDecl *D) {
  CurrentClass = D->getClassInterface();

  // If the @interface/@protocolclass definition had invalid sloc (e.g. @protocolclass),
  // emit a bare typedef before the methods so the type is known.
  if (CurrentClass && EmittedIvarStructs.find(CurrentClass->getNameAsString()) == EmittedIvarStructs.end()) {
    std::string Name = CurrentClass->getNameAsString();
    // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef >
    Rewrite.InsertTextBefore(D->getAtStartLoc(),
        "typedef struct { } OBJC_CLASS_" + Name + ";\n");
    // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef <
    EmittedIvarStructs.insert(Name);
  }

  for (auto *M : D->methods()) {
    if (M->isAlias()) {
      // Erase the @method_implementation line.
      const char *Start = SM->getCharacterData(M->getBeginLoc());
      const char *p = Start;
      while (p > SM->getCharacterData(SM->getLocForStartOfFile(SM->getFileID(M->getBeginLoc()))) && *p != '@')
        --p;
      const char *q = Start;
      while (*q && *q != ';') ++q;
      if (*q == ';') ++q;
      SourceLocation AtLoc = M->getBeginLoc().getLocWithOffset(p - Start);
      Rewrite.ReplaceText(AtLoc, q - p, "");
    } else if (M->isSynthesizedAccessorStub()) {
      // Implicit or explicit @synthesize — emit C accessor before @implementation
      const ObjCPropertyDecl *PD = M->findPropertyDecl();
      if (!PD) continue;
      ObjCIvarDecl *IVar = nullptr;
      // find the ivar from property_impls, or fall back to _propName convention
      for (auto *PI : D->property_impls())
        if (PI->getPropertyDecl() == PD) { IVar = PI->getPropertyIvarDecl(); break; }
      std::string IVarName = IVar ? IVar->getNameAsString()
                                  : "_" + PD->getNameAsString();
      std::string IVarType = PrintType(IVar ? IVar->getType() : PD->getType());
      std::string ClassName = D->getClassInterface()->getNameAsString();
      std::string CName = MethodCName(M, D->getClassInterface());
      std::string Fn;
      llvm::raw_string_ostream FOS(Fn);
      std::string ObjCName = (M->isInstanceMethod() ? "-[" : "+[")
          + D->getClassInterface()->getNameAsString() + " "
          + M->getSelector().getAsString() + "]";
      // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef >
      std::string SelfTypeName = "OBJC_CLASS_" + ClassName;
      FOS << "static void *\n" << CName
          << "(" << SelfTypeName << " *self, mulle_objc_methodid_t _cmd, void *_param)"
          << " __asm__(\"" << ObjCName << "\");\n"
          << "static void *\n" << CName
          << "(" << SelfTypeName << " *self, mulle_objc_methodid_t _cmd, void *_param)\n{\n";
      // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef <
      if (M->getSelector().getNumArgs() == 0)
        FOS << "  return (void *)(intptr_t) self->" << IVarName << ";\n}\n";
      else
        FOS << "  self->" << IVarName << " = *(" << IVarType << " *)_param;\n  return NULL;\n}\n";
      Rewrite.InsertTextBefore(D->getAtStartLoc(), Fn);
    } else if (M->hasBody())
      RewriteMethodDecl(M, D);
  }

  // @synthesize: erase (accessor already emitted above); @dynamic: no-op comment
  for (auto *PI : D->property_impls()) {
    SourceLocation Beg = PI->getBeginLoc();
    if (Beg.isInvalid()) continue;  // implicit synthesize — no source to erase
    SourceLocation End = Lexer::findLocationAfterToken(
        PI->getSourceRange().getEnd(), tok::semi, *SM, LangOpts, false);
    if (End.isInvalid())
      End = PI->getSourceRange().getEnd();
    unsigned Len = SM->getFileOffset(End) - SM->getFileOffset(Beg);
    std::string Comment = (PI->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize)
        ? "/* @synthesize */" : "/* @dynamic */";
    Rewrite.ReplaceText(Beg, Len, Comment);
  }

  // Erase "@implementation ClassName" header line — find end of that line
  // D->getAtStartLoc() = '@', D->getLocation() = class name identifier
  // Advance past the class name to end of line
  SourceLocation ImplEnd = D->getLocation();
  // Move past the class name token
  ImplEnd = Lexer::getLocForEndOfToken(ImplEnd, 0, *SM, LangOpts);
  unsigned Len = SM->getFileOffset(ImplEnd) - SM->getFileOffset(D->getAtStartLoc());
  Rewrite.ReplaceText(D->getAtStartLoc(), Len, "");

  // Erase @end (4 chars)
  Rewrite.ReplaceText(D->getEndLoc(), 4, "");
}

// ---------------------------------------------------------------------------
// Build a C identifier for a method: -[Foo bar:baz:] -> Foo_im_bar_baz_
// ---------------------------------------------------------------------------
std::string RewriteMulleObjC::MethodCName(const ObjCMethodDecl *M,
                                           const ObjCContainerDecl *CD) {
  std::string S = CD->getNameAsString();
  S += M->isInstanceMethod() ? "_im_" : "_cm_";
  // Replace ':' with '_' in selector
  std::string sel = M->getSelector().getAsString();
  for (char &c : sel)
    if (c == ':') c = '_';
  S += sel;
  return S;
}

// ---------------------------------------------------------------------------
// ObjC type encoding for a QualType (used in ivar signatures/hashes)
// ---------------------------------------------------------------------------
std::string RewriteMulleObjC::ObjCEncodeType(QualType T) {
  std::string enc;
  Context->getObjCEncodingForType(T, enc);
  return enc;
}

// ---------------------------------------------------------------------------
// Print a QualType as C — map ObjC types to C equivalents
// ---------------------------------------------------------------------------
std::string RewriteMulleObjC::PrintType(QualType T) {
  // @mulle-objc@ use OBJC_CLASS_ prefix for ObjC class pointer types >
  // ObjC class pointer: Foo * -> OBJC_CLASS_Foo *
  if (T->isObjCObjectPointerType()) {
    if (T->isObjCIdType())
      return "void *";
    if (auto *OPT = T->getAs<ObjCObjectPointerType>()) {
      if (auto *ID = OPT->getInterfaceDecl())
        return "OBJC_CLASS_" + ID->getNameAsString() + " *";
    }
    return "void *";
  }
  if (T->isObjCIdType())
    return "void *";
  if (T->isObjCClassType())
    return "void *";
  // id * -> void ** (pointer to ObjC object pointer)
  if (auto *PT = T->getAs<PointerType>()) {
    QualType Pointee = PT->getPointeeType();
    if (Pointee->isObjCObjectPointerType() || Pointee->isObjCIdType())
      return "void **";
  }
  // @mulle-objc@ use OBJC_CLASS_ prefix for ObjC class pointer types <
  // Everything else: use clang's printer
  std::string S;
  llvm::raw_string_ostream OS(S);
  T.print(OS, Context->getPrintingPolicy());
  return OS.str();
}

// ---------------------------------------------------------------------------
// Rewrite a single method to a C function
// ---------------------------------------------------------------------------
void RewriteMulleObjC::RewriteMethodDecl(ObjCMethodDecl *M,
                                          const ObjCContainerDecl *CD) {
  if (!M->hasBody()) return;

  // The ObjC name: -[Foo bar:baz:]
  std::string ObjCName;
  ObjCName += M->isInstanceMethod() ? '-' : '+';
  ObjCName += '[';
  ObjCName += CD->getNameAsString();
  ObjCName += ' ';
  ObjCName += M->getSelector().getAsString();
  ObjCName += ']';

  std::string CName = MethodCName(M, CD);

  // Build _param struct and unpack locals
  std::string ParamStructDef;
  std::string Unpack;
  RecordDecl *RD = M->getParamRecord();

  if (RD && (M->param_size() > 1 || M->isVariadic())) {
    // Multi-param or variadic: _param points to a struct
    // (mulle_vararg_start needs _param to be a typed struct pointer)
    std::string StructName = CName + "_t";
    llvm::raw_string_ostream SDef(ParamStructDef);
    SDef << "struct " << StructName << " { ";
    for (auto *FD : RD->fields())
      SDef << PrintType(FD->getType()) << " " << FD->getNameAsString() << "; ";
    SDef << "};\n";

    llvm::raw_string_ostream UOS(Unpack);
    // For variadic methods, keep _param as struct pointer (mulle_vararg_start uses it).
    // For non-variadic multi-param, unpack all fields.
    if (M->isVariadic()) {
      // _param is already typed struct pointer in the signature.
      for (auto *FD : RD->fields())
        UOS << "  " << PrintType(FD->getType()) << " " << FD->getNameAsString()
            << " = _param->" << FD->getNameAsString() << ";\n";
    } else {
      for (auto *FD : RD->fields())
        UOS << "  " << PrintType(FD->getType()) << " " << FD->getNameAsString()
            << " = ((struct " << StructName << " *)_param)->"
            << FD->getNameAsString() << ";\n";
    }

  } else if (M->param_size() == 1) {
    // Single-param: _param IS a pointer to the single value
    auto *P = *M->param_begin();
    llvm::raw_string_ostream UOS(Unpack);
    UOS << "  " << PrintType(P->getType()) << " " << P->getNameAsString()
        << " = *(" << PrintType(P->getType()) << " *)_param;\n";
  }

  // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef >
  std::string SelfType = M->isInstanceMethod()
      ? "OBJC_CLASS_" + CD->getNameAsString() + " *"
      : "void *";
  // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef <
  std::string ParamType = (M->isVariadic() && RD)
      ? "struct " + CName + "_t *"
      : "void *";
  // Build the C function signature with __asm__ for the ObjC name
  std::string Sig;
  llvm::raw_string_ostream SigOS(Sig);
  SigOS << "static void *" << CName
        << "(" << SelfType << "self, mulle_objc_methodid_t _cmd, " << ParamType << "_param)"
        << " __asm__(\"" << ObjCName << "\");\n"
        << "static void *" << CName
        << "(" << SelfType << "self, mulle_objc_methodid_t _cmd, " << ParamType << "_param)\n";

  // Rewrite inner ObjC expressions in the body first
  InMethod = true;
  CurrentMethod = M;
  RewriteStmt(M->getBody());
  InMethod = false;
  CurrentMethod = nullptr;
  std::string BodyText = Rewrite.getRewrittenText(M->getBody()->getSourceRange());

  // Inject unpack locals after opening brace, and ensure void methods return 0
  std::string FuncBody;
  bool isVoidReturn = M->getReturnType()->isVoidType();
  if (!Unpack.empty() && !BodyText.empty() && BodyText[0] == '{')
    FuncBody = "{\n" + Unpack + BodyText.substr(1);
  else
    FuncBody = BodyText;
  // void ObjC methods need explicit return 0 since C function returns void*
  if (isVoidReturn && !FuncBody.empty() && FuncBody.back() == '}')
    FuncBody.insert(FuncBody.size() - 1, " return 0;");

  std::string Full = ParamStructDef + Sig + FuncBody;

  SourceRange MRange(M->getBeginLoc(), M->getBody()->getEndLoc());
  Rewrite.ReplaceText(MRange.getBegin(), Rewrite.getRangeSize(MRange), Full);
}

void RewriteMulleObjC::RewriteCategoryDecl(ObjCCategoryDecl *D) {
  // @interface Foo (Cat) — just erase it, no C output needed
  ReplaceText(D->getSourceRange(), "");
}

void RewriteMulleObjC::RewriteCategoryImplDecl(ObjCCategoryImplDecl *D) {
  CurrentClass = D->getClassInterface();
  LoadCategories.push_back(D);
  HasObjCContent = true;

  for (auto *M : D->methods()) {
    if (M->isAlias()) {
      const char *Start = SM->getCharacterData(M->getBeginLoc());
      const char *p = Start;
      while (p > SM->getCharacterData(SM->getLocForStartOfFile(SM->getFileID(M->getBeginLoc()))) && *p != '@')
        --p;
      const char *q = Start;
      while (*q && *q != ';') ++q;
      if (*q == ';') ++q;
      Rewrite.ReplaceText(M->getBeginLoc().getLocWithOffset(p - Start), q - p, "");
    } else if (M->hasBody())
      RewriteMethodDecl(M, D->getClassInterface());
  }

  // Erase @implementation Foo (Cat) header up to first method
  SourceLocation EraseEnd;
  for (auto *M : D->methods())
    if (M->hasBody()) { EraseEnd = M->getBeginLoc(); break; }

  if (EraseEnd.isValid()) {
    unsigned Len = SM->getFileOffset(EraseEnd) - SM->getFileOffset(D->getAtStartLoc());
    Rewrite.ReplaceText(D->getAtStartLoc(), Len, "");
  } else {
    SourceLocation ImplEnd = Lexer::getLocForEndOfToken(D->getLocation(), 0, *SM, LangOpts);
    // skip past ')' of category name
    ImplEnd = Lexer::findLocationAfterToken(ImplEnd, tok::r_paren, *SM, LangOpts, true);
    if (ImplEnd.isValid()) {
      unsigned Len = SM->getFileOffset(ImplEnd) - SM->getFileOffset(D->getAtStartLoc());
      Rewrite.ReplaceText(D->getAtStartLoc(), Len, "");
    }
  }
  // Erase @end
  Rewrite.ReplaceText(D->getEndLoc(), 4, "");
}

void RewriteMulleObjC::RewriteProtocolDecl(ObjCProtocolDecl *D) {
  // @protocol — no C output needed
  // Only erase the definition; forward decls are handled by RewriteForwardClassDecl
  // (for @protocolclass) or are already erased by the interface rewrite.
  if (D->isThisDeclarationADefinition())
    ReplaceText(D->getSourceRange(), "");
}

// ---------------------------------------------------------------------------
// ObjC expression implementations
// ---------------------------------------------------------------------------

void RewriteMulleObjC::RewriteSelectorExpr(ObjCSelectorExpr *E) {
  HasObjCContent = true;
  // @selector(foo:bar:)  ->  ((mulle_objc_methodid_t) 0x12345678UL)
  std::string selStr = E->getSelector().getAsString();
  uint32_t hash = MulleObjCUniqueIdHashForString(selStr);
  std::string S;
  llvm::raw_string_ostream OS(S);
  OS << "((mulle_objc_methodid_t) 0x";
  OS.write_hex(hash);
  OS << "UL) /* @selector(" << selStr << ") */";
  ReplaceText(E->getSourceRange(), OS.str());
}

void RewriteMulleObjC::RewriteDeclStmt(DeclStmt *S) {
  for (auto *D : S->decls()) {
    // Handle @defs(ClassName) inside a local struct declaration
    if (auto *RD = dyn_cast<RecordDecl>(D)) {
      for (auto *Field : RD->fields()) {
        if (!isa<ObjCAtDefsFieldDecl>(Field)) continue;
        SourceLocation LBrace = RD->getBraceRange().getBegin();
        if (LBrace.isInvalid()) break;
        const char *Buf = SM->getCharacterData(LBrace);
        const char *p = Buf;
        while (*p && strncmp(p, "@defs(", 6) != 0) ++p;
        if (strncmp(p, "@defs(", 6) != 0) break;
        const char *nameStart = p + 6;
        while (*nameStart == ' ') ++nameStart;
        const char *nameEnd = nameStart;
        while (*nameEnd && *nameEnd != ')' && *nameEnd != ' ') ++nameEnd;
        std::string ClassName(nameStart, nameEnd - nameStart);
        const char *end = nameEnd;
        while (*end && *end != ')') ++end;
        if (*end == ')') ++end;
        SourceLocation DefsLoc = LBrace.getLocWithOffset(p - Buf);
        Rewrite.ReplaceText(DefsLoc, end - p, "OBJC_CLASS_" + ClassName + "_IVARS");
        break;
      }
      continue;
    }
    auto *VD = dyn_cast<VarDecl>(D);
    if (!VD) continue;
    QualType T = VD->getType().getUnqualifiedType();
    if (!T->isObjCObjectPointerType() && !T->isObjCIdType()) continue;
    // Replace everything from start of decl to start of variable name with the C type
    SourceLocation Start   = VD->getBeginLoc();
    SourceLocation NameLoc = VD->getLocation();
    if (NameLoc.isInvalid() || Start.isInvalid()) continue;
    unsigned Len = SM->getFileOffset(NameLoc) - SM->getFileOffset(Start);
    if (Len == 0) continue;
    Rewrite.ReplaceText(Start, Len, PrintType(T) + " ");
  }
}

void RewriteMulleObjC::RewriteThrowStmt(ObjCAtThrowStmt *S) {
  if (Expr *E = S->getThrowExpr()) {
    // The expression is already rewritten in-place (bottom-up).
    // Just replace "@throw " with "mulle_objc_exception_throw(" and
    // append ", 0)" before the semicolon.
    // @throw is 6 chars; replace up to (but not including) the expression.
    unsigned ThrowLen = SM->getFileOffset(E->getBeginLoc())
                      - SM->getFileOffset(S->getBeginLoc());
    Rewrite.ReplaceText(S->getBeginLoc(), ThrowLen,
                        "mulle_objc_exception_throw(");
    // Insert ", 0)" after the expression's (original) end
    SourceLocation ExprEnd = Lexer::getLocForEndOfToken(
        E->getEndLoc(), 0, *SM, LangOpts);
    Rewrite.InsertTextBefore(ExprEnd, ", 0)");
  } else {
    // bare @throw; -> rethrow
    Rewrite.ReplaceText(S->getBeginLoc(),
        Rewrite.getRangeSize(S->getSourceRange()),
        "mulle_objc_exception_throw(_rethrow, 0)");
  }
}

void RewriteMulleObjC::RewriteForCollectionStmt(ObjCForCollectionStmt *S) {
  // for (Type var in collection) body
  // ->
  // { NSFastEnumerationState _fe_state = {0};
  //   id _fe_buf[16];
  //   NSUInteger _fe_count, _fe_idx, _fe_mutations;
  //   Type var;
  //   _fe_count = (NSUInteger)(intptr_t)mulle_objc_object_call(collection, hash,
  //       &(struct{NSFastEnumerationState *rover_; id *objects_; NSUInteger count_;}){&_fe_state, _fe_buf, 16});
  //   if (_fe_count) { _fe_mutations = *_fe_state.mutationsPtr; _fe_idx = 0;
  //     for (;;) {
  //       if (_fe_idx >= _fe_count) {
  //         _fe_count = ...; if (!_fe_count) break; _fe_idx = 0; }
  //       if (*_fe_state.mutationsPtr != _fe_mutations) mulle_objc_enumeration_mutation(collection);
  //       var = _fe_state.itemsPtr[_fe_idx++];
  //       body
  //     }
  //   }
  // }

  static unsigned FeCount = 0;
  std::string idx = std::to_string(FeCount++);
  std::string state   = "_fe_state"   + idx;
  std::string buf     = "_fe_buf"     + idx;
  std::string count   = "_fe_count"   + idx;
  std::string fidx    = "_fe_idx"     + idx;
  std::string muts    = "_fe_muts"    + idx;

  // selector hash for countByEnumeratingWithState:objects:count:
  uint32_t selHash = MulleObjCUniqueIdHashForString("countByEnumeratingWithState:objects:count:");
  std::string hashStr;
  llvm::raw_string_ostream HS(hashStr);
  HS << "(mulle_objc_methodid_t) 0x"; HS.write_hex(selHash); HS << "UL";

  // collection expression (already rewritten by bottom-up recursion)
  std::string collText = Rewrite.getRewrittenText(S->getCollection()->getSourceRange());

  // loop variable — may be a DeclStmt (new var) or an existing expr
  std::string varDecl, varName;
  if (auto *DS = dyn_cast<DeclStmt>(S->getElement())) {
    auto *VD = cast<VarDecl>(DS->getSingleDecl());
    varName = VD->getNameAsString();
    varDecl = PrintType(VD->getType()) + " " + varName + ";\n  ";
  } else {
    varName = Rewrite.getRewrittenText(S->getElement()->getSourceRange());
  }

  // body (already rewritten)
  std::string bodyText = Rewrite.getRewrittenText(S->getBody()->getSourceRange());
  // Ensure body ends with semicolon if it's a bare expression statement
  if (!bodyText.empty() && bodyText.back() != '}' && bodyText.back() != ';')
    bodyText += ";";

  // call helper macro — use temp var to avoid macro comma issues
  std::string feParam = "_fe_param" + idx;
  std::string callExpr =
    "({ struct { NSFastEnumerationState *rover_; void **objects_; NSUInteger count_; } " + feParam +
    " = { &" + state + ", (void **)" + buf + ", 16 }; "
    "(NSUInteger)(intptr_t)mulle_objc_rewrite_call(" + collText + ", " + hashStr +
    ", &" + feParam + "); })";

  std::string R;
  R += "{ NSFastEnumerationState " + state + " = { 0 };\n";
  R += "  void *" + buf + "[16];\n";
  R += "  NSUInteger " + count + ", " + fidx + ", " + muts + ";\n";
  R += "  " + varDecl;
  R += "  " + count + " = " + callExpr + ";\n";
  R += "  if (" + count + ") { " + muts + " = *" + state + ".mutationsPtr; " + fidx + " = 0;\n";
  R += "    for (;;) {\n";
  R += "      if (" + fidx + " >= " + count + ") {\n";
  R += "        " + count + " = " + callExpr + "; if (!" + count + ") break; " + fidx + " = 0; }\n";
  R += "      if (*" + state + ".mutationsPtr != " + muts + ") mulle_objc_enumeration_mutation(" + collText + ");\n";
  R += "      " + varName + " = " + state + ".itemsPtr[" + fidx + "++];\n";
  R += "      " + bodyText + "\n";
  R += "    }\n  }\n}";

  ReplaceText(S->getSourceRange(), R);
}

void RewriteMulleObjC::RewriteTryStmt(ObjCAtTryStmt *S) {
  std::string excVar = "_exc_" + std::to_string(TryCount++);

  // Build the full replacement as a string, then do targeted replacements.
  // Strategy: replace @try -> opening of our block, rewrite @catch clauses,
  // rewrite @finally, and wrap everything.

  ObjCAtFinallyStmt *Finally = S->getFinallyStmt();
  unsigned NumCatch = S->getNumCatchStmts();

  // --- Replace "@try" (4 chars) with setup + if ---
  std::string TryOpen;
  TryOpen  = "{ struct { void *exception; void *previous; void *unused[2]; jmp_buf buf; } " + excVar + ";\n";
  TryOpen += "  mulle_objc_exception_tryenter(&" + excVar + ", 0);\n";
  TryOpen += "  if (!_setjmp(" + excVar + ".buf))";
  // @try is 4 chars
  Rewrite.ReplaceText(S->getAtTryLoc(), 4, TryOpen);

  // After the try body's closing brace, insert tryexit + else
  Stmt *TryBody = S->getTryBody();
  SourceLocation TryEnd = TryBody->getEndLoc(); // points to '}'
  std::string AfterTry = " mulle_objc_exception_tryexit(&" + excVar + ", 0); }";
  // insert before the '}' — replace '}' with '} tryexit; }'
  Rewrite.ReplaceText(TryEnd, 1, AfterTry);

  if (NumCatch > 0) {
    // Insert "else { void *_e = extract; " before first @catch
    ObjCAtCatchStmt *FirstCatch = S->getCatchStmt(0);
    std::string ElseOpen;
    ElseOpen  = " else {\n";
    ElseOpen += "  void *_rethrow = 0;\n";
    ElseOpen += "  void *_e = mulle_objc_exception_extract(&" + excVar + ", 0);\n";
    Rewrite.InsertTextBefore(FirstCatch->getBeginLoc(), ElseOpen);

    for (unsigned i = 0; i < NumCatch; ++i) {
      ObjCAtCatchStmt *Catch = S->getCatchStmt(i);
      VarDecl *CatchVar = Catch->getCatchParamDecl();

      if (!CatchVar) {
        // @catch (...) — catch all
        // Replace "@catch (...)" with "if (1)"
        Rewrite.ReplaceText(Catch->getBeginLoc(),
            SM->getFileOffset(Catch->getRParenLoc()) + 1
            - SM->getFileOffset(Catch->getBeginLoc()),
            "if (1)");
      } else {
        QualType T = CatchVar->getType();
        std::string CatchClass;
        if (auto *PT = T->getAs<ObjCObjectPointerType>())
          if (auto *ID = PT->getObjectType()->getInterface())
            CatchClass = ID->getNameAsString();

        std::string CatchVarName = CatchVar->getNameAsString();
        uint32_t classId = CatchClass.empty() ? 0
            : MulleObjCUniqueIdHashForString(CatchClass);

        std::string Cond;
        if (CatchClass.empty() || CatchClass == "id")
          Cond = "if (1)";
        else {
          std::string hex;
          llvm::raw_string_ostream HS(hex);
          HS << "0x"; HS.write_hex(classId); HS << "U";
          Cond = "if (mulle_objc_exception_match(_e, 0, (mulle_objc_classid_t) " + hex + "))";
        }

        // Replace "@catch (Type *var)" with "if (match) { Type *var = _e;"
        unsigned CatchHdrLen = SM->getFileOffset(Catch->getRParenLoc()) + 1
            - SM->getFileOffset(Catch->getBeginLoc());
        std::string CatchType = PrintType(T);
        Rewrite.ReplaceText(Catch->getBeginLoc(), CatchHdrLen,
            Cond + " { " + CatchType + " " + CatchVarName + " = (" + CatchType + ") _e;");

        // Close the extra '{' after the catch body
        SourceLocation BodyEnd = Catch->getCatchBody()->getEndLoc();
        Rewrite.ReplaceText(BodyEnd, 1, "} }");
      }
    }

    // Close the else block, with rethrow if needed
    ObjCAtCatchStmt *LastCatch = S->getCatchStmt(NumCatch - 1);
    SourceLocation AfterLastCatch = LastCatch->getCatchBody()->getEndLoc();
    // We already replaced that '}' above — insert after it
    std::string ElseClose = "\n  if (_rethrow) mulle_objc_exception_throw(_rethrow, 0);\n}";
    Rewrite.InsertTextAfter(AfterLastCatch, ElseClose);
  } else {
    // No catch — just else { extract; rethrow }
    std::string ElseRethrow;
    ElseRethrow  = " else {\n";
    ElseRethrow += "  void *_rethrow = mulle_objc_exception_extract(&" + excVar + ", 0);\n";
    ElseRethrow += "  mulle_objc_exception_throw(_rethrow, 0);\n}";
    if (Finally)
      Rewrite.InsertTextBefore(Finally->getBeginLoc(), ElseRethrow);
    // else appended after try body — handled by AfterTry above
  }

  // @finally — just strip the keyword
  if (Finally)
    Rewrite.ReplaceText(Finally->getBeginLoc(), 8, ""); // "@finally" = 8 chars

  // Close outer block after everything
  SourceLocation End = S->getEndLoc();
  Rewrite.InsertTextAfter(End, "\n}");
}

void RewriteMulleObjC::RewriteReturnStmt(ReturnStmt *S) {
  if (!InMethod) return;
  Expr *RV = S->getRetValue();
  if (!RV) return;
  QualType T = RV->getType();
  // void* and ObjC pointer returns are already compatible with void*
  if (T->isPointerType() || T->isObjCObjectPointerType() || T->isVoidType())
    return;

  // Struct (aggregate) return via MetaABI:
  // _param points to the union; write the struct into it and return _param.
  if (CurrentMethod && CurrentMethod->getRvalRecord()) {
    std::string RetType = PrintType(T);
    // Replace: return EXPR  →  *( RetType *)_param = EXPR; return _param
    // We insert before `return` and after the expression.
    SourceLocation RetLoc = S->getBeginLoc(); // points to 'return'
    SourceLocation Begin  = RV->getBeginLoc();
    SourceLocation End    = Lexer::getLocForEndOfToken(RV->getEndLoc(), 0, *SM, LangOpts);
    // Replace 'return ' with '*( RetType *)_param = '
    unsigned RetToExpr = SM->getFileOffset(Begin) - SM->getFileOffset(RetLoc);
    Rewrite.ReplaceText(RetLoc, RetToExpr, "*((" + RetType + " *)_param) = ");
    // After expression insert '; return _param'
    Rewrite.InsertTextBefore(End, "; return _param");
    return;
  }

  // Wrap scalar: return (void*)(intptr_t)(expr)
  // Use InsertText to avoid clobbering already-rewritten sub-expressions.
  SourceLocation Begin = RV->getBeginLoc();
  SourceLocation End = Lexer::getLocForEndOfToken(RV->getEndLoc(), 0, *SM, LangOpts);
  Rewrite.InsertTextBefore(Begin, "(void *)(intptr_t)(");
  Rewrite.InsertTextBefore(End, ")");
}

void RewriteMulleObjC::RewriteMessageExpr(ObjCMessageExpr *E) {
  HasObjCContent = true;
  std::string S;
  llvm::raw_string_ostream OS(S);

  ObjCMessageExpr::ReceiverKind Kind = E->getReceiverKind();
  bool isSuper = (Kind == ObjCMessageExpr::SuperInstance ||
                  Kind == ObjCMessageExpr::SuperClass);

  // Receiver text
  std::string Receiver;
  std::string SuperId;
  if (isSuper) {
    Receiver = "self";
    ObjCInterfaceDecl *Super = CurrentClass ? CurrentClass->getSuperClass() : nullptr;
    std::string SuperClassName = Super ? Super->getNameAsString() : "";
    std::string CallerClassName = CurrentClass ? CurrentClass->getNameAsString() : "";
    std::string SelName = E->getSelector().getAsString();
    // superid = hash("CallerClass;selectorName") — matches codegen GetSuperIdentifier
    std::string superKey = CallerClassName + ";" + SelName;
    uint32_t superHash  = MulleObjCUniqueIdHashForString(superKey);
    uint32_t classHash  = MulleObjCUniqueIdHashForString(CallerClassName);
    uint32_t methodHash = MulleObjCUniqueIdHashForString(SelName);
    llvm::raw_string_ostream SOS(SuperId);
    SOS << "(mulle_objc_superid_t) 0x"; SOS.write_hex(superHash); SOS << "UL";
    // Collect for OBJC_SUPER_LOADS (deduplicate by superKey)
    bool found = false;
    for (auto &SE : LoadSupers) if (SE.name == superKey) { found = true; break; }
    if (!found)
      LoadSupers.push_back({superHash, superKey, classHash, methodHash});
  } else if (Kind == ObjCMessageExpr::Instance) {
    Receiver = Rewrite.getRewrittenText(E->getInstanceReceiver()->getSourceRange());
  } else {
    // class message — look up the infraclass at runtime
    ObjCInterfaceDecl *ID = E->getClassReceiver()
        ->castAs<ObjCObjectType>()->getInterface();
    uint32_t classId = MulleObjCUniqueIdHashForString(ID->getNameAsString());
    std::string hex;
    llvm::raw_string_ostream HS(hex);
    HS << "0x"; HS.write_hex(classId); HS << "U";
    Receiver = "mulle_objc_rewrite_lookup_class(0, (mulle_objc_classid_t) " + hex + ")";
  }

  // Selector hash
  std::string SelStr = E->getSelector().getAsString();
  uint32_t Hash = MulleObjCUniqueIdHashForString(SelStr);
  std::string HashStr;
  llvm::raw_string_ostream HOS(HashStr);
  HOS << "(mulle_objc_methodid_t) 0x"; HOS.write_hex(Hash); HOS << "UL";

  // Return cast: message returns void*, cast to actual return type if needed
  QualType RetTy = E->getType();

  // MetaABI aggregate return: build union with rval field, read _p.rval after call.
  bool isAggregateReturn = false;
  if (RetTy->isRecordType() && !RetTy->isUnionType()) {
    if (auto *MD = E->getMethodDecl())
      isAggregateReturn = (MD->getRvalRecord() != nullptr);
    else
      isAggregateReturn = Context->typeNeedsMetaABIAlloca(RetTy);
  }

  bool needCast = !isAggregateReturn &&
                  !RetTy->isVoidType() &&
                  !RetTy->isObjCObjectPointerType() &&
                  !RetTy->isPointerType();
  std::string CastOpen, CastClose;
  if (needCast) {
    CastOpen  = "(" + PrintType(RetTy) + ")(intptr_t)(";
    CastClose = ")";
  }

  unsigned NumArgs = E->getNumArgs();

  if (isAggregateReturn) {
    // Build: ({ union { struct { args... } v; void *space; RetType rval; } _p = { .v = { args } };
    //          mulle_objc_rewrite_call(recv, sel, &_p); _p.rval; })
    static unsigned AggCount = 0;
    std::string pName = "_mulle_agg" + std::to_string(AggCount++);
    std::string retType = PrintType(RetTy);

    OS << "({ union { struct { ";
    Selector Sel = E->getSelector();
    unsigned NumSlots = Sel.getNumArgs();
    for (unsigned i = 0; i < NumArgs; ++i) {
      std::string fname = (i < NumSlots)
          ? Sel.getNameForSlot(i).str() + "_"
          : "_v" + std::to_string(i - NumSlots);
      OS << PrintType(E->getArg(i)->getType()) << " " << fname << "; ";
    }
    if (NumArgs == 0) OS << "void *_dummy; ";
    OS << "} v; void *space; " << retType << " rval; } " << pName;
    if (NumArgs > 0) {
      OS << " = { .v = { ";
      for (unsigned i = 0; i < NumArgs; ++i) {
        if (i) OS << ", ";
        OS << Rewrite.getRewrittenText(E->getArg(i)->getSourceRange());
      }
      OS << " } }";
    }
    OS << "; ";
    if (isSuper)
      OS << "mulle_objc_rewrite_call_super(" << Receiver << ", " << HashStr << ", &" << pName << ", " << SuperId << ")";
    else
      OS << "mulle_objc_rewrite_call(" << Receiver << ", " << HashStr << ", &" << pName << ")";
    OS << "; " << pName << ".rval; })";
    ReplaceText(E->getSourceRange(), OS.str());
    return;
  }

  auto buildCall = [&](const std::string &param) {
    if (isSuper)
      OS << CastOpen << "mulle_objc_rewrite_call_super(" << Receiver << ", " << HashStr << ", " << param << ", " << SuperId << ")" << CastClose;
    else
      OS << CastOpen << "mulle_objc_rewrite_call(" << Receiver << ", " << HashStr << ", " << param << ")" << CastClose;
  };

  // At partial inlining (level 3+), retain/release bypass the message send.
  // When forceLevel is known, emit direct calls; otherwise use __MULLE_OBJC_INLINE_METHOD_CALLS__.
  // @mulle-objc@ full 5-level inline support + forceLevel emits direct calls >
  if (!isSuper && NumArgs == 0) {
    unsigned forceLevel = LangOpts.ObjCInlineMethodCalls;
    std::string selName = E->getSelector().getAsString();
    if (selName == "retain" || selName == "release") {
      bool isRetain = (selName == "retain");
      std::string inlineFn = isRetain ? "mulle_objc_object_retain_inline"
                                      : "mulle_objc_object_release_inline";
      std::string fallback = CastOpen
          + "mulle_objc_rewrite_call(" + Receiver + ", " + HashStr + ", NULL)"
          + CastClose;

      if (forceLevel >= 3) {
        // known at rewrite time: use inline directly
        if (isRetain)
          OS << CastOpen << inlineFn << "(" << Receiver << ")" << CastClose;
        else
          OS << inlineFn << "(" << Receiver << ")";
      } else if (forceLevel != 0) {
        // known at rewrite time: no inline
        OS << fallback;
      } else {
        // defer to downstream compiler — include trailing ';' inside the #if block
        OS << "#if __MULLE_OBJC_INLINE_METHOD_CALLS__ >= 3\n";
        if (isRetain)
          OS << CastOpen << inlineFn << "(" << Receiver << ")" << CastClose;
        else
          OS << inlineFn << "(" << Receiver << ")";
        OS << ";\n#else\n" << fallback << ";\n#endif\n";
        // Extend range to swallow the trailing semicolon so we don't get '#endif;'
        SourceLocation End = Lexer::findLocationAfterToken(
            E->getEndLoc(), tok::semi, *SM, LangOpts, false);
        SourceRange R = End.isValid()
            ? SourceRange(E->getBeginLoc(), End.getLocWithOffset(-1))
            : E->getSourceRange();
        ReplaceText(R, OS.str());
        return;
      }
    }
  }
  // @mulle-objc@ full 5-level inline support + forceLevel emits direct calls <

  if (NumArgs == 0) {
    buildCall("NULL");
  } else if (NumArgs == 1) {
    QualType ArgTy = E->getArg(0)->getType();
    std::string ArgText = Rewrite.getRewrittenText(E->getArg(0)->getSourceRange());
    buildCall("&(" + PrintType(ArgTy) + "){ " + ArgText + " }");
  } else {
    // Use a named temp variable to avoid macro argument comma confusion.
    static unsigned TmpCount = 0;
    std::string tmpName = "_mulle_p" + std::to_string(TmpCount++);
    std::string structType;
    llvm::raw_string_ostream TS(structType);
    TS << "struct { ";
    Selector Sel = E->getSelector();
    unsigned NumSlots = Sel.getNumArgs();
    for (unsigned i = 0; i < NumArgs; ++i) {
      std::string fname = (i < NumSlots)
          ? Sel.getNameForSlot(i).str() + "_"
          : "_v" + std::to_string(i - NumSlots);
      TS << PrintType(E->getArg(i)->getType()) << " " << fname << "; ";
    }
    TS << "}";
    OS << "({ " << structType << " " << tmpName << " = { ";
    for (unsigned i = 0; i < NumArgs; ++i) {
      if (i) OS << ", ";
      OS << Rewrite.getRewrittenText(E->getArg(i)->getSourceRange());
    }
    OS << " }; ";
    buildCall("&" + tmpName);
    OS << "; })";
  }

  ReplaceText(E->getSourceRange(), OS.str());
}

void RewriteMulleObjC::RewriteStringLiteral(ObjCStringLiteral *E) {
  StringRef Str = E->getString()->getString();
  size_t Len = Str.size();
  char *S = const_cast<char *>(Str.data());

  std::string Out;
  llvm::raw_string_ostream OS(Out);

  uint64_t value = 0;
  if (MulleObjCChar7StringIs64Bit(S, Len)) {
    value = MulleObjCChar7StringEncode64(S, Len);
    value = (value << 3) | 0x4;
  } else if (MulleObjCChar5StringIs64Bit(S, Len)) {
    value = MulleObjCChar5StringEncode64(S, Len);
    value = (value << 3) | 0x1;
  }

  if (value) {
    OS << "((void *) 0x";
    OS.write_hex(value);
    OS << "ULL) /* @\"" << Str << "\" */";
  } else {
    // Static NSConstantString struct — isa patched at load time by runtime.
    // Layout: { intptr_t rc; void *isa; const char *str; unsigned len; }
    // The object pointer is &str (field 2), matching the compiler's alias.
    std::string VarName = "__nsstr_" + std::to_string(NSStringCount++);
    std::string Def;
    llvm::raw_string_ostream DS(Def);
    DS << "static struct { intptr_t _rc; void *_isa; const char *_str; unsigned _len; } "
       << VarName << " = { (intptr_t) 0x";
    DS.write_hex((uint64_t)(INTPTR_MAX - 1));
    DS << ", 0, \"";
    for (char c : Str) { if (c == '"' || c == '\\') DS << '\\'; DS << c; }
    DS << "\", " << Len << " };\n";
    // Register with runtime load info so _isa gets patched at startup.
    // Collected into OBJC_STATICSTRING_LOADS at end of TU — no per-string section needed.
    NSStringPtrs.push_back("(struct _mulle_objc_object *)&" + VarName + "._str");
    NSStringDefs += Def;

    OS << "((void *) &" << VarName << "._str) /* @\"" << Str << "\" */";
  }

  ReplaceText(E->getSourceRange(), OS.str());
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Emit OBJC_CLASS_LOADS data structures
// ---------------------------------------------------------------------------
std::string RewriteMulleObjC::EmitLoadClassList() {
  if (LoadClasses.empty()) return "";

  std::string Out;
  llvm::raw_string_ostream OS(Out);

  std::vector<std::string> ClassVarNames;

  for (auto *D : LoadClasses) {
    ObjCInterfaceDecl *ID = D->getClassInterface();
    std::string ClassName = ID->getNameAsString();
    std::string VarBase    = "OBJC_CLASS___" + ClassName;  // C-safe name
    std::string VarAsmName = "OBJC_CLASS_$_" + ClassName;  // linker symbol
    ClassVarNames.push_back(VarBase);

    uint32_t classId    = MulleObjCUniqueIdHashForString(ClassName);
    uint32_t classIvarHash = MulleObjCUniqueIdHashForString(
        ID->getIvarHashString(*Context));

    ObjCInterfaceDecl *Super = ID->getSuperClass();
    std::string superName   = Super ? Super->getNameAsString() : "";
    uint32_t superId        = Super ? MulleObjCUniqueIdHashForString(superName) : 0;
    uint32_t superIvarHash  = Super ? MulleObjCUniqueIdHashForString(
        Super->getIvarHashString(*Context)) : 0;

    // Collect ivars and methods (needed by helpers below)
    std::vector<ObjCIvarDecl *> OwnIvars(ID->ivar_begin(), ID->ivar_end());
    std::vector<ObjCMethodDecl *> IMethods, CMethods;
    for (auto *M : D->methods())
      (M->isInstanceMethod() ? IMethods : CMethods).push_back(M);
    auto sortMethods = [](ObjCMethodDecl *A, ObjCMethodDecl *B) {
      return MulleObjCUniqueIdHashForString(A->getSelector().getAsString())
           < MulleObjCUniqueIdHashForString(B->getSelector().getAsString());
    };
    std::sort(IMethods.begin(), IMethods.end(), sortMethods);
    std::sort(CMethods.begin(), CMethods.end(), sortMethods);

    // Helper: emit ivar list as compound literal (or "0")
    auto EmitIvarList = [&]() -> std::string {
      if (OwnIvars.empty()) return "0";
      std::string S;
      llvm::raw_string_ostream L(S);
      L << "&(struct { unsigned int n_ivars; struct _mulle_objc_ivar ivars["
        << OwnIvars.size() << "]; }){\n    " << OwnIvars.size() << ",\n    {\n";
      for (auto *IV : OwnIvars) {
        std::string ivarName = IV->getNameAsString();
        std::string ivarEnc  = ObjCEncodeType(IV->getType());
        uint32_t ivarId = MulleObjCUniqueIdHashForString(ivarName + ":" + ivarEnc);
        std::string offset = IV->isBitField()
            ? "0"
            // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef >
            : "(int) __builtin_offsetof(OBJC_CLASS_" + ClassName + ", " + ivarName + ")";
            // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef <
        L << "      { { (mulle_objc_ivarid_t) 0x";
        L.write_hex(ivarId);
        L << "U, \"" << ivarName << "\", \"" << ivarEnc << "\" }, " << offset << " },\n";
      }
      L << "    }\n  }";
      return S;
    };

    // Helper: emit method list as compound literal (or "0")
    auto EmitMethodListInline = [&](const std::vector<ObjCMethodDecl *> &Methods,
                                    uint32_t catId) -> std::string {
      if (Methods.empty()) return "0";
      std::string S;
      llvm::raw_string_ostream L(S);
      L << "&(struct { unsigned int n_methods; struct _mulle_objc_loadcategory *loadcategory;"
           " struct _mulle_objc_method methods[" << Methods.size() << "]; }){\n    "
        << Methods.size() << ", ";
      if (catId) { L << "(struct _mulle_objc_loadcategory *) 0x"; L.write_hex(catId); L << "U"; }
      else L << "0";
      L << ",\n    {\n";
      for (auto *M : Methods) {
        std::string sel = M->getSelector().getAsString();
        uint32_t selId  = MulleObjCUniqueIdHashForString(sel);
        std::string cname;
        if (M->isAlias()) {
          if (auto *TM = M->getAliasMethod())
            cname = MethodCName(TM, D->getClassInterface());
          else if (auto *FD = M->getAliasFunction())
            cname = FD->getNameAsString();
        }
        if (cname.empty()) cname = MethodCName(M, D->getClassInterface());
        std::string methSig = Context->getObjCEncodingForMethodDecl(M);
        L << "      { { (mulle_objc_methodid_t) 0x";
        L.write_hex(selId);
        L << "U, \"" << methSig << "\", \"" << sel << "\", 0x200000 },"
             " (mulle_objc_implementation_t) " << cname << " },\n";
      }
      L << "    }\n  }";
      return S;
    };

    // Helper: emit protocol list as compound literal (or "0")
    auto EmitProtoList = [&]() -> std::string {
      auto &Protos = ID->getReferencedProtocols();
      if (Protos.empty()) return "0";
      std::string S;
      llvm::raw_string_ostream L(S);
      L << "&(struct { unsigned int n_protocols; struct _mulle_objc_protocol protocols["
        << Protos.size() << "]; }){\n    " << Protos.size() << ",\n    {\n";
      for (auto *P : Protos) {
        uint32_t pid = MulleObjCUniqueIdHashForString(P->getNameAsString());
        L << "      { (mulle_objc_protocolid_t) 0x";
        L.write_hex(pid);
        L << "U, \"" << P->getNameAsString() << "\" },\n";
      }
      L << "    }\n  }";
      return S;
    };

    // Helper: emit protocolclassids array as compound literal (or "0")
    auto EmitProtoClassIds = [&]() -> std::string {
      auto &Protos = ID->getReferencedProtocols();
      std::vector<std::pair<uint32_t,std::string>> PCIds;
      for (auto *P : Protos) {
        ObjCInterfaceDecl *PI = nullptr;
        auto Results = Context->getTranslationUnitDecl()->lookup(
            DeclarationName(&Context->Idents.get(P->getName())));
        for (auto *R : Results)
          if ((PI = dyn_cast<ObjCInterfaceDecl>(R))) break;
        if (PI && PI->isProtocolClass())
          PCIds.push_back({MulleObjCUniqueIdHashForString(P->getNameAsString()),
                           P->getNameAsString()});
      }
      if (PCIds.empty()) return "0";
      std::string S;
      llvm::raw_string_ostream L(S);
      L << "(mulle_objc_classid_t []){\n";
      for (auto &PC : PCIds) {
        L << "    (mulle_objc_classid_t) 0x";
        L.write_hex(PC.first);
        L << "U, /* " << PC.second << " */\n";
      }
      L << "    (mulle_objc_classid_t) 0\n  }";
      return S;
    };

    // Helper: emit property list as compound literal (or "0")
    auto EmitPropList = [&]() -> std::string {
      SmallVector<ObjCPropertyDecl *, 8> Props(ID->instance_properties());
      if (Props.empty()) return "0";
      std::string S;
      llvm::raw_string_ostream L(S);
      L << "&(struct { unsigned int n_properties; struct _mulle_objc_property properties["
        << Props.size() << "]; }){\n    " << Props.size() << ",\n    {\n";
      for (auto *PD : Props) {
        uint32_t propId   = MulleObjCUniqueIdHashForString(PD->getNameAsString());
        ObjCIvarDecl *IVar = PD->getPropertyIvarDecl();
        std::string ivarName = IVar ? IVar->getNameAsString() : "_" + PD->getNameAsString();
        uint32_t ivarId   = MulleObjCUniqueIdHashForString(ivarName);
        uint32_t getterId = MulleObjCUniqueIdHashForString(PD->getGetterName().getAsString());
        uint32_t setterId = PD->isReadOnly() ? 0
            : MulleObjCUniqueIdHashForString(PD->getSetterName().getAsString());
        std::string sig = Context->getObjCEncodingForPropertyDecl(PD, nullptr);
        L << "      { (mulle_objc_propertyid_t) 0x";
        L.write_hex(propId);
        L << "U, (mulle_objc_ivarid_t) 0x";
        L.write_hex(ivarId);
        L << "U, \"" << PD->getNameAsString() << "\", \"" << sig << "\","
             " (mulle_objc_methodid_t) 0x";
        L.write_hex(getterId);
        L << "U, (mulle_objc_methodid_t) 0x";
        L.write_hex(setterId);
        L << "U, 0, 0, 0 },\n";
      }
      L << "    }\n  }";
      return S;
    };

    // --- loadclass struct (all sub-structs inlined as compound literals) ---
    OS << "static struct _mulle_objc_loadclass " << VarBase
       << " __asm__(\"" << VarAsmName << "\")"
       << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
       << "  (mulle_objc_classid_t) 0x"; OS.write_hex(classId);
    OS << "U,\n  \"" << ClassName << "\",\n"
       << "  (mulle_objc_hash_t) 0x"; OS.write_hex(classIvarHash);
    OS << "U,\n"
       << "  (mulle_objc_classid_t) 0x"; OS.write_hex(superId);
    OS << "U,\n  " << (superName.empty() ? "0" : "\"" + superName + "\"") << ",\n"
       << "  (mulle_objc_hash_t) 0x"; OS.write_hex(superIvarHash);
    OS << "U,\n"
       << "  -1,\n"
       // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef >
       << "  " << (OwnIvars.empty() ? "0" : "(int) sizeof(OBJC_CLASS_" + ClassName + ")") << ",\n"
       // @mulle-objc@ use OBJC_CLASS_ prefix for class typedef <
       << "  " << EmitIvarList() << ",\n"
       << "  " << EmitMethodListInline(CMethods, 0) << ",\n"
       << "  " << EmitMethodListInline(IMethods, 0) << ",\n"
       << "  " << EmitPropList() << ",\n"
       << "  " << EmitProtoList() << ",\n"
       << "  " << EmitProtoClassIds() << ",\n"
       << "  0\n"  // origin
       << "};\n";
  }

  // --- class list ---
  OS << "static struct {\n"
     << "  unsigned int n_loadclasses;\n"
     << "  struct _mulle_objc_loadclass *loadclasses[" << ClassVarNames.size() << "];\n"
     << "} OBJC_CLASS_LOADS"
     << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
     << "  " << ClassVarNames.size() << ",\n  {";
  for (unsigned i = 0; i < ClassVarNames.size(); ++i) {
    if (i) OS << ",";
    OS << "\n    &" << ClassVarNames[i];
  }
  OS << "\n  }\n};\n";

  return Out;
}



// ---------------------------------------------------------------------------
// Emit OBJC_CATEGORY_LOADS data structures
// ---------------------------------------------------------------------------
std::string RewriteMulleObjC::EmitLoadCategoryList() {
  if (LoadCategories.empty()) return "";

  std::string Out;
  llvm::raw_string_ostream OS(Out);
  std::vector<std::string> CatVarNames;

  for (auto *D : LoadCategories) {
    ObjCInterfaceDecl *ID  = D->getClassInterface();
    std::string ClassName  = ID->getNameAsString();
    std::string CatName    = D->getName().str();
    std::string VarBase    = "OBJC_CATEGORY___" + ClassName + "_" + CatName;
    std::string VarAsmName = "OBJC_CATEGORY_$_" + ClassName + "_" + CatName;
    CatVarNames.push_back(VarBase);

    uint32_t catId   = MulleObjCUniqueIdHashForString(CatName);
    uint32_t classId = MulleObjCUniqueIdHashForString(ClassName);
    uint32_t classIvarHash = MulleObjCUniqueIdHashForString(
        ID->getIvarHashString(*Context));

    std::vector<ObjCMethodDecl *> IMethods, CMethods;
    for (auto *M : D->methods())
      (M->isInstanceMethod() ? IMethods : CMethods).push_back(M);
    {
      auto sortMethods = [](ObjCMethodDecl *A, ObjCMethodDecl *B) {
        return MulleObjCUniqueIdHashForString(A->getSelector().getAsString())
             < MulleObjCUniqueIdHashForString(B->getSelector().getAsString());
      };
      std::sort(IMethods.begin(), IMethods.end(), sortMethods);
      std::sort(CMethods.begin(), CMethods.end(), sortMethods);
    }

    auto EmitMethodList = [&](const std::vector<ObjCMethodDecl *> &Methods,
                               uint32_t catId) -> std::string {
      if (Methods.empty()) return "0";
      std::string S;
      llvm::raw_string_ostream L(S);
      L << "&(struct { unsigned int n_methods; struct _mulle_objc_loadcategory *loadcategory;"
           " struct _mulle_objc_method methods[" << Methods.size() << "]; }){\n    "
        << Methods.size() << ", ";
      if (catId) { L << "(struct _mulle_objc_loadcategory *) 0x"; L.write_hex(catId); L << "U"; }
      else L << "0";
      L << ",\n    {\n";
      for (auto *M : Methods) {
        std::string sel = M->getSelector().getAsString();
        uint32_t selId  = MulleObjCUniqueIdHashForString(sel);
        std::string cname;
        if (M->isAlias()) {
          if (auto *TM = M->getAliasMethod())
            cname = MethodCName(TM, D->getClassInterface());
          else if (auto *FD = M->getAliasFunction())
            cname = FD->getNameAsString();
        }
        if (cname.empty()) cname = MethodCName(M, D->getClassInterface());
        std::string methSig = Context->getObjCEncodingForMethodDecl(M);
        L << "      { { (mulle_objc_methodid_t) 0x";
        L.write_hex(selId);
        L << "U, \"" << methSig << "\", \"" << sel << "\", 0x200000 },"
             " (mulle_objc_implementation_t) " << cname << " },\n";
      }
      L << "    }\n  }";
      return S;
    };

    ObjCCategoryDecl *CatDecl = D->getCategoryDecl();

    auto EmitCatProtoList = [&]() -> std::string {
      if (!CatDecl) return "0";
      auto &Protos = CatDecl->getReferencedProtocols();
      if (Protos.empty()) return "0";
      std::string S;
      llvm::raw_string_ostream L(S);
      L << "&(struct { unsigned int n_protocols; struct _mulle_objc_protocol protocols["
        << Protos.size() << "]; }){\n    " << Protos.size() << ",\n    {\n";
      for (auto *P : Protos) {
        uint32_t pid = MulleObjCUniqueIdHashForString(P->getNameAsString());
        L << "      { (mulle_objc_protocolid_t) 0x";
        L.write_hex(pid);
        L << "U, \"" << P->getNameAsString() << "\" },\n";
      }
      L << "    }\n  }";
      return S;
    };

    auto EmitCatProtoClassIds = [&]() -> std::string {
      if (!CatDecl) return "0";
      std::vector<std::pair<uint32_t,std::string>> PCIds;
      for (auto *P : CatDecl->getReferencedProtocols()) {
        ObjCInterfaceDecl *PI = nullptr;
        auto Results = Context->getTranslationUnitDecl()->lookup(
            DeclarationName(&Context->Idents.get(P->getName())));
        for (auto *R : Results)
          if ((PI = dyn_cast<ObjCInterfaceDecl>(R))) break;
        if (PI && PI->isProtocolClass())
          PCIds.push_back({MulleObjCUniqueIdHashForString(P->getNameAsString()),
                           P->getNameAsString()});
      }
      if (PCIds.empty()) return "0";
      std::string S;
      llvm::raw_string_ostream L(S);
      L << "(mulle_objc_classid_t []){\n";
      for (auto &PC : PCIds) {
        L << "    (mulle_objc_classid_t) 0x";
        L.write_hex(PC.first);
        L << "U, /* " << PC.second << " */\n";
      }
      L << "    (mulle_objc_classid_t) 0\n  }";
      return S;
    };

    auto EmitCatPropList = [&]() -> std::string {
      if (!CatDecl) return "0";
      SmallVector<ObjCPropertyDecl *, 8> CatProps(CatDecl->instance_properties());
      if (CatProps.empty()) return "0";
      std::string S;
      llvm::raw_string_ostream L(S);
      L << "&(struct { unsigned int n_properties; struct _mulle_objc_property properties["
        << CatProps.size() << "]; }){\n    " << CatProps.size() << ",\n    {\n";
      for (auto *PD : CatProps) {
        uint32_t propId  = MulleObjCUniqueIdHashForString(PD->getNameAsString());
        ObjCIvarDecl *IVar = PD->getPropertyIvarDecl();
        std::string ivarName = IVar ? IVar->getNameAsString() : "_" + PD->getNameAsString();
        uint32_t ivarId  = MulleObjCUniqueIdHashForString(ivarName);
        uint32_t getterId = MulleObjCUniqueIdHashForString(PD->getGetterName().getAsString());
        uint32_t setterId = PD->isReadOnly() ? 0
            : MulleObjCUniqueIdHashForString(PD->getSetterName().getAsString());
        std::string sig = Context->getObjCEncodingForPropertyDecl(PD, nullptr);
        L << "      { (mulle_objc_propertyid_t) 0x";
        L.write_hex(propId);
        L << "U, (mulle_objc_ivarid_t) 0x";
        L.write_hex(ivarId);
        L << "U, \"" << PD->getNameAsString() << "\", \"" << sig << "\","
             " (mulle_objc_methodid_t) 0x";
        L.write_hex(getterId);
        L << "U, (mulle_objc_methodid_t) 0x";
        L.write_hex(setterId);
        L << "U, 0, 0, 0 },\n";
      }
      L << "    }\n  }";
      return S;
    };

    OS << "static struct _mulle_objc_loadcategory " << VarBase
       << " __asm__(\"" << VarAsmName << "\")"
       << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
       << "  (mulle_objc_categoryid_t) 0x"; OS.write_hex(catId);
    OS << "U,\n  \"" << CatName << "\",\n"
       << "  (mulle_objc_classid_t) 0x"; OS.write_hex(classId);
    OS << "U,\n  \"" << ClassName << "\",\n"
       << "  (mulle_objc_hash_t) 0x"; OS.write_hex(classIvarHash);
    OS << "U,\n"
       << "  " << EmitMethodList(CMethods, catId) << ",\n"
       << "  " << EmitMethodList(IMethods, catId) << ",\n"
       << "  " << EmitCatPropList() << ",\n"
       << "  " << EmitCatProtoList() << ",\n"
       << "  " << EmitCatProtoClassIds() << ",\n"
       << "  0\n"  // origin
       << "};\n";
  }

  OS << "static struct {\n"
     << "  unsigned int n_loadcategories;\n"
     << "  struct _mulle_objc_loadcategory *loadcategories[" << CatVarNames.size() << "];\n"
     << "} OBJC_CATEGORY_LOADS"
     << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
     << "  " << CatVarNames.size() << ",\n  {";
  for (unsigned i = 0; i < CatVarNames.size(); ++i) {
    if (i) OS << ",";
    OS << "\n    &" << CatVarNames[i];
  }
  OS << "\n  }\n};\n";
  return Out;
}


std::string RewriteMulleObjC::EmitHashNameList() {
  if (LoadClasses.empty()) return "";

  std::map<uint32_t, std::string> entries;
  auto add = [&](const std::string &s) {
    entries[MulleObjCUniqueIdHashForString(s)] = s;
  };

  for (auto *D : LoadCategories) {
    add(D->getName().str());
    for (auto *M : D->methods())
      add(M->getSelector().getAsString());
  }

  for (auto *D : LoadClasses) {
    ObjCInterfaceDecl *ID = D->getClassInterface();
    add(ID->getNameAsString());
    if (auto *Super = ID->getSuperClass())
      add(Super->getNameAsString());
    for (auto *P : ID->getReferencedProtocols())
      add(P->getNameAsString());
    // walk full ivar chain (own + inherited) for hashname coverage
    for (ObjCInterfaceDecl *C = ID; C; C = C->getSuperClass())
      for (auto *IV : C->ivars()) {
        add(IV->getNameAsString());
        add(IV->getNameAsString() + ":" + ObjCEncodeType(IV->getType()));
      }
    for (auto *M : D->methods())
      add(M->getSelector().getAsString());
  }

  std::string Out;
  llvm::raw_string_ostream OS(Out);
  OS << "static struct {\n"
     << "  unsigned int n_loadentries;\n"
     << "  struct _mulle_objc_loadhashedstring loadentries[" << entries.size() << "];\n"
     << "} OBJC_HASHNAME_LOADS"
     << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
     << "  " << entries.size() << ",\n  {\n";
  for (auto &[h, s] : entries) {
    OS << "    { (mulle_objc_uniqueid_t) 0x";
    OS.write_hex(h);
    OS << "U, \"" << s << "\" },\n";
  }
  OS << "  }\n};\n";
  return Out;
}


std::unique_ptr<ASTConsumer>
clang::CreateMulleObjCRewriter(const std::string &InFile,
                               std::unique_ptr<raw_ostream> OS,
                               DiagnosticsEngine &Diags,
                               const LangOptions &LOpts,
                               bool SilenceRewriteMacroWarning) {
  return std::make_unique<RewriteMulleObjC>(InFile, std::move(OS), Diags,
                                            LOpts, SilenceRewriteMacroWarning);
}
