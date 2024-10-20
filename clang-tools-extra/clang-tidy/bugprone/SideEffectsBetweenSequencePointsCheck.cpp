#include "SideEffectsBetweenSequencePointsCheck.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

using GlobalRWAggregation = 
    SideEffectsBetweenSequencePointsCheck::GlobalRWAggregation;
using GlobalRWVisitor =
    SideEffectsBetweenSequencePointsCheck::GlobalRWVisitor;

static bool isGlobalDecl(const VarDecl* VD) {
    return 
        VD &&
        VD->hasGlobalStorage() && 
        VD->getLocation().isValid() &&
        !VD->getType().isConstQualified();
}

static bool isGlobalDeclRefExpr(const DeclRefExpr* DR) {
    if(!isa<VarDecl>(DR->getDecl())) {
        return false;
    }
    const auto* VD = dyn_cast<VarDecl>(DR->getDecl());
    return isGlobalDecl(VD);
}

static void checkCallExpr(const CallExpr* CE, GlobalRWVisitor* Visitor) {
    const Type* CT = CE->getCallee()->getType().getTypePtrOrNull();
    if(const auto* PT = dyn_cast_if_present<PointerType>(CT)) {
        CT = PT->getPointeeType().getTypePtrOrNull();
    }
    const auto* ProtoType = dyn_cast_if_present<FunctionProtoType>(CT);

    for(uint32_t I = 0; I < CE->getNumArgs(); I++) {
        const Expr* Arg = CE->getArg(I);
        if(const auto* DR = dyn_cast<DeclRefExpr>(Arg)) {
            if(!isGlobalDeclRefExpr(DR)) {
                continue;
            }

            if(!ProtoType || I >= ProtoType->getNumParams()) {
                Visitor->addGlobalManually(DR, /*isWrite*/false);
                continue;
            }

            const Type* ParamType = ProtoType->getParamType(I)
                                                .getTypePtrOrNull();
            if(const auto* RT = dyn_cast_if_present<ReferenceType>(ParamType)) {
                if(!RT->getPointeeType().isConstQualified()) {
                    Visitor->addGlobalManually(DR, /*isWrite*/true);
                }
            }

            Visitor->addGlobalManually(DR, /*isWrite*/false);
            continue;
        }

        if(const auto* Op = dyn_cast<UnaryOperator>(Arg)) {
            if(Op->getOpcode() != UO_AddrOf 
            || !isa<DeclRefExpr>(Op->getSubExpr())) {
                // Still do the traversal as we don't know what is in there.
                Visitor->startTraversal(const_cast<Expr*>(Arg));
                continue;
            }
            const auto* DR = dyn_cast<DeclRefExpr>(Op->getSubExpr());

            if(!isGlobalDeclRefExpr(DR)) {
                continue;
            }

            if(!ProtoType || I >= ProtoType->getNumParams()) {
                Visitor->addGlobalManually(DR, /*isWrite*/false);
                continue;
            }

            const Type* ParamType = ProtoType->getParamType(I)
                                                .getTypePtrOrNull();
            if(const auto* RT = dyn_cast_if_present<ReferenceType>(ParamType)) {
                if(!RT->getPointeeType().isConstQualified()) {
                    Visitor->addGlobalManually(DR, /*isWrite*/true);
                }
            }

            Visitor->addGlobalManually(DR, /*isWrite*/false);
            continue;
        }

        Visitor->startTraversal(const_cast<Expr*>(Arg));
    }
}

AST_MATCHER(Expr, twoGlobalWritesBetweenSequencePoints) {
    const Expr* E = &Node;
    GlobalRWVisitor Visitor;

    if(const BinaryOperator* Op = dyn_cast<BinaryOperator>(E)) {
        const BinaryOperator::Opcode Code = Op->getOpcode();
        if(Code == BO_LAnd || Code == BO_LOr || Code == BO_Comma) {
            return false;
        }
        Visitor.startTraversal(Op->getLHS());
        Visitor.startTraversal(Op->getRHS());
    }

    if(const CallExpr* CE = dyn_cast<CallExpr>(E)) {
        for(uint32_t I = 0; I < CE->getNumArgs(); I++) {
            Visitor.startTraversal(const_cast<Expr*>(CE->getArg(I)));
        }
    }

    std::vector<GlobalRWAggregation> Globals = Visitor.getGlobalsFound();
    for(uint32_t I = 0; I < Globals.size(); I++) {
        if(Globals[I].hasConflict()) {
            return true;
        }
    }
    return false;
}

SideEffectsBetweenSequencePointsCheck::SideEffectsBetweenSequencePointsCheck(
        StringRef Name,
        ClangTidyContext* Context)
    : ClangTidyCheck(Name, Context) {}

void
SideEffectsBetweenSequencePointsCheck::registerMatchers(
        MatchFinder* Finder) {
    Finder->addMatcher(
            stmt(traverse(TK_AsIs, expr(twoGlobalWritesBetweenSequencePoints())
                    .bind("gw"))), this);
}

void
SideEffectsBetweenSequencePointsCheck::check(
        const MatchFinder::MatchResult& Result) {
    const Expr* E = Result.Nodes.getNodeAs<Expr>("gw");
    if(E && E->getBeginLoc().isValid()) {
        diag(E->getBeginLoc(), "read/write conflict on global variable");
    }
}

void
GlobalRWVisitor::startTraversal(Expr* E) {
    TraversalIndex++;
    IsInsideAFunction = false;
    FunctionsChecked.clear();
    TraverseStmt(E); // soooo clean
}

bool
GlobalRWVisitor::VisitDeclRefExpr(DeclRefExpr* DR) {
    if(!isa<VarDecl>(DR->getDecl())) {
        return true;
    }
    const auto* VD = dyn_cast<VarDecl>(DR->getDecl());
    if(isGlobalDecl(VD)) {
        addGlobal(VD->getDeclName(), VD->getBeginLoc(), false);
        return true;
    }
    return true;
}

bool
GlobalRWVisitor::VisitExpr(Expr* E) {
    const Expr* Modified = nullptr;

    if(const auto* Op = dyn_cast<UnaryOperator>(E)) {
        UnaryOperator::Opcode Code = Op->getOpcode();
        if(Code == UO_PostInc || Code == UO_PostDec
        || Code == UO_PreInc  || Code == UO_PreDec) {
            Modified = Op->getSubExpr();
        } else {
            return true;
        }
    }

    if(const auto* Op = dyn_cast<BinaryOperator>(E)) {
        if(Op->isAssignmentOp()) {
            Modified = Op->getLHS();
        } else {
            return true;
        }
    }

    if(!Modified) {
        return true;
    }

    if(!isa<DeclRefExpr>(Modified)) {
        return true;
    }
    const auto* DE = dyn_cast<DeclRefExpr>(Modified);

    if(!isa<VarDecl>(DE->getDecl())) {
        return true;
    }
    const auto* VD = dyn_cast<VarDecl>(DE->getDecl());

    if(isGlobalDecl(VD)) {
        addGlobal(VD->getDeclName(), VD->getBeginLoc(), true);
        return false;
    }
    return true;
}

bool
GlobalRWVisitor::TraverseStmt(Stmt* S, DataRecursionQueue* Queue) {
    bool Result = RecursiveASTVisitor<GlobalRWVisitor>::TraverseStmt(S, Queue);

    if(!isa<CallExpr>(S)) {
        return Result;
    }
    const auto* CE = dyn_cast<CallExpr>(S);

    if(!isa<FunctionDecl>(CE->getCalleeDecl())) {
        return Result;
    }
    const auto* FD = dyn_cast<FunctionDecl>(CE->getCalleeDecl());

    if(!FD->hasBody()) {
        return Result;
    }

    for(uint32_t I = 0; I < FunctionsChecked.size(); I++) {
        if(FunctionsChecked[I] == FD->getDeclName()) {
            return Result;
        }
    }
    FunctionsChecked.push_back(FD->getDeclName());
    bool StartedOutsideFunction = IsInsideAFunction;
    IsInsideAFunction = true;
    TraverseStmt(FD->getBody(), Queue);
    if(!StartedOutsideFunction) {
        IsInsideAFunction = false;
    }

    return Result;
}

const std::vector<GlobalRWAggregation>& 
GlobalRWVisitor::getGlobalsFound() {
    return GlobalsFound;
}

void
GlobalRWVisitor::addGlobal(DeclarationName Name, SourceLocation Loc,
                           bool IsWrite) {
    for(uint32_t I = 0; I < GlobalsFound.size(); I++) {
        if(GlobalsFound[I].getDeclName() == Name) {
            GlobalsFound[I].addGlobalRW(Loc, IsWrite, TraversalIndex,
                                        IsInsideAFunction);
            return;
        }
    }

    GlobalsFound.emplace_back(Name, Loc, IsWrite, TraversalIndex,
                              IsInsideAFunction);
}

void
GlobalRWVisitor::addGlobal(const DeclRefExpr* DR, bool IsWrite) {
    const auto* VD = dyn_cast<VarDecl>(DR->getDecl());
    assert(VD); // Risky
    addGlobal(VD->getDeclName(), DR->getBeginLoc(), IsWrite);
}

GlobalRWAggregation::GlobalRWAggregation(DeclarationName Name,
                                         SourceLocation  Loc,
                                         bool            IsWrite,
                                         int             Index,
                                         bool            IsInFunction)
    : DeclName(Name), IndexCreated(Index), HasWrite(IsWrite),
      HasConflict(false), IsAnyInFunction(IsInFunction) {

    if(IsWrite) {
        WritePos = Loc;
    } else {
        OtherPos = Loc;
    }
}

void
GlobalRWAggregation::addGlobalRW(SourceLocation Loc, bool IsWrite, int Index,
                                 bool IsInFunction) {
    if(HasConflict || (!IsWrite && !HasWrite)) {
        return;
    }

    IsAnyInFunction |= IsInFunction;

    if(HasWrite) {
        OtherPos = Loc;
    } else {
        WritePos = Loc;
        HasWrite = true;
    }

    if(IndexCreated != Index) {
        HasConflict = true;
    }
}

DeclarationName
GlobalRWAggregation::getDeclName() {
    return DeclName;
}

bool
GlobalRWAggregation::hasConflict() {
    return HasConflict && IsAnyInFunction;
}

} // namespace clang::tidy::bugprone
