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
  ObjCInterfaceDecl          *CurrentClass = nullptr;
  unsigned                    TryCount = 0;
  unsigned                    NSStringCount = 0;
  std::string                 NSStringDefs;
  std::vector<std::string>    NSStringPtrs;
  std::vector<ObjCImplementationDecl *> LoadClasses;
  std::vector<ObjCCategoryImplDecl *>   LoadCategories;
  std::set<std::string>       EmittedIvarStructs; // classes whose ivar struct was emitted

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

    LOS << "static struct _mulle_objc_loadinfo OBJC_LOAD_INFO"
        << " __attribute__((used, section(\".data.objc.objc_load_info\"))) = {\n"
        << "  { MULLE_OBJC_RUNTIME_LOAD_VERSION, 0, 0, 0, 0 },\n"
        << "  0,\n"  // loaduniverse
        << "  " << (LoadClasses.empty() ? "0" : "(struct _mulle_objc_loadclasslist *)&OBJC_CLASS_LOADS") << ",\n"
        << "  " << (LoadCategories.empty() ? "0" : "(struct _mulle_objc_loadcategorylist *)&OBJC_CATEGORY_LOADS") << ",\n"
        << "  0,\n"  // supers
        << "  " << (NSStringPtrs.empty() ? "0" : "(struct _mulle_objc_loadstringlist *)&OBJC_STATICSTRING_LOADS") << ",\n"
        << "  " << (LoadClasses.empty() ? "0" : "(struct _mulle_objc_loadhashedstringlist *)&OBJC_HASHNAME_LOADS") << "\n"
        << "};\n";

    LOS << "\nstatic void __attribute__((constructor))\n"
           "__load_mulle_objc(void)\n"
           "{\n"
           "  mulle_objc_loadinfo_enqueue_nofail(&OBJC_LOAD_INFO);\n"
           "}\n";

    *OutFile << Load;
    OutFile->flush();
  }

private:
  void HandleTopLevelSingleDecl(Decl *D);
  void RewriteStmt(Stmt *S);
  void RewriteDeclStmt(DeclStmt *S);
  void RewriteTryStmt(ObjCAtTryStmt *S);
  void RewriteThrowStmt(ObjCAtThrowStmt *S);

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
    if (auto *FD = cast<FunctionDecl>(D))
      if (FD->isThisDeclarationADefinition() && FD->getBody())
        RewriteStmt(FD->getBody());
    break;
  default:
    llvm::errs() << "TopLevel unhandled kind=" << D->getDeclKindName() << "\n";
    break;
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
    if (T->isObjCObjectPointerType() || T->isObjCIdType()) {
      SourceRange TR = CE->getLParenLoc().isValid()
          ? SourceRange(CE->getLParenLoc(), CE->getRParenLoc())
          : SourceRange();
      if (TR.isValid())
        Rewrite.ReplaceText(TR.getBegin(), Rewrite.getRangeSize(TR), "(void *)");
    }
  }
  else if (auto *TS = dyn_cast<ObjCAtTryStmt>(S))
    RewriteTryStmt(TS);
  else if (auto *TS = dyn_cast<ObjCAtThrowStmt>(S))
    RewriteThrowStmt(TS);
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
  // @class Foo;  or  @protocolclass Foo;  ->  typedef struct Foo Foo;
  std::string S = "typedef struct " + D->getNameAsString() + " " + D->getNameAsString() + ";";
  SourceLocation End = Lexer::findLocationAfterToken(
      D->getEndLoc(), tok::semi, *SM, LangOpts, false);
  SourceLocation Begin = D->getBeginLoc();
  SourceRange R = End.isValid()
      ? SourceRange(Begin, End.getLocWithOffset(-1))
      : D->getSourceRange();
  ReplaceText(R, S);
}

void RewriteMulleObjC::RewriteInterfaceDecl(ObjCInterfaceDecl *D) {
  std::string Name = D->getNameAsString();
  std::string Guard = "OBJC_CLASS_" + Name + "_IVARS";

  // Own ivars as flat token list for the macro value
  std::string OwnIvars;
  for (auto *IV : D->ivars()) {
    OwnIvars += PrintType(IV->getType());
    OwnIvars += " ";
    OwnIvars += IV->getNameAsString();
    OwnIvars += "; ";
  }

  // The macro value inlines the superclass macro (expands at use-time)
  std::string MacroVal;
  if (ObjCInterfaceDecl *Super = D->getSuperClass())
    MacroVal += "OBJC_CLASS_" + Super->getNameAsString() + "_IVARS ";
  MacroVal += OwnIvars;

  std::string S;
  llvm::raw_string_ostream OS(S);
  OS << "#ifndef " << Guard << "\n"
     << "#define " << Guard << " " << MacroVal << "\n"
     << "#endif\n"
     << "typedef struct " << Name << " { " << Guard << " } " << Name << ";";

  SourceRange R(D->getAtStartLoc(), D->getEndLoc().getLocWithOffset(3));
  ReplaceText(R, S);
  EmittedIvarStructs.insert(Name);
}

void RewriteMulleObjC::RewriteImplementationDecl(ObjCImplementationDecl *D) {
  CurrentClass = D->getClassInterface();

  // If the @interface/@protocolclass definition had invalid sloc (e.g. @protocolclass
  // with no explicit @interface), emit the ivar struct now before the methods.
  if (CurrentClass && EmittedIvarStructs.find(CurrentClass->getNameAsString()) == EmittedIvarStructs.end()) {
    // Synthesize and insert before @implementation
    std::string tmp;
    llvm::raw_string_ostream OS(tmp);
    std::string Name = CurrentClass->getNameAsString();
    std::string Guard = "OBJC_CLASS_" + Name + "_IVARS";
    std::string MacroVal;
    if (ObjCInterfaceDecl *Super = CurrentClass->getSuperClass())
      MacroVal += "OBJC_CLASS_" + Super->getNameAsString() + "_IVARS ";
    OS << "#ifndef " << Guard << "\n#define " << Guard << " " << MacroVal << "\n#endif\n"
       << "typedef struct " << Name << " { " << Guard << " } " << Name << ";\n";
    Rewrite.InsertTextBefore(D->getAtStartLoc(), OS.str());
    EmittedIvarStructs.insert(Name);
  }

  for (auto *M : D->methods())
    if (M->hasBody())
      RewriteMethodDecl(M, D);

  // @synthesize / @dynamic are no-ops in mulle-objc — replace with comment
  for (auto *PI : D->property_impls()) {
    std::string kind = (PI->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize)
        ? "@synthesize" : "@dynamic";
    // Include the trailing semicolon
    SourceLocation End = Lexer::findLocationAfterToken(
        PI->getSourceRange().getEnd(), tok::semi, *SM, LangOpts, false);
    if (End.isInvalid())
      End = PI->getSourceRange().getEnd();
    unsigned Len = SM->getFileOffset(End) - SM->getFileOffset(PI->getBeginLoc());
    Rewrite.ReplaceText(PI->getBeginLoc(), Len,
        "/* " + kind + " is a no-op in mulle-objc */");
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
  // Strip ObjC pointer types to void *
  if (T->isObjCObjectPointerType() || T->isObjCIdType())
    return "void *";
  if (T->isObjCClassType())
    return "void *";  // Class
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

  std::string SelfType = M->isInstanceMethod()
      ? CD->getNameAsString() + " *"
      : "void *";
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
  RewriteStmt(M->getBody());
  InMethod = false;
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

  for (auto *M : D->methods())
    if (M->hasBody())
      RewriteMethodDecl(M, D->getClassInterface());

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
    auto *VD = dyn_cast<VarDecl>(D);
    if (!VD) continue;
    QualType T = VD->getType();
    if (!T->isObjCObjectPointerType() && !T->isObjCIdType()) continue;
    // Replace everything from start of decl to start of variable name with "void *"
    SourceLocation Start   = VD->getBeginLoc();
    SourceLocation NameLoc = VD->getLocation();
    if (NameLoc.isInvalid() || Start.isInvalid()) continue;
    unsigned Len = SM->getFileOffset(NameLoc) - SM->getFileOffset(Start);
    if (Len == 0) continue;
    Rewrite.ReplaceText(Start, Len, "void *");
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

void RewriteMulleObjC::RewriteTryStmt(ObjCAtTryStmt *S) {
  std::string excVar = "_exc_" + std::to_string(TryCount++);

  // Build the full replacement as a string, then do targeted replacements.
  // Strategy: replace @try -> opening of our block, rewrite @catch clauses,
  // rewrite @finally, and wrap everything.

  ObjCAtFinallyStmt *Finally = S->getFinallyStmt();
  unsigned NumCatch = S->getNumCatchStmts();

  // --- Replace "@try" (4 chars) with setup + if ---
  std::string TryOpen;
  TryOpen  = "{ struct { void *_s; jmp_buf _j; } " + excVar + ";\n";
  TryOpen += "  mulle_objc_exception_tryenter(&" + excVar + ", 0);\n";
  TryOpen += "  if (!_setjmp(" + excVar + "._j))";
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
  // Wrap scalar: return (void*)(intptr_t)(expr)
  std::string ValText = Rewrite.getRewrittenText(RV->getSourceRange());
  std::string Wrapped = "(void *)(intptr_t)(" + ValText + ")";
  Rewrite.ReplaceText(RV->getBeginLoc(),
                      Rewrite.getRangeSize(RV->getSourceRange()), Wrapped);
}

void RewriteMulleObjC::RewriteMessageExpr(ObjCMessageExpr *E) {
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
    // superid = hash of the superclass name
    ObjCInterfaceDecl *Super = CurrentClass ? CurrentClass->getSuperClass() : nullptr;
    std::string SuperName = Super ? Super->getNameAsString() : "";
    uint32_t SHash = MulleObjCUniqueIdHashForString(SuperName);
    llvm::raw_string_ostream SOS(SuperId);
    SOS << "(mulle_objc_superid_t) 0x"; SOS.write_hex(SHash); SOS << "UL";
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
    Receiver = "mulle_objc_global_lookup_infraclass_inline_nofail(0, (mulle_objc_classid_t) " + hex + ")";
  }

  // Selector hash
  std::string SelStr = E->getSelector().getAsString();
  uint32_t Hash = MulleObjCUniqueIdHashForString(SelStr);
  std::string HashStr;
  llvm::raw_string_ostream HOS(HashStr);
  HOS << "(mulle_objc_methodid_t) 0x"; HOS.write_hex(Hash); HOS << "UL";

  // Return cast: message returns void*, cast to actual return type if needed
  QualType RetTy = E->getType();
  bool needCast = !RetTy->isVoidType() &&
                  !RetTy->isObjCObjectPointerType() &&
                  !RetTy->isPointerType();
  std::string CastOpen, CastClose;
  if (needCast) {
    CastOpen  = "(" + PrintType(RetTy) + ")(intptr_t)(";
    CastClose = ")";
  }

  std::string CallFn = isSuper ? "mulle_objc_object_call_super" : "mulle_objc_object_call";
  unsigned NumArgs = E->getNumArgs();

  auto buildCall = [&](const std::string &param) {
    OS << CastOpen << CallFn << "(" << Receiver << ", " << HashStr << ", " << param;
    if (isSuper) OS << ", " << SuperId;
    OS << ")" << CastClose;
  };

  if (NumArgs == 0) {
    buildCall("NULL");
  } else if (NumArgs == 1) {
    QualType ArgTy = E->getArg(0)->getType();
    std::string ArgText = Rewrite.getRewrittenText(E->getArg(0)->getSourceRange());
    buildCall("&(" + PrintType(ArgTy) + "){ " + ArgText + " }");
  } else {
    std::string param;
    llvm::raw_string_ostream PS(param);
    PS << "&(struct { ";
    Selector Sel = E->getSelector();
    unsigned NumSlots = Sel.getNumArgs();
    for (unsigned i = 0; i < NumArgs; ++i) {
      std::string fname = (i < NumSlots)
          ? Sel.getNameForSlot(i).str() + "_"
          : "_v" + std::to_string(i - NumSlots);
      PS << PrintType(E->getArg(i)->getType()) << " " << fname << "; ";
    }
    PS << "}){ ";
    for (unsigned i = 0; i < NumArgs; ++i) {
      if (i) PS << ", ";
      PS << Rewrite.getRewrittenText(E->getArg(i)->getSourceRange());
    }
    PS << " }";
    buildCall(param);
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
    std::string VarBase = "OBJC_CLASS_$_" + ClassName;
    ClassVarNames.push_back(VarBase);

    uint32_t classId    = MulleObjCUniqueIdHashForString(ClassName);
    uint32_t classIvarHash = MulleObjCUniqueIdHashForString(
        ID->getIvarHashString(*Context));

    ObjCInterfaceDecl *Super = ID->getSuperClass();
    std::string superName   = Super ? Super->getNameAsString() : "";
    uint32_t superId        = Super ? MulleObjCUniqueIdHashForString(superName) : 0;
    uint32_t superIvarHash  = Super ? MulleObjCUniqueIdHashForString(
        Super->getIvarHashString(*Context)) : 0;

    // --- instance variables ---
    std::vector<ObjCIvarDecl *> OwnIvars(ID->ivar_begin(), ID->ivar_end());
    std::string IvarListVar = "OBJC_INSTANCE_VARIABLES_" + ClassName;
    if (!OwnIvars.empty()) {
      OS << "static struct {\n"
         << "  unsigned int n_ivars;\n"
         << "  struct _mulle_objc_ivar ivars[" << OwnIvars.size() << "];\n"
         << "} " << IvarListVar
         << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
         << "  " << OwnIvars.size() << ",\n  {\n";
      for (auto *IV : OwnIvars) {
        std::string ivarName = IV->getNameAsString();
        std::string ivarEnc = ObjCEncodeType(IV->getType());
        uint32_t ivarId = MulleObjCUniqueIdHashForString(ivarName + ":" + ivarEnc);
        OS << "    { { (mulle_objc_ivarid_t) 0x";
        OS.write_hex(ivarId);
        OS << "U, \"" << ivarName << "\", \"" << ivarEnc << "\" },"
           << " (int) __builtin_offsetof(" << ClassName << ", " << ivarName << ") },\n";
      }
      OS << "  }\n};\n";
    }

    // --- instance methods ---
    std::vector<ObjCMethodDecl *> IMethods, CMethods;
    for (auto *M : D->methods())
      if (!M->isSynthesizedAccessorStub())
        (M->isInstanceMethod() ? IMethods : CMethods).push_back(M);

    auto EmitMethodList = [&](const std::string &VarName,
                               const std::vector<ObjCMethodDecl *> &Methods) {
      if (Methods.empty()) return;
      OS << "static struct {\n"
         << "  unsigned int n_methods;\n"
         << "  struct _mulle_objc_loadcategory *loadcategory;\n"
         << "  struct _mulle_objc_method methods[" << Methods.size() << "];\n"
         << "} " << VarName
         << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
         << "  " << Methods.size() << ", 0,\n  {\n";
      for (auto *M : Methods) {
        std::string sel = M->getSelector().getAsString();
        uint32_t selId = MulleObjCUniqueIdHashForString(sel);
        std::string cname = MethodCName(M, D->getClassInterface());
        // bits: 0x200000 = preload flag (standard for instance methods)
        OS << "    { { (mulle_objc_methodid_t) 0x";
        OS.write_hex(selId);
        OS << "U, \"\", \"" << sel << "\", 0x200000 }, (mulle_objc_implementation_t) " << cname << " },\n";
      }
      OS << "  }\n};\n";
    };

    std::string IMethodListVar = "OBJC_INSTANCE_METHODS_" + ClassName;
    std::string CMethodListVar = "OBJC_CLASS_METHODS_" + ClassName;
    EmitMethodList(IMethodListVar, IMethods);
    EmitMethodList(CMethodListVar, CMethods);

    // --- protocols ---
    std::string ProtoListVar;
    auto &Protos = ID->getReferencedProtocols();
    if (!Protos.empty()) {
      ProtoListVar = "OBJC_CLASS_PROTOCOLS_" + ClassName;
      OS << "static struct {\n"
         << "  unsigned int n_protocols;\n"
         << "  struct _mulle_objc_protocol protocols[" << Protos.size() << "];\n"
         << "} " << ProtoListVar
         << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
         << "  " << Protos.size() << ",\n  {\n";
      for (auto *P : Protos) {
        uint32_t pid = MulleObjCUniqueIdHashForString(P->getNameAsString());
        OS << "    { (mulle_objc_protocolid_t) 0x";
        OS.write_hex(pid);
        OS << "U, \"" << P->getNameAsString() << "\" },\n";
      }
      OS << "  }\n};\n";
    }

    // --- loadclass struct ---
    // instancesize = sizeof(ClassName) — emit as expression
    OS << "static struct _mulle_objc_loadclass " << VarBase
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
       << "  " << (OwnIvars.empty() ? "0" : "(int) sizeof(" + ClassName + ")") << ",\n"
       << "  " << (OwnIvars.empty()      ? "0" : "(struct _mulle_objc_ivarlist *)&"    + IvarListVar)    << ",\n"
       << "  " << (CMethods.empty()      ? "0" : "(struct _mulle_objc_methodlist *)&"  + CMethodListVar) << ",\n"
       << "  " << (IMethods.empty()      ? "0" : "(struct _mulle_objc_methodlist *)&"  + IMethodListVar) << ",\n"
       << "  0,\n"  // properties
       << "  " << (ProtoListVar.empty()  ? "0" : "(struct _mulle_objc_protocollist *)&" + ProtoListVar)  << ",\n"
       << "  0, 0\n"  // protocolclassids, origin
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
    std::string VarBase    = "OBJC_CATEGORY_$_" + ClassName + "_" + CatName;
    CatVarNames.push_back(VarBase);

    uint32_t catId   = MulleObjCUniqueIdHashForString(CatName);
    uint32_t classId = MulleObjCUniqueIdHashForString(ClassName);
    uint32_t classIvarHash = MulleObjCUniqueIdHashForString(
        ID->getIvarHashString(*Context));

    std::vector<ObjCMethodDecl *> IMethods, CMethods;
    for (auto *M : D->methods())
      if (!M->isSynthesizedAccessorStub())
        (M->isInstanceMethod() ? IMethods : CMethods).push_back(M);

    auto EmitMethodList = [&](const std::string &VarName,
                               const std::vector<ObjCMethodDecl *> &Methods) {
      if (Methods.empty()) return;
      OS << "static struct {\n"
         << "  unsigned int n_methods;\n"
         << "  struct _mulle_objc_loadcategory *loadcategory;\n"
         << "  struct _mulle_objc_method methods[" << Methods.size() << "];\n"
         << "} " << VarName
         << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
         << "  " << Methods.size() << ", 0,\n  {\n";
      for (auto *M : Methods) {
        std::string sel = M->getSelector().getAsString();
        uint32_t selId = MulleObjCUniqueIdHashForString(sel);
        std::string cname = MethodCName(M, D->getClassInterface());
        OS << "    { { (mulle_objc_methodid_t) 0x";
        OS.write_hex(selId);
        OS << "U, \"\", \"" << sel << "\", 0x200000 }, (mulle_objc_implementation_t) " << cname << " },\n";
      }
      OS << "  }\n};\n";
    };

    std::string IMethodListVar = "OBJC_CAT_INSTANCE_METHODS_" + ClassName + "_" + CatName;
    std::string CMethodListVar = "OBJC_CAT_CLASS_METHODS_"    + ClassName + "_" + CatName;
    EmitMethodList(IMethodListVar, IMethods);
    EmitMethodList(CMethodListVar, CMethods);

    // --- protocols ---
    std::string ProtoListVar;
    ObjCCategoryDecl *CatDecl = D->getCategoryDecl();
    if (CatDecl) {
      auto &Protos = CatDecl->getReferencedProtocols();
      if (!Protos.empty()) {
        ProtoListVar = "OBJC_CAT_PROTOCOLS_" + ClassName + "_" + CatName;
        OS << "static struct {\n"
           << "  unsigned int n_protocols;\n"
           << "  struct _mulle_objc_protocol protocols[" << Protos.size() << "];\n"
           << "} " << ProtoListVar
           << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
           << "  " << Protos.size() << ",\n  {\n";
        for (auto *P : Protos) {
          uint32_t pid = MulleObjCUniqueIdHashForString(P->getNameAsString());
          OS << "    { (mulle_objc_protocolid_t) 0x";
          OS.write_hex(pid);
          OS << "U, \"" << P->getNameAsString() << "\" },\n";
        }
        OS << "  }\n};\n";
      }
    }

    OS << "static struct _mulle_objc_loadcategory " << VarBase
       << " __attribute__((used,section(\".data.objc.objc_load_info\"))) = {\n"
       << "  (mulle_objc_categoryid_t) 0x"; OS.write_hex(catId);
    OS << "U,\n  \"" << CatName << "\",\n"
       << "  (mulle_objc_classid_t) 0x"; OS.write_hex(classId);
    OS << "U,\n  \"" << ClassName << "\",\n"
       << "  (mulle_objc_hash_t) 0x"; OS.write_hex(classIvarHash);
    OS << "U,\n"
       << "  " << (CMethods.empty()     ? "0" : "(struct _mulle_objc_methodlist *)&"   + CMethodListVar) << ",\n"
       << "  " << (IMethods.empty()     ? "0" : "(struct _mulle_objc_methodlist *)&"   + IMethodListVar) << ",\n"
       << "  0,\n"  // properties
       << "  " << (ProtoListVar.empty() ? "0" : "(struct _mulle_objc_protocollist *)&" + ProtoListVar)   << ",\n"
       << "  0, 0\n"  // protocolclassids, origin
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
