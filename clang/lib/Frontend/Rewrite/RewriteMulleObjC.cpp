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
  bool                        InMethod = false; // for return-cast gating

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

    if (const RewriteBuffer *Buf =
            Rewrite.getRewriteBufferFor(SM->getMainFileID()))
      *OutFile << std::string(Buf->begin(), Buf->end());
    else {
      // No rewrites — emit original source unchanged
      StringRef MainBuf = SM->getBufferData(SM->getMainFileID());
      *OutFile << MainBuf;
    }
    OutFile->flush();
  }

private:
  void HandleTopLevelSingleDecl(Decl *D);
  void RewriteStmt(Stmt *S);

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

  // ObjC expression/statement handlers
  void RewriteReturnStmt(ReturnStmt *S);
  void RewriteMessageExpr(ObjCMessageExpr *E);
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
}

// ---------------------------------------------------------------------------
// ObjC declaration implementations
// ---------------------------------------------------------------------------

void RewriteMulleObjC::RewriteForwardClassDecl(ObjCInterfaceDecl *D) {
  // @class Foo;  ->  typedef struct Foo Foo;
  std::string S = "typedef struct ";
  S += D->getNameAsString();
  S += " ";
  S += D->getNameAsString();
  // Extend range to include the trailing semicolon if present
  SourceLocation End = Lexer::findLocationAfterToken(
      D->getEndLoc(), tok::semi, *SM, LangOpts, /*SkipTrailingWhitespace=*/false);
  SourceRange R = End.isValid()
      ? SourceRange(D->getBeginLoc(), End.getLocWithOffset(-1))
      : D->getSourceRange();
  ReplaceText(R, S + ";");
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
}

void RewriteMulleObjC::RewriteImplementationDecl(ObjCImplementationDecl *D) {
  // First rewrite all method bodies in-place
  for (auto *M : D->methods())
    if (M->hasBody())
      RewriteMethodDecl(M, D);

  // Now erase the @implementation header line.
  // D->getAtStartLoc() is '@', D->getLocation() is the class name.
  // We want to erase from '@' to the newline after the class name.
  // Find the first method's start (or @end if no methods).
  SourceLocation EraseEnd;
  for (auto *M : D->methods()) {
    if (M->hasBody()) { EraseEnd = M->getBeginLoc(); break; }
  }
  if (EraseEnd.isValid()) {
    // Erase from @implementation up to (not including) first method
    unsigned Len = SM->getFileOffset(EraseEnd) - SM->getFileOffset(D->getAtStartLoc());
    Rewrite.ReplaceText(D->getAtStartLoc(), Len, "");
  }

  // Erase @end — D->getEndLoc() points to '@' of '@end' (4 chars)
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

  if (RD && M->param_size() > 1) {
    // Multi-param: _param points to a struct
    std::string StructName = CName + "_t";
    llvm::raw_string_ostream SDef(ParamStructDef);
    SDef << "struct " << StructName << " { ";
    for (auto *FD : RD->fields())
      SDef << PrintType(FD->getType()) << " " << FD->getNameAsString() << "; ";
    SDef << "};\n";

    llvm::raw_string_ostream UOS(Unpack);
    for (auto *FD : RD->fields())
      UOS << "  " << PrintType(FD->getType()) << " " << FD->getNameAsString()
          << " = ((struct " << StructName << " *)_param)->"
          << FD->getNameAsString() << ";\n";

  } else if (M->param_size() == 1) {
    // Single-param: _param IS a pointer to the single value
    auto *P = *M->param_begin();
    llvm::raw_string_ostream UOS(Unpack);
    UOS << "  " << PrintType(P->getType()) << " " << P->getNameAsString()
        << " = *(" << PrintType(P->getType()) << " *)_param;\n";
  }

  // Build the C function signature with __asm__ for the ObjC name
  std::string Sig;
  llvm::raw_string_ostream SigOS(Sig);
  SigOS << "static void *" << CName
        << "(" << CD->getNameAsString() << " *self, mulle_objc_methodid_t _cmd, void *_param)"
        << " __asm__(\"" << ObjCName << "\");\n"
        << "static void *" << CName
        << "(" << CD->getNameAsString() << " *self, mulle_objc_methodid_t _cmd, void *_param)\n";

  // Rewrite inner ObjC expressions in the body first
  InMethod = true;
  RewriteStmt(M->getBody());
  InMethod = false;
  std::string BodyText = Rewrite.getRewrittenText(M->getBody()->getSourceRange());

  // Inject unpack locals after opening brace
  std::string FuncBody;
  if (!Unpack.empty() && !BodyText.empty() && BodyText[0] == '{')
    FuncBody = "{\n" + Unpack + BodyText.substr(1);
  else
    FuncBody = BodyText;

  std::string Full = ParamStructDef + Sig + FuncBody;

  SourceRange MRange(M->getBeginLoc(), M->getBody()->getEndLoc());
  Rewrite.ReplaceText(MRange.getBegin(), Rewrite.getRangeSize(MRange), Full);
}

void RewriteMulleObjC::RewriteCategoryDecl(ObjCCategoryDecl *D) {
  TodoComment(D->getSourceRange(), "ObjCCategoryDecl");
}

void RewriteMulleObjC::RewriteCategoryImplDecl(ObjCCategoryImplDecl *D) {
  for (auto *M : D->methods())
    if (M->hasBody())
      RewriteStmt(M->getBody());
  TodoComment(D->getSourceRange(), "ObjCCategoryImplDecl");
}

void RewriteMulleObjC::RewriteProtocolDecl(ObjCProtocolDecl *D) {
  TodoComment(D->getSourceRange(), "ObjCProtocolDecl");
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

  // Receiver
  std::string Receiver;
  if (E->getReceiverKind() == ObjCMessageExpr::Instance) {
    Receiver = Rewrite.getRewrittenText(E->getInstanceReceiver()->getSourceRange());
  } else {
    // class message
    Receiver = E->getClassReceiver()->castAs<ObjCObjectType>()->getInterface()->getNameAsString();
  }

  // Selector hash
  std::string SelStr = E->getSelector().getAsString();
  uint32_t Hash = MulleObjCUniqueIdHashForString(SelStr);
  std::string HashStr;
  llvm::raw_string_ostream HOS(HashStr);
  HOS << "0x"; HOS.write_hex(Hash); HOS << "UL";

  unsigned NumArgs = E->getNumArgs();

  if (NumArgs == 0) {
    // mulle_objc_object_call(obj, hash, NULL)
    OS << "mulle_objc_object_call(" << Receiver << ", "
       << "(mulle_objc_methodid_t) " << HashStr << ", NULL)";
  } else if (NumArgs == 1) {
    // single arg: pass pointer to the arg value
    // mulle_objc_object_call(obj, hash, &(type){ arg })
    QualType ArgTy = E->getArg(0)->getType();
    std::string ArgText = Rewrite.getRewrittenText(E->getArg(0)->getSourceRange());
    OS << "mulle_objc_object_call(" << Receiver << ", "
       << "(mulle_objc_methodid_t) " << HashStr << ", "
       << "&(" << PrintType(ArgTy) << "){ " << ArgText << " })";
  } else {
    // multi-arg: compound literal struct
    // mulle_objc_object_call(obj, hash, &(struct { t0 a0; t1 a1; }){ a0, a1 })
    OS << "mulle_objc_object_call(" << Receiver << ", "
       << "(mulle_objc_methodid_t) " << HashStr << ", "
       << "&(struct { ";
    Selector Sel = E->getSelector();
    for (unsigned i = 0; i < NumArgs; ++i) {
      OS << PrintType(E->getArg(i)->getType()) << " "
         << Sel.getNameForSlot(i).str() << "_; ";
    }
    OS << "}){ ";
    for (unsigned i = 0; i < NumArgs; ++i) {
      if (i) OS << ", ";
      OS << Rewrite.getRewrittenText(E->getArg(i)->getSourceRange());
    }
    OS << " })";
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
    OS << "((id) 0x";
    OS.write_hex(value);
    OS << "ULL) /* @\"" << Str << "\" */";
  } else {
    // Fall back to static struct: { MULLE_OBJC_NEVER_RELEASE, NULL, "str", len }
    // Emit a compound literal that matches the NSConstantString layout
    OS << "((id) &(struct { intptr_t rc; void *isa; const char *str; unsigned len; })"
       << "{ (intptr_t) 0x" ;
    OS.write_hex((uint64_t)(INTPTR_MAX - 1));
    OS << ", 0, \"";
    // Escape the string
    for (char c : Str) {
      if (c == '"' || c == '\\') OS << '\\';
      OS << c;
    }
    OS << "\", " << Len << " }) /* @\"" << Str << "\" */";
  }

  ReplaceText(E->getSourceRange(), OS.str());
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<ASTConsumer>
clang::CreateMulleObjCRewriter(const std::string &InFile,
                               std::unique_ptr<raw_ostream> OS,
                               DiagnosticsEngine &Diags,
                               const LangOptions &LOpts,
                               bool SilenceRewriteMacroWarning) {
  return std::make_unique<RewriteMulleObjC>(InFile, std::move(OS), Diags,
                                            LOpts, SilenceRewriteMacroWarning);
}
