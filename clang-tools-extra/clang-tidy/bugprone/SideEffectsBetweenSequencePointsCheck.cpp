#include "SideEffectsBetweenSequencePointsCheck.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

using GlobalRWAggregation = 
    SideEffectsBetweenSequencePointsCheck::GlobalRWAggregation;
using GlobalRWVisitor =
    SideEffectsBetweenSequencePointsCheck::GlobalRWVisitor;


AST_MATCHER(DeclRefExpr, globalVarExpr) {
    const DeclRefExpr* E = &Node;
    const ValueDecl* D = E->getDecl();
    if(!isa<VarDecl>(D)) {
        return false;
    }
    const VarDecl* VarD = dyn_cast<VarDecl>(D);

    return VarD->getLocation().isValid()
        && VarD->hasGlobalStorage()
        && !VarD->getType().isConstQualified();
}

using ExpressionMatcher = ast_matchers::internal::Matcher<Expr>;

AST_MATCHER_P(Expr, writeOf, ExpressionMatcher, InnerMatcher) {
    const Expr* E = &Node;

    if(const auto* Op = dyn_cast<UnaryOperator>(E)) {
        UnaryOperator::Opcode Code = Op->getOpcode();
        if(Code == UO_PostInc || Code == UO_PostDec
        || Code == UO_PreInc  || Code == UO_PreDec) {
            return InnerMatcher.matches(*Op->getSubExpr(), Finder, Builder);
        }
        return false;
    }

    if(const auto* Op = dyn_cast<BinaryOperator>(E)) {
        if(Op->isAssignmentOp()) {
            return InnerMatcher.matches(*Op->getLHS(), Finder, Builder);
        }
    }
    return false;
}

AST_MATCHER_P(CallExpr, hasFunctionFrom,
        std::reference_wrapper<const std::vector<DeclarationName>>,
        PossibleNames) {
    const CallExpr* E = &Node;
    const FunctionDecl* D = E->getDirectCallee();
    if(!D) {
        return false;
    }
    for(size_t I = 0; I < PossibleNames.get().size(); I++) {
        if(PossibleNames.get()[I] == D->getDeclName()) {
            return true;
        }
    }

    return false;
}

AST_MATCHER(Expr, twoGlobalWritesBetweenSequencePoints) {
    const Expr* E = &Node;

    const ast_matchers::internal::Matcher<Expr> GlobalMatcher = 
        hasDescendant(declRefExpr(globalVarExpr()));

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
            //NOLINTNEXTLINE
            Visitor.startTraversal((Expr*)(CE->getArg(I)));
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
            stmt(hasDescendant(expr(twoGlobalWritesBetweenSequencePoints())
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
    FunctionsChecked.clear();
    TraverseStmt(E);
}

bool
GlobalRWVisitor::isGlobalDecl(const VarDecl* VD) {
    return 
        VD->hasGlobalStorage() && 
        VD->getLocation().isValid() &&
        !VD->getType().isConstQualified();
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
    Expr* Modified = nullptr;

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
    TraverseStmt(FD->getBody(), Queue);

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
            GlobalsFound[I].addGlobalRW(Loc, IsWrite, TraversalIndex);
            return;
        }
    }

    GlobalsFound.emplace_back(Name, Loc, IsWrite, TraversalIndex);
}

GlobalRWAggregation::GlobalRWAggregation(DeclarationName Name,
                                         SourceLocation  Loc,
                                         bool            IsWrite,
                                         int             Index)
    : DeclName(Name), IndexCreated(Index), HasWrite(IsWrite),
      HasConflict(false) {

    if(IsWrite) {
        WritePos = Loc;
    } else {
        OtherPos = Loc;
    }
}

void
GlobalRWAggregation::addGlobalRW(SourceLocation Loc, bool IsWrite, int Index) {
    if(HasConflict || (!IsWrite && !HasWrite)) {
        return;
    }

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
    return HasConflict;
}

} // namespace clang::tidy::bugprone
