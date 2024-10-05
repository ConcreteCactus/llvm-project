#include "SideEffectsBetweenSequencePointsCheck.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

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

    if(const BinaryOperator* Op = dyn_cast<BinaryOperator>(E)) {
        const BinaryOperator::Opcode Code = Op->getOpcode();
        if(Code == BO_LAnd || Code == BO_LOr || Code == BO_Comma) {
            return false;
        }

        SideEffectsBetweenSequencePointsCheck::GlobalASTVisitor LeftVisitor;
        SideEffectsBetweenSequencePointsCheck::GlobalASTVisitor RightVisitor;
        LeftVisitor.TraverseStmt(Op->getLHS());
        RightVisitor.TraverseStmt(Op->getRHS());
        const std::vector<std::pair<DeclarationName, bool>>& LeftFound = 
            LeftVisitor.getGlobalsFound();
        const std::vector<std::pair<DeclarationName, bool>>& RightFound = 
            RightVisitor.getGlobalsFound();
        for(uint32_t I = 0; I < LeftFound.size(); I++) {
            for(uint32_t J = 0; J < RightFound.size(); J++) {
                if(LeftFound[I].first == RightFound[J].first &&
                        (LeftFound[I].second || RightFound[J].second)) {
                    return true;
                }
            }
        }
    }

    return false;
}

SideEffectsBetweenSequencePointsCheck::SideEffectsBetweenSequencePointsCheck(
        StringRef Name,
        ClangTidyContext* Context)
    : ClangTidyCheck(Name, Context),
    FuncCallback(this) {}

void SideEffectsBetweenSequencePointsCheck::registerMatchers(
        MatchFinder* Finder) {
    Finder->addMatcher(
            stmt(hasDescendant(expr(twoGlobalWritesBetweenSequencePoints())
                    .bind("gw"))), this);
}

void SideEffectsBetweenSequencePointsCheck::check(
        const MatchFinder::MatchResult& Result) {
    const Expr* E = Result.Nodes.getNodeAs<Expr>("gw");
    if(E && E->getBeginLoc().isValid()) {
        diag(E->getBeginLoc(), "read/write conflict on global variable");
    }
}

SideEffectsBetweenSequencePointsCheck::FunctionCallback::
    FunctionCallback(SideEffectsBetweenSequencePointsCheck* Check)
    : Check(Check) {}

void SideEffectsBetweenSequencePointsCheck::FunctionCallback::run(
        const MatchFinder::MatchResult& Result) {
}

bool SideEffectsBetweenSequencePointsCheck::GlobalASTVisitor::
    isGlobalDecl(const VarDecl* VD) {
    return 
        VD->hasGlobalStorage() && 
        VD->getLocation().isValid() &&
        !VD->getType().isConstQualified();
}

bool SideEffectsBetweenSequencePointsCheck::GlobalASTVisitor::
    VisitDeclRefExpr(DeclRefExpr* DR) {
    if(!isa<VarDecl>(DR->getDecl())) {
        return true;
    }
    const auto* VD = dyn_cast<VarDecl>(DR->getDecl());
    if(isGlobalDecl(VD)) {
        GlobalsFound.emplace_back(VD->getDeclName(), false);
        return true;
    }
    return true;
}

bool SideEffectsBetweenSequencePointsCheck::GlobalASTVisitor::
    VisitExpr(Expr* E) {

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
        GlobalsFound.emplace_back(VD->getDeclName(), true);
        return false;
    }
    return true;
}

const std::vector<std::pair<DeclarationName, bool>>& 
SideEffectsBetweenSequencePointsCheck::GlobalASTVisitor::getGlobalsFound() {
    return GlobalsFound;
}

} // namespace clang::tidy::bugprone
