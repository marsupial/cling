//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "ValueExtractionSynthesizer.h"

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/Utils/AST.h"
#include "cling/Utils/Output.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"

#include <stdarg.h>

using namespace clang;

namespace cling {
  ValueExtractionSynthesizer::ValueExtractionSynthesizer(clang::Sema* S,
                                                         Interpreter* I)
      : WrapperTransformer(S), m_Parent(I),
        m_ValueSynth(nullptr) {}

  // pin the vtable here.
  ValueExtractionSynthesizer::~ValueExtractionSynthesizer() { }

  namespace {
    class ReturnStmtCollector : public StmtVisitor<ReturnStmtCollector> {
    private:
      llvm::SmallVectorImpl<Stmt**>& m_Stmts;
    public:
      ReturnStmtCollector(llvm::SmallVectorImpl<Stmt**>& S)
        : m_Stmts(S) {}

      void VisitStmt(Stmt* S) {
        for(Stmt::child_iterator I = S->child_begin(), E = S->child_end();
            I != E; ++I) {
          if (!*I)
            continue;
          if (isa<LambdaExpr>(*I))
            continue;
          Visit(*I);
          if (isa<ReturnStmt>(*I))
            m_Stmts.push_back(&*I);
        }
      }
    };
  }

  ASTTransformer::Result ValueExtractionSynthesizer::Transform(clang::Decl* D) {
    const CompilationOptions& CO = getCompilationOpts();
    // If we do not evaluate the result, or printing out the result return.
    if (!(CO.ResultEvaluation || CO.ValuePrinting))
      return Result(D, true);

    FunctionDecl* FD = cast<FunctionDecl>(D);
    assert(utils::Analyze::IsWrapper(FD) && "Expected wrapper");

    int foundAtPos = -1;
    Expr* lastExpr = utils::Analyze::GetOrCreateLastExpr(FD, &foundAtPos,
                                                         /*omitDS*/false,
                                                         m_Sema);
    if (foundAtPos < 0)
      return Result(D, true);

    typedef llvm::SmallVector<Stmt**, 4> StmtIters;
    StmtIters returnStmts;
    ReturnStmtCollector collector(returnStmts);
    CompoundStmt* CS = cast<CompoundStmt>(FD->getBody());
    collector.VisitStmt(CS);

    if (isa<Expr>(*(CS->body_begin() + foundAtPos)))
      returnStmts.push_back(CS->body_begin() + foundAtPos);

    // We want to support cases such as:
    // gCling->evaluate("if() return 'A' else return 12", V), that puts in V,
    // either A or 12.
    // In this case the void wrapper is compiled with the stmts returning
    // values. Sema would cast them to void, but the code will still be
    // executed. For example:
    // int g(); void f () { return g(); } will still call g().
    //
    for (StmtIters::iterator I = returnStmts.begin(), E = returnStmts.end();
         I != E; ++I) {
      ReturnStmt* RS = dyn_cast<ReturnStmt>(**I);
      if (RS) {
        // When we are handling a return stmt, the last expression must be the
        // return stmt value. Ignore the calculation of the lastStmt because it
        // might be wrong, in cases where the return is not in the end of the
        // function.
        lastExpr = RS->getRetValue();
        if (lastExpr) {
          assert (lastExpr->getType()->isVoidType() && "Must be void type.");
          // Any return statement will have been "healed" by Sema
          // to correspond to the original void return type of the
          // wrapper, using a ImplicitCastExpr 'void' <ToVoid>.
          // Remove that.
          if (ImplicitCastExpr* VoidCast
              = dyn_cast<ImplicitCastExpr>(lastExpr)) {
            lastExpr = VoidCast->getSubExpr();
          }
        }
        // if no value assume void
        else {
          // We can't PushDeclContext, because we don't have scope.
          Sema::ContextRAII pushedDC(*m_Sema, FD);
          RS->setRetValue(SynthesizeSVRInit(0));
        }

      }
      else
        lastExpr = cast<Expr>(**I);

      ASTContext& AST = m_Parent->getSema().getASTContext();
      if (lastExpr) {
        QualType lastExprTy = lastExpr->getType();
        // May happen on auto types which resolve to dependent.
        if (lastExprTy->isDependentType())
          continue;
        // Set up lastExpr properly.
        // Change the void function's return type
        // We can't PushDeclContext, because we don't have scope.
        Sema::ContextRAII pushedDC(*m_Sema, FD);

        if (lastExprTy->isFunctionType()) {
          // A return type of function needs to be converted to
          // pointer to function.
          lastExprTy = AST.getPointerType(lastExprTy);
          lastExpr = m_Sema->ImpCastExprToType(lastExpr, lastExprTy,
                                               CK_FunctionToPointerDecay,
                                               VK_RValue).get();
        }

        //
        // Here we don't want to depend on the JIT runFunction, because of its
        // limitations, when it comes to return value handling. There it is
        // not clear who provides the storage and who cleans it up in a
        // platform independent way. Depending on the type we need to
        // synthesize a call to cling:
        //
        // 0) void, set the cling::Value's type to void:
        //        cling_ValueExtraction(cling::kAssignType)
        //
        // 1) valid type for variadic function that can be stored in a Value
        //    (enum, integral, float, double, pointers, references):
        //      gCling->evaluate("1", OutValue); =>
        //        cling_ValueExtraction(cling::kAssignValue)
        //
        // 2) object type with stack allocation transformed to heap:
        //      gCling->evaluate("Type(Arg0, Arg1)", OutValue); =>
        //        Buf = cling_ValueExtraction(cling::kAssignType)
        //        ::operator new (Buf) ype(Arg0, Arg1)
        //
        // 3) object type with stack allocation for printing only, where the
        //    temporary object is materialized to allow for L-Value expression
        //    where it's address is passed as a variadic argument
        //      gCling->echo("Type(Arg0, Arg1)"); =>
        //        cling_ValueExtraction(kAssignValue|kMaterialized|kDumpValue)
        //

        // FIXME: Following is old, but what does/did it actually mean?
        // We need to synthesize later:
        // Wrapper has signature: void w(cling::Value SVR)
        // case 1):
        //   setValueNoAlloc(gCling, &SVR, lastExprTy, lastExpr())
        // case 2):
        //   new (setValueWithAlloc(gCling, &SVR, lastExprTy)) (lastExpr)

        Expr* SVRInit = SynthesizeSVRInit(lastExpr);
        // if we had return stmt update to execute the SVR init, even if the
        // wrapper returns void.
        if (SVRInit) {
          if (RS) {
            if (ImplicitCastExpr* VoidCast
                = dyn_cast<ImplicitCastExpr>(RS->getRetValue()))
              VoidCast->setSubExpr(SVRInit);
          }
          else {
            **I = SVRInit;
          }
        }
        else {
          // FIXME: Is this leaking anything (lastExpr?)
          return Result(D, false);
        }
      }
    }
    //FD->dump();
    return Result(D, true);
  }

// Helper function for the SynthesizeSVRInit
namespace {
  static bool availableCopyConstructor(QualType QT, clang::Sema* S) {
    // Check the the existance of the copy constructor the tha placement new will use.
    if (CXXRecordDecl* RD = QT->getAsCXXRecordDecl()) {
      // If it has a trivial copy constructor it is accessible and it is callable.
      if(RD->hasTrivialCopyConstructor()) return true;
      // Lookup the copy canstructor and check its accessiblity.
      if (CXXConstructorDecl* CD = S->LookupMovingConstructor(RD, QT.getCVRQualifiers())) {
        if (!CD->isDeleted() && CD->getAccess() == clang::AccessSpecifier::AS_public) {
          return true;
        }
      }
      return false;
    }
    return true;
  }

  static std::pair<QualType, QualType> makeLValues(const ASTContext& AST,
                                                   QualType A, QualType B) {
    return std::make_pair(AST.getLValueReferenceType(A),
                          AST.getLValueReferenceType(B));
  }

  enum {
    kAssignType   = 0,
    kAssignValue  = 1,
    kDumpValue    = 2,
    kMaterialized = 4,
  };
}

  Expr* ValueExtractionSynthesizer::SynthesizeSVRInit(Expr* E) {
    if (!m_ValueSynth && !FindAndCacheRuntimeDecls(E))
      return nullptr;

    assert((getCompilationOpts().ValuePrinting !=
               cling::CompilationOptions::VPAuto) &&
           "VPAuto must have been expanded earlier.");

    const ASTContext& AST = m_Sema->getASTContext();

    // We have the wrapper as Sema's CurContext
    FunctionDecl* FD = cast<FunctionDecl>(m_Sema->CurContext);

    
    bool Placement = false;
    VarDecl* VDecl = nullptr;
    DeclRefExpr* DRefEnd = nullptr;
    ExprWithCleanups* Cleanups = nullptr;
    const llvm::APInt* NElem = nullptr;

    // Build a reference to Value* in the wrapper, should be the only argument.
    SourceLocation Start, End;
    QualType ETy, ElemTy;

    const bool StoreResult = getCompilationOpts().ResultEvaluation;
    const bool Printing = getCompilationOpts().ValuePrinting;

    if (E) {
      if (isa<ExprWithCleanups>(E)) {
        // In case of ExprWithCleanups we need to extend its 'scope' to the call
        Cleanups = cast<ExprWithCleanups>(E);
        E = Cleanups->getSubExpr();
      }
      if (StoreResult) {
        if ((DRefEnd = dyn_cast<DeclRefExpr>(E))) {
          // Try to xform VarDecl statements of temporaries into heap objs.
          // evaluate('X') -> 'return new(cling_ValueExtraction(args)) X'
          //
          // This avoid unneccessary copies and issues with THING having a private
          // or deleted copy and/or move constructors
          //
          if (VarDecl* VD = dyn_cast<VarDecl>(DRefEnd->getDecl())) {
            // Invalid decl, code won't run
            if (VD->isInvalidDecl())
              return E;
            if (!dyn_cast_or_null<CXXNewExpr>(VD->getInit())) {
              if (CompoundStmt* CS =
                      dyn_cast_or_null<CompoundStmt>(FD->getBody())) {
                if (DeclStmt* DS =
                        dyn_cast_or_null<DeclStmt>(CS->body_front())) {
                  // FIXME: Is this really necessary?
                  // Should be checking isInvalidDecl to exit early?
                  for (Decl* D : DS->getDeclGroup()) {
                    if (dyn_cast<VarDecl>(D) == VD) {
                      VDecl = VD;
                      Placement = true;
                      break;
                    }
                  }
                }
              }
            }
          }
        } else if (ImplicitCastExpr* CE = dyn_cast<ImplicitCastExpr>(E)) {
          // Try to xform simple return statements of temporaries into heap objs.
          // 'return THING' -> 'return new(cling_ValueExtraction(args)) THING'.
          Expr* SbE = CE->getSubExpr();
          CXXBindTemporaryExpr* B = dyn_cast_or_null<CXXBindTemporaryExpr>(SbE);
          if (!B) {
            if (CXXFunctionalCastExpr* FC =
                    dyn_cast_or_null<CXXFunctionalCastExpr>(SbE)) {
              B = dyn_cast_or_null<CXXBindTemporaryExpr>(FC->getSubExpr());
            }
          }
          if (B) {
            E = B;
            E->setValueKind(VK_LValue);
            Placement = true;
          }
        }
      }

      Start = E->getLocStart();
      End = E->getLocEnd();
      ETy = E->getType();
      ElemTy = ETy;
      if (const clang::ConstantArrayType* ArrTy =
              llvm::dyn_cast<clang::ConstantArrayType>(ETy.getTypePtr())) {
        ElemTy = ArrTy->getElementType();
        NElem = &ArrTy->getSize();
      } else
        ElemTy = ETy;

    } else {
      // VOID
      Start = FD->getLocStart();
      End = FD->getLocEnd();
      ETy = AST.VoidTy;
    }

    QualType desugaredTy = ETy.getDesugaredType(AST);

    // Cache these to avoid redunadant isa<> lookups
    const bool IsVoid = desugaredTy->isVoidType();
    const bool IsRecord = !IsVoid && desugaredTy->isRecordType();
    const bool IsValue = IsRecord || desugaredTy->isMemberPointerType();
    const bool CPlusPlus = m_Sema->getLangOpts().CPlusPlus;

    int Action = IsVoid || IsValue || Placement
                     ? kAssignType
                     : kAssignValue | (Printing ? kDumpValue : 0);

    if (!IsVoid) {
      assert(E != nullptr && "Invalid Expression");
      // The expr result is transported as reference, pointer, array, float etc
      // based on the desugared type. We should still expose the typedef'ed
      // (sugared) type to the cling::Value.
      if ((IsRecord || Placement) && E->getValueKind() == VK_LValue) {
        if ((IsRecord || NElem) && VDecl) {
          // Local to Heap object/array, change type to a pointer.
          QualType PtrTy = desugaredTy = AST.getPointerType(ElemTy);
          VDecl->setType(PtrTy);
          DRefEnd->setType(PtrTy);
        } else if (VDecl || !Placement) {
          if (IsRecord) {
            // returning a lvalue (not a temporary): the value should contain
            // a reference to the lvalue instead of copying it.
            std::tie(desugaredTy, ETy) = makeLValues(AST, desugaredTy, ETy);
          }
          Placement = 0;
          Action = kAssignValue | ( Printing ? kDumpValue : 0);
        }
      } else if (ImplicitCastExpr* Cast = dyn_cast<ImplicitCastExpr>(E)) {
        // C-Style references to statics/globals: cling -x -c
        // [cling]$ struct Test ST = {};
        // [cling]$ ST
        if (Cast->getCastKind() == CK_LValueToRValue) {
          if (DeclRefExpr *DRef = dyn_cast<DeclRefExpr>(Cast->getSubExpr())) {
            ExprResult LVal = m_Sema->BuildDeclRefExpr(DRef->getDecl(),
                               AST.VoidPtrTy, VK_LValue, SourceLocation());
            if (LVal.isUsable()) {
              E = LVal.get();
              std::tie(desugaredTy, ETy) = makeLValues(AST, desugaredTy, ETy);
              if (Printing)
                Action = kAssignValue | kDumpValue;
            }
          }
        }
      } else if (!StoreResult && IsValue && Printing && CPlusPlus) {
        // Printing only, don't need to allocate into cling::Value
        // Materialize the Temporary object so the address can be taken.
        E = m_Sema->CreateMaterializeTemporaryExpr(E->getType(), E, true);
        std::tie(desugaredTy, ETy) = makeLValues(AST, desugaredTy, ETy);
        Action = kAssignValue | kMaterialized | kDumpValue;
      }
    }

    llvm::SmallVector<Expr*, 6> CallArgs;

    // Get a (DeclRef) reference to the cling::Value pointer
    ExprResult ValueDecl =
        m_Sema->BuildDeclRefExpr(FD->getParamDecl(0), AST.VoidPtrTy, VK_LValue,
                                 Start);
    Expr* ValueP = ValueDecl.get();

    // Pass the cling::Value pointer
    CallArgs.push_back(ValueP);

    // Pass the QualType as a void*
    if (IsVoid) {
      QualType VdP = AST.VoidPtrTy;
      QualType Vd = AST.VoidTy;
      CallArgs.push_back(utils::Synthesize::CStyleCastPtrExpr(m_Sema, VdP,
                                               uintptr_t(Vd.getAsOpaquePtr())));
    } else {
      CallArgs.push_back(utils::Synthesize::CStyleCastPtrExpr(m_Sema,
                         AST.VoidPtrTy, uintptr_t(ETy.getAsOpaquePtr())));
    }

    // Pass the Intepreter
    CallArgs.push_back(utils::Synthesize::CStyleCastPtrExpr(m_Sema,
                       AST.VoidPtrTy, uintptr_t(&m_Parent->ancestor())));

    // Pass on the action to take (assign, dump, etc).
    CallArgs.push_back(new (AST) IntegerLiteral(AST, llvm::APInt(32, Action),
                                                AST.IntTy, SourceLocation()));

    ExprResult Call;
    const SourceLocation Loc = Start;
    clang::Scope* Scope = nullptr;
    if (IsVoid) {
      assert(Action == kAssignType && "Bad Action");
      // In cases where the cling::Value gets reused we need to reset the
      // previous settings to void.
      // We need to synthesize setValueNoAlloc(...), E, because we still need
      // to run E.

      Call = m_Sema->ActOnCallExpr(Scope, m_ValueSynth, Start, CallArgs, End);

      if (E)
        Call = m_Sema->CreateBuiltinBinOp(Start, BO_Comma, Call.get(), E);
    }
    else if ((IsValue || Placement) && Action == kAssignType) {
      // temporary object types that need to be preserved:
      //   gCling->evaluate("CxxObj Obj", V)
      //   gCling->evaluate("CxxObj()", V)

      Expr* Init;

      // Make sure the call to operator new has everything needed
      // before calling cling_ValueExtraction to allocate the value.
      if (CPlusPlus) {
        if (CXXBindTemporaryExpr* Bind = dyn_cast<CXXBindTemporaryExpr>(E))
          Init = Bind->getSubExpr();
        else if (VDecl) {
          Init = VDecl->getInit();
          // VAL v[3] = { VAL(), VAL(), VAL() }
          if (ExprWithCleanups* EC = dyn_cast_or_null<ExprWithCleanups>(Init))
            Init = EC->getSubExpr();
        } else
          Init = dyn_cast<CXXTemporaryObjectExpr>(E);

        // Forward constructor arguments
        // Test(A, B, C) -> new (Ptr) Test(A, B, C)
        if (CXXConstructExpr* Ctor = dyn_cast_or_null<CXXConstructExpr>(Init)) {
          ParenListExpr* PExpr = nullptr;
          if (auto N = Ctor->getNumArgs()) {
            clang::Expr** Args = Ctor->getArgs();
            // Drop the default arguments, rebuilt in BuildCXXNew
            while (N && dyn_cast<CXXDefaultArgExpr>(Args[N-1]))
              --N;
            if (N) {
              PExpr =
                  new (AST) ParenListExpr(AST, Args[0]->getLocStart(),
                                          llvm::ArrayRef<clang::Expr*>(Args, N),
                                          Args[N - 1]->getLocEnd());
            }
          }
          if (!PExpr) {
            // No arguments, empty parenthesis
            PExpr = new (AST)
                ParenListExpr(AST, Loc, llvm::ArrayRef<clang::Expr*>(), Loc);
          }
          Init = PExpr;
        } else if (!Init || !isa<InitListExpr>(Init)) {
          // check existence of copy constructor before call
          if (IsRecord && !availableCopyConstructor(desugaredTy, m_Sema))
            return E;
          // Can we do CXXNewExpr::CallInit? (see Sema::BuildCXXNew)
          if (!E->getSourceRange().isValid())
            return E;
          Init = E;
        }
      }

      // Ptr = cling_ValueExtraction(gCling, &SVR, ETy);
      Call = m_Sema->ActOnCallExpr(Scope, m_ValueSynth, Start, CallArgs, End);

      if (CPlusPlus) {
        // call new (Ptr) (Init)
        assert(!Call.isInvalid() && "Invalid Call before building new");

        TypeSourceInfo* ETSI = AST.getTrivialTypeSourceInfo(ElemTy, Loc);
        SourceRange InitRange;
        if (Init && !isa<InitListExpr>(Init) && !isa<CXXConstructExpr>(Init))
          InitRange = Init->getSourceRange();
        IntegerLiteral* ArraySize =
            NElem ? new (AST) IntegerLiteral(AST, *NElem, AST.getSizeType(),
                                             SourceLocation())
                  : nullptr;
        
        SourceLocation ALoc;
        // Reuse the void* function parameter.
        // vpValue = cling_ValueExtraction()
        ExprResult Alloc =
            m_Sema->CreateBuiltinBinOp(ALoc, BO_Assign, ValueP, Call.get());

        // call new(vpValue) Object(Args)
        Call = m_Sema->BuildCXXNew(E->getSourceRange(),
                                   /*useGlobal ::*/ true,
                                   /*placementLParen*/ Loc,
                                   /*placementArgs*/MultiExprArg(ValueP),
                                   /*placementRParen*/ Loc,
                                   /*TypeIdParens*/ SourceRange(),
                                   /*allocType*/ ETSI->getType(),
                                   /*allocTypeInfo*/ ETSI,
                                   /*arraySize*/ ArraySize,
                                   /*directInitRange*/ InitRange,
                                   /*initializer*/Init);

        // Mark the constructor as succeeding. This is tied to cling::Value
        // AllocatedValue that delivers a pointer to a char buffer with the
        // element -1 as part of the object.
        //
        // ((char*)vpValue)[-1] = -1;

        Expr* Payload = utils::Synthesize::CStyleCastPtrExpr(
            m_Sema, AST.getPointerType(AST.CharTy), ValueP);
        IntegerLiteral* NegOne =
            new (AST) IntegerLiteral(AST, llvm::APInt(8, -1), AST.CharTy, ALoc);
        ArraySubscriptExpr* Subscript =
            new (AST) ArraySubscriptExpr(Payload, NegOne, AST.CharTy,
                                         VK_LValue, OK_Ordinary, ALoc);
        ExprResult Done =
            m_Sema->CreateBuiltinBinOp(ALoc, BO_Assign, Subscript, NegOne);

        llvm::SmallVector<Stmt*, 4> Mark;
        Mark.push_back(Alloc.get());
        Mark.push_back(Call.get());
        Mark.push_back(Done.get());
        if (!DRefEnd) {
          Mark.push_back(utils::Synthesize::CStyleCastPtrExpr(
            m_Sema, AST.getPointerType(ElemTy), ValueP));
        } else
          Mark.push_back(DRefEnd);

        m_Sema->ActOnStartOfCompoundStmt();
        Stmt* Stmt = m_Sema->ActOnCompoundStmt(ALoc, ALoc, Mark, false).get();
        m_Sema->ActOnFinishOfCompoundStmt();
        m_Sema->ActOnStartStmtExpr();
        Call  = m_Sema->ActOnStmtExpr(ALoc, Stmt, ALoc);

      }
      if (Call.isInvalid()) {
        m_Sema->Diag(E->getLocStart(), diag::err_unsupported_unknown_any_expr);
        return Call.get();
      }

      // Handle possible cleanups:
      Call = m_Sema->ActOnFinishFullExpr(Call.get());
      if (VDecl) {
        VDecl->setInit(Call.get());
        return E;
      }
    }
    else {
      assert(Action & kAssignValue && "Bad Action");

      // Mark the current number of arguemnts
      const size_t nArgs = CallArgs.size();

      if (!desugaredTy->isBuiltinType()) {
        if (desugaredTy->isIntegralOrEnumerationType()) {
          // force-cast it into uint64 in order to pick up the correct overload.
          TypeSourceInfo* TSI =
              AST.getTrivialTypeSourceInfo(AST.UnsignedLongLongTy, Loc);
          Expr* castedE = m_Sema->BuildCStyleCastExpr(Loc, TSI, Loc, E).get();
          CallArgs.push_back(castedE);
        } else if (desugaredTy->isReferenceType()) {
          // we need to get the address of the references
          Expr* AddrOfE = m_Sema->BuildUnaryOp(Scope, Loc, UO_AddrOf, E).get();
          CallArgs.push_back(AddrOfE);
        } else if (desugaredTy->isAnyPointerType() ||
                   desugaredTy->isConstantArrayType()) {
          // function pointers need explicit void* cast.
          TypeSourceInfo* TSI = AST.getTrivialTypeSourceInfo(AST.VoidPtrTy, Loc);
          Expr* castE = m_Sema->BuildCStyleCastExpr(Loc, TSI, Loc, E).get();
          CallArgs.push_back(castE);
        }
      } else
        CallArgs.push_back(E);

      // Test CallArgs.size to make sure an additional argument (the value)
      // has been pushed on, if not than we didn't know how to handle the type
      if (CallArgs.size() > nArgs) {
        Call = m_Sema->ActOnCallExpr(Scope, m_ValueSynth, Start, CallArgs, End);
      }
      else {
        m_Sema->Diag(Start, diag::err_unsupported_unknown_any_decl) <<
           utils::TypeName::GetFullyQualifiedName(desugaredTy, AST) <<
           SourceRange(Start, End);
      }
    }

    assert(!Call.isInvalid() && "Invalid Call");

    // Extend the scope of the temporary cleaner if applicable.
    if (Cleanups && !Call.isInvalid()) {
      Cleanups->setSubExpr(Call.get());
      Cleanups->setValueKind(Call.get()->getValueKind());
      Cleanups->setType(Call.get()->getType());
      return Cleanups;
    }

    return Call.get();
  }

  static bool VSError(clang::Sema* Sema, clang::Expr* E, llvm::StringRef Err) {
    DiagnosticsEngine& Diags = Sema->getDiagnostics();
    Diags.Report(E->getLocStart(),
                 Diags.getCustomDiagID(
                     clang::DiagnosticsEngine::Level::Error,
                     "ValueExtractionSynthesizer could not find: '%0'."))
        << Err;
    return false;
  }

  bool ValueExtractionSynthesizer::FindAndCacheRuntimeDecls(clang::Expr* E) {
    assert(!m_ValueSynth && "Called multiple times!?");
    ASTContext& AST = m_Parent->getSema().getASTContext();
    DeclContext* NSD = AST.getTranslationUnitDecl();

    LookupResult R(*m_Sema, &AST.Idents.get("cling_ValueExtraction"),
                   SourceLocation(), Sema::LookupOrdinaryName,
                   Sema::ForRedeclaration);

    m_Sema->LookupQualifiedName(R, NSD);
    if (R.empty())
      return VSError(m_Sema, E, "cling_ValueExtraction");

    CXXScopeSpec CSS;
    m_ValueSynth = m_Sema->BuildDeclarationNameExpr(CSS, R, /*ADL*/false).get();
    if (!m_ValueSynth)
      return VSError(m_Sema, E, "cling_ValueExtraction");
    return true;
  }
} // end namespace cling

namespace {

class Materialized : public cling::Value {
  Materialized* const V;

public:
  // Strip the & from the type name.
  Materialized(cling::Value* Vin, clang::QualType QT) : V((Materialized*)Vin) {
    if (V) V->m_Type = QT.getNonReferenceType().getAsOpaquePtr();
  }
  ~Materialized() {
    if (V) {
      // Mark the value as invalid.
      assert(V->m_StorageType != kManagedAllocation);
      V->m_StorageType = cling::Value::kUnsupportedType;
    }
  }
};

}

extern "C"
void* cling_ValueExtraction(void* vpSVR, void* vpQT, void* vpI, int Actn, ...) {
  cling::Interpreter* Interp = reinterpret_cast<cling::Interpreter*>(vpI);
  clang::QualType QT = clang::QualType::getFromOpaquePtr(vpQT);
  cling::Value& V = *reinterpret_cast<cling::Value*>(vpSVR);

  // Here the copy keeps the refcounted value alive.
  V = cling::Value(QT, *Interp, Actn != cling::kAssignType);
  if (Actn == cling::kAssignType)
    return V.getAs<void*>();

  // Was this a temporary object materialized only for this call?
  // If so, change the QT type to remove reference.
  // so that [cling] Test() will print '(Test) 0x*' vs '(Test &) @0x*'
  Materialized MVal(Actn & cling::kMaterialized ? &V : nullptr, QT);

  assert(Actn & cling::kAssignValue);
  va_list VL;
  va_start(VL, Actn);
  if (const clang::BuiltinType* BT = QT->getAs<clang::BuiltinType>()) {
    switch (BT->getKind()) {
      case clang::BuiltinType::WChar_U:
      case clang::BuiltinType::WChar_S:
        static_assert(sizeof(wchar_t) <= sizeof(int), "Size mismatch");
      case clang::BuiltinType::Bool:
      case clang::BuiltinType::SChar:
      case clang::BuiltinType::Char_S:
      case clang::BuiltinType::Char_U:
      case clang::BuiltinType::UChar:
      case clang::BuiltinType::Short:
      case clang::BuiltinType::UShort:
      case clang::BuiltinType::Char16:
      case clang::BuiltinType::Char32:
      case clang::BuiltinType::Int:
        V.getAs<long long>() = va_arg(VL, int);
        break;
      case clang::BuiltinType::UInt:
        V.getAs<unsigned long long>() = va_arg(VL, unsigned int);
        break;
      case clang::BuiltinType::Long:
        V.getAs<long long>() = va_arg(VL, long);
        break;
      case clang::BuiltinType::ULong:
        V.getAs<unsigned long long>() = va_arg(VL, unsigned long);
        break;
      case clang::BuiltinType::LongLong:
        V.getAs<long long>() = va_arg(VL, long long);
        break;
      case clang::BuiltinType::ULongLong:
        V.getAs<unsigned long long>() = va_arg(VL, unsigned long long);
        break;
      //case clang::BuiltinType::Int128:
      //case clang::BuiltinType::Half:
      case clang::BuiltinType::Float:
        V.getAs<float>() = va_arg(VL, double);
        break;
      case clang::BuiltinType::Double:
        V.getAs<double>() = va_arg(VL, double);
        break;
      case clang::BuiltinType::LongDouble:
        V.getAs<long double>() = va_arg(VL, long double);
        break;
      //case clang::BuiltinType::Float128:
      default:
        break;
    }
  } else
    V.getAs<void*>() = va_arg(VL, void*);

  if (Actn & cling::kDumpValue)
    V.dump();

  return nullptr;
}
