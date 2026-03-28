// @mulle-objc@ --mulle-objc-emit-deps implementation >
//===--- MulleObjCDepsEmitter.cpp - .deps.inc sidecar writer --------------===//
//
// Emits a <basename>.deps.inc file for each compiled translation unit when
// --mulle-objc-emit-deps[=<dir>] is set. Works with any frontend action
// (normal compile, rewrite, syntax-only, etc.) via CreateWrappedASTConsumer.
//
// Output format: one array-element initializer per @implementation, suitable
// for #include inside a struct initializer, matching the objc-deps.inc
// convention used by mulle-objc build infrastructure.
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/FrontendOptions.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

extern "C" uint32_t MulleObjCUniqueIdHashForString(std::string s);

namespace clang {

/// Format a uint32_t as an 8-digit lowercase hex string.
static std::string hexId(uint32_t v) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%08x", v);
  return buf;
}

/// ASTConsumer that collects all @implementation decls and writes a
/// .deps.inc sidecar file in HandleTranslationUnit.
class MulleObjCDepsEmitter : public ASTConsumer {
  CompilerInstance &CI;

  struct Entry {
    std::string ClassName;
    std::string CategoryName; // empty for plain class / protocolclass impls
  };
  std::vector<Entry> Entries;

public:
  explicit MulleObjCDepsEmitter(CompilerInstance &CI) : CI(CI) {}

  bool HandleTopLevelDecl(DeclGroupRef DG) override {
    for (Decl *D : DG) {
      if (auto *Impl = dyn_cast<ObjCImplementationDecl>(D)) {
        Entries.push_back({Impl->getClassInterface()->getNameAsString(), ""});
      } else if (auto *Cat = dyn_cast<ObjCCategoryImplDecl>(D)) {
        Entries.push_back({Cat->getClassInterface()->getNameAsString(),
                           Cat->getNameAsString()});
      }
    }
    return true;
  }

  void HandleTranslationUnit(ASTContext &) override {
    // Determine output directory: explicit dir > output file dir > "."
    const FrontendOptions &FOpts = CI.getFrontendOpts();
    std::string Dir = FOpts.MulleObjCEmitDepsDir;
    if (Dir.empty()) {
      if (!FOpts.OutputFile.empty())
        Dir = llvm::sys::path::parent_path(FOpts.OutputFile).str();
      if (Dir.empty())
        Dir = ".";
    }

    // Build output path: <dir>/<source-basename>.deps.inc
    StringRef InputPath;
    if (!FOpts.Inputs.empty())
      InputPath = FOpts.Inputs[0].getFile();
    llvm::SmallString<256> OutPath(Dir);
    llvm::sys::path::append(OutPath,
        llvm::sys::path::stem(InputPath.empty() ? "output" : InputPath));
    OutPath += ".deps.inc";

    std::error_code EC;
    llvm::raw_fd_ostream OS(OutPath, EC, llvm::sys::fs::OF_Text);
    if (EC) {
      CI.getDiagnostics().Report(diag::err_fe_unable_to_open_output)
          << OutPath << EC.message();
      return;
    }

    // Leading comment: show the source path relative to the common ancestor
    // of the input and output directory, so consumers see e.g. "src/main.m".
    // Walk up from Dir until the relative input path has at least one
    // directory component (i.e. more context than just the bare filename).
    if (!InputPath.empty()) {
      std::string relInput;
      llvm::SmallString<256> Base(Dir);
      for (;;) {
        llvm::sys::path::remove_filename(Base);
        if (Base.empty() || Base == "/")
          break;
        StringRef baseRef(Base);
        if (InputPath.starts_with(baseRef) &&
            InputPath.size() > baseRef.size() &&
            InputPath[baseRef.size()] == '/') {
          std::string rel = InputPath.drop_front(baseRef.size() + 1).str();
          relInput = rel; // keep updating; last wins (deepest w/ dir component)
          if (!llvm::sys::path::parent_path(rel).empty())
            break; // found a rel path with a directory part — done
        }
      }
      if (relInput.empty())
        relInput = llvm::sys::path::filename(InputPath).str();
      OS << "// " << relInput << "\n";
    }

    for (const Entry &E : Entries) {
      std::string cid = hexId(MulleObjCUniqueIdHashForString(E.ClassName));
      if (E.CategoryName.empty()) {
        OS << "      { @selector( " << E.ClassName
           << "), MULLE_OBJC_NO_CATEGORYID },      // "
           << cid << ";" << E.ClassName << ";;\n";
      } else {
        std::string catid = hexId(MulleObjCUniqueIdHashForString(E.CategoryName));
        OS << "      { @selector( " << E.ClassName
           << "), @selector( " << E.CategoryName << ") },      // "
           << cid << ";" << E.ClassName << ";"
           << catid << ";" << E.CategoryName << "\n";
      }
    }
    // Empty TU: file intentionally left empty (zero bytes).
  }
};

std::unique_ptr<ASTConsumer>
CreateMulleObjCDepsEmitter(CompilerInstance &CI) {
  return std::make_unique<MulleObjCDepsEmitter>(CI);
}

} // namespace clang
// @mulle-objc@ --mulle-objc-emit-deps implementation <
