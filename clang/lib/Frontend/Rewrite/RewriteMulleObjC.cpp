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

// Defined in ExprConstant.cpp — same pattern as CGObjCMulleRuntime.cpp
extern "C" uint32_t MulleObjCUniqueIdHashForString(std::string s);

namespace {

class RewriteMulleObjC : public ASTConsumer {
  Rewriter                    Rewrite;
  DiagnosticsEngine          &Diags;
  const LangOptions          &LangOpts;
  ASTContext                 *Context = nullptr;
  SourceManager              *SM      = nullptr;
  std::unique_ptr<raw_ostream> OutFile;
  std::string                 InFileName;

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
  void RewriteCategoryDecl(ObjCCategoryDecl *D);
  void RewriteCategoryImplDecl(ObjCCategoryImplDecl *D);
  void RewriteProtocolDecl(ObjCProtocolDecl *D);

  // ObjC expression/statement handlers
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
  TodoComment(D->getSourceRange(), "ObjCInterfaceDecl");
}

void RewriteMulleObjC::RewriteImplementationDecl(ObjCImplementationDecl *D) {
  // Rewrite method bodies
  for (auto *M : D->methods())
    if (M->hasBody())
      RewriteStmt(M->getBody());
  TodoComment(D->getSourceRange(), "ObjCImplementationDecl");
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

void RewriteMulleObjC::RewriteMessageExpr(ObjCMessageExpr *E) {
  TodoComment(E->getSourceRange(), "ObjCMessageExpr");
}

void RewriteMulleObjC::RewriteStringLiteral(ObjCStringLiteral *E) {
  TodoComment(E->getSourceRange(), "ObjCStringLiteral");
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
