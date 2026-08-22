// @mulle-objc@ -emit-nh implementation >
//===--- MulleNHEmitter.cpp - nuobjc .nh interface file emitter -----------===//
//
// Emits a .nh file describing ObjC classes, protocols, and vetted C functions
// from a parsed translation unit. Used by the nuobjc compiler to learn about
// ObjC frameworks without hand-written .nh files.
//
// Usage: mulle-clang -emit-nh -o Foo.nh <include-flags> Foo.imports.h
//
// Output format:
//   @header { ... }   — vetted C function prototypes (value-only tier)
//   @class Name : Super <Proto, ...>
//   - (RetType) methodName( kwarg:(Type) param, ... )
//   @end
//   @protocol Name <Inherited, ...>
//   ...
//   @end
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Type.h"
#include "clang/Basic/Builtins.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

using namespace clang;

namespace {

//===----------------------------------------------------------------------===//
// Type classification (reduced tier model, v1)
//===----------------------------------------------------------------------===//

/// Returns true if T is a "value type" suitable for emission in v1:
/// integers, floats, enums, ObjC object pointers, SEL, Class,
/// and structs/unions that are transitively value-only.
static bool isValueType(QualType T, const ASTContext &Ctx) {
  T = T.getCanonicalType();

  // Void is allowed as a return type.
  if (T->isVoidType())
    return true;

  // All builtin scalar types: int, float, double, BOOL, size_t, ...
  if (T->isBuiltinType())
    return true;

  // Enums (NSComparisonResult, UIViewAutoresizing, ...)
  if (T->isEnumeralType())
    return true;

  // ObjC object pointers: id, NSString *, Class, etc.
  if (T->isObjCObjectPointerType())
    return true;

  // SEL (may desugar to a pointer in some configs)
  if (T->isObjCSelType())
    return true;

  // Struct/union passed by value — recursively check all fields.
  if (const auto *RT = T->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    if (!RD || !RD->getDefinition())
      return false; // incomplete struct → reject
    for (const FieldDecl *F : RD->getDefinition()->fields()) {
      if (!isValueType(F->getType(), Ctx))
        return false;
    }
    return true;
  }

  // Everything else: C pointers, block pointers, function pointers → reject.
  return false;
}

//===----------------------------------------------------------------------===//
// Type printing
//===----------------------------------------------------------------------===//

static std::string printType(QualType T, const ASTContext &Ctx) {
  PrintingPolicy PP(Ctx.getLangOpts());
  PP.SuppressTagKeyword = true; // "CGRect" not "struct CGRect"
  return T.getAsString(PP);
}

//===----------------------------------------------------------------------===//
// Selector → dot-call reversal
//===----------------------------------------------------------------------===//

/// Emit a method declaration in nuobjc .nh syntax.
/// Unary:    - (NSUInteger) length
/// Keyword:  - (void) set( title:(NSString *) t, forDay:(int) d)
static void emitMethod(const ObjCMethodDecl *M, const ASTContext &Ctx,
                       raw_ostream &OS) {
  // Skip property accessors — properties are emitted separately if needed.
  // For v1 we emit all methods including accessors since nuobjc uses method
  // tables for argument reordering.

  // Skip variadic methods
  if (M->isVariadic())
    return;

  // Skip methods with non-value parameter or return types
  if (!isValueType(M->getReturnType(), Ctx))
    return;
  for (const ParmVarDecl *P : M->parameters()) {
    if (!isValueType(P->getType(), Ctx))
      return;
  }

  OS << (M->isClassMethod() ? "+ " : "- ");
  OS << "(" << printType(M->getReturnType(), Ctx) << ") ";

  Selector Sel = M->getSelector();
  unsigned NumArgs = Sel.getNumArgs();

  if (NumArgs == 0) {
    // Unary selector: - (NSUInteger) length
    OS << Sel.getNameForSlot(0).str();
  } else {
    // Keyword selector: first part is method name, rest are keyword args.
    // setTitle:forDay: → set( title:(NSString *) t, forDay:(int) d)
    //
    // The nuobjc convention:
    //   - First keyword part with trailing colon stripped becomes the method name.
    //   - If the first keyword matches the pattern "xxxYyy:" where the part after
    //     lowercasing the first char of the suffix gives the first arg label,
    //     we split it. But for v1, we use the full first keyword as the method
    //     name and the first param as an anonymous arg.
    //
    // Actually, the nuobjc dot-call convention is:
    //   obj->setTitle( t, forDay: d)
    // which in .nh declaration becomes:
    //   - (void) setTitle( :(NSString *) t, forDay:(int) d)
    //
    // The first keyword slot is the method name, first param is anonymous (:),
    // subsequent slots are named args.

    StringRef FirstSlot = Sel.getNameForSlot(0);
    OS << FirstSlot.str() << "( ";

    for (unsigned i = 0; i < NumArgs; ++i) {
      if (i > 0)
        OS << ", ";

      if (i == 0) {
        // First parameter: anonymous (just ":(Type) name")
        OS << ":";
      } else {
        // Subsequent parameters: "keyword:(Type) name"
        OS << Sel.getNameForSlot(i).str() << ":";
      }

      const ParmVarDecl *P = M->parameters()[i];
      OS << "(" << printType(P->getType(), Ctx) << ")";
      StringRef PName = P->getName();
      if (!PName.empty())
        OS << " " << PName.str();
    }
    OS << ")";
  }
  OS << "\n";
}

//===----------------------------------------------------------------------===//
// Class / Protocol / Function emission
//===----------------------------------------------------------------------===//

static void emitClassDecl(const ObjCInterfaceDecl *ID, const ASTContext &Ctx,
                          raw_ostream &OS) {
  // Only emit if this is the definition (not a forward decl).
  if (!ID->isThisDeclarationADefinition())
    return;

  OS << "@class " << ID->getNameAsString();

  // Superclass
  if (const ObjCInterfaceDecl *Super = ID->getSuperClass())
    OS << " : " << Super->getNameAsString();

  // Protocols
  auto Protos = ID->protocols();
  if (Protos.begin() != Protos.end()) {
    OS << " <";
    bool First = true;
    for (const ObjCProtocolDecl *P : Protos) {
      if (!First)
        OS << ", ";
      OS << P->getNameAsString();
      First = false;
    }
    OS << ">";
  }
  OS << "\n";

  // Methods from the class itself
  for (const ObjCMethodDecl *M : ID->methods())
    emitMethod(M, Ctx, OS);

  // Methods from categories (folded into the class)
  for (const ObjCCategoryDecl *Cat : ID->known_categories()) {
    for (const ObjCMethodDecl *M : Cat->methods())
      emitMethod(M, Ctx, OS);
  }

  OS << "@end\n\n";
}

static void emitProtocolDecl(const ObjCProtocolDecl *PD, const ASTContext &Ctx,
                             raw_ostream &OS) {
  if (!PD->isThisDeclarationADefinition())
    return;

  OS << "@protocol " << PD->getNameAsString();

  // Inherited protocols
  auto Protos = PD->protocols();
  if (Protos.begin() != Protos.end()) {
    OS << " <";
    bool First = true;
    for (const ObjCProtocolDecl *P : Protos) {
      if (!First)
        OS << ", ";
      OS << P->getNameAsString();
      First = false;
    }
    OS << ">";
  }
  OS << "\n";

  for (const ObjCMethodDecl *M : PD->methods())
    emitMethod(M, Ctx, OS);

  OS << "@end\n\n";
}

/// Emit a C function prototype if it passes the reduced tier model.
static bool emitFunctionDecl(const FunctionDecl *FD, const ASTContext &Ctx,
                             raw_ostream &OS) {
  // Skip variadic functions
  if (FD->isVariadic())
    return false;

  // Skip non-external (static, inline-only) functions
  if (!FD->isExternallyVisible())
    return false;

  // Skip compiler builtins (but allow library builtins like cos, sin, sqrt)
  unsigned BuiltinID = FD->getBuiltinID();
  if (BuiltinID != 0) {
    const auto &BI = Ctx.BuiltinInfo;
    if (!BI.isLibFunction(BuiltinID) && !BI.isPredefinedLibFunction(BuiltinID))
      return false;
  }

  // Check return type
  if (!isValueType(FD->getReturnType(), Ctx))
    return false;

  // Check all parameters
  for (const ParmVarDecl *P : FD->parameters()) {
    if (!isValueType(P->getType(), Ctx))
      return false;
  }

  // Emit: "RetType FuncName( Type1, Type2, ...);"
  OS << printType(FD->getReturnType(), Ctx) << " " << FD->getNameAsString()
     << "(";
  bool First = true;
  for (const ParmVarDecl *P : FD->parameters()) {
    if (!First)
      OS << ", ";
    OS << " " << printType(P->getType(), Ctx);
    First = false;
  }
  if (FD->param_empty())
    OS << " void";
  OS << ");\n";
  return true;
}

//===----------------------------------------------------------------------===//
// ASTConsumer
//===----------------------------------------------------------------------===//

class MulleNHEmitter : public ASTConsumer {
  CompilerInstance &CI;
  raw_ostream &OS;

public:
  MulleNHEmitter(CompilerInstance &CI, raw_ostream &OS) : CI(CI), OS(OS) {}

  void HandleTranslationUnit(ASTContext &Ctx) override {
    // First pass: collect C functions into an @header block.
    std::string FuncBuf;
    llvm::raw_string_ostream FuncOS(FuncBuf);
    for (const Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
      if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
        // Only emit the first declaration (avoid duplicates from redecls).
        if (FD->isFirstDecl())
          emitFunctionDecl(FD, Ctx, FuncOS);
      }
    }

    // Emit @header block if we have any functions.
    if (!FuncBuf.empty()) {
      OS << "@header\n{\n" << FuncBuf << "}\n\n";
    }

    // Second pass: emit classes and protocols.
    for (const Decl *D : Ctx.getTranslationUnitDecl()->decls()) {
      if (const auto *ID = dyn_cast<ObjCInterfaceDecl>(D))
        emitClassDecl(ID, Ctx, OS);
      else if (const auto *PD = dyn_cast<ObjCProtocolDecl>(D))
        emitProtocolDecl(PD, Ctx, OS);
    }
  }
};

} // anonymous namespace

//===----------------------------------------------------------------------===//
// FrontendAction implementation
//===----------------------------------------------------------------------===//

std::unique_ptr<ASTConsumer>
MulleNHEmitAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  // Output goes to -o file, or stdout if no -o.
  raw_ostream *OS = &llvm::outs();
  std::unique_ptr<raw_ostream> OwnedOS;

  if (!CI.getFrontendOpts().OutputFile.empty()) {
    std::error_code EC;
    auto FileOS = std::make_unique<llvm::raw_fd_ostream>(
        CI.getFrontendOpts().OutputFile, EC, llvm::sys::fs::OF_Text);
    if (EC) {
      CI.getDiagnostics().Report(diag::err_fe_unable_to_open_output)
          << CI.getFrontendOpts().OutputFile << EC.message();
      return nullptr;
    }
    OS = FileOS.get();
    OwnedOS = std::move(FileOS);
  }

  // We need the stream to outlive the consumer. Stash it in a shared_ptr
  // captured by a custom consumer wrapper, or just use a static approach.
  // For simplicity, store the owned stream in the CI's output manager.
  // Actually, the cleanest way: return a consumer that holds the stream.
  //
  // The consumer destructor will flush and close the stream.
  class MulleNHEmitterOwning : public ASTConsumer {
    std::unique_ptr<raw_ostream> OwnedOS;
    MulleNHEmitter Inner;
  public:
    MulleNHEmitterOwning(CompilerInstance &CI,
                         std::unique_ptr<raw_ostream> OS,
                         raw_ostream &ActualOS)
        : OwnedOS(std::move(OS)), Inner(CI, ActualOS) {}
    void HandleTranslationUnit(ASTContext &Ctx) override {
      Inner.HandleTranslationUnit(Ctx);
    }
  };

  if (OwnedOS) {
    raw_ostream &Ref = *OwnedOS;
    return std::make_unique<MulleNHEmitterOwning>(CI, std::move(OwnedOS), Ref);
  }
  // stdout case — no ownership needed.
  return std::make_unique<MulleNHEmitter>(CI, *OS);
}
// @mulle-objc@ -emit-nh implementation <
