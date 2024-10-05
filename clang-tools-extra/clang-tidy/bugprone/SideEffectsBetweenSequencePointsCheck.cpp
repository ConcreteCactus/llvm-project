#include "SideEffectsBetweenSequencePointsCheck.h"
#include "../utils/Matchers.h"
#include <iostream>

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
        const std::vector<DeclarationName>& LeftFound = 
            LeftVisitor.getGlobalsFound();
        const std::vector<DeclarationName>& RightFound = 
            RightVisitor.getGlobalsFound();
        for(uint32_t I = 0; I < LeftFound.size(); I++) {
            for(uint32_t J = 0; J < RightFound.size(); J++) {
                if(LeftFound[I] == RightFound[J]) {
                    std::cout << "Found with name: " 
                              << LeftFound[I].getAsString()
                              << std::endl;
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
    const ast_matchers::internal::VariadicDynCastAllOfMatcher<Expr, DeclRefExpr>
        DeclToExpr;
    auto FuncWithGlobalVarChange = 
        functionDecl(
            hasDescendant(
                expr(
                    writeOf(
                        DeclToExpr(
                            globalVarExpr()))))).bind("gFunc");
    Finder->addMatcher(FuncWithGlobalVarChange, this);

    auto FuncWithGlobalVarChangeFunc =
        functionDecl(
                hasDescendant(
                    callExpr(hasFunctionFrom(FunctionNames)))).bind("gFunc");
    Finder->addMatcher(FuncWithGlobalVarChangeFunc, this);
}

void SideEffectsBetweenSequencePointsCheck::check(
        const MatchFinder::MatchResult& Result) {
    const FunctionDecl* FD = Result.Nodes.getNodeAs<FunctionDecl>("gFunc");
    FunctionNames.push_back(FD->getDeclName());
    if(FD && FD->getBeginLoc().isValid()) {
        diag(FD->getBeginLoc(), "write of global variable in function");
    }
}

SideEffectsBetweenSequencePointsCheck::FunctionCallback::
    FunctionCallback(SideEffectsBetweenSequencePointsCheck* Check)
    : Check(Check) {}

void SideEffectsBetweenSequencePointsCheck::FunctionCallback::run(
        const MatchFinder::MatchResult& Result) {
}

bool SideEffectsBetweenSequencePointsCheck::GlobalASTVisitor::
    VisitStmt(Stmt* S) {
    if(!isa<DeclRefExpr>(S)) {
        return false;
    }
    const auto* DR = dyn_cast<DeclRefExpr>(S);
    if(!isa<VarDecl>(DR->getDecl())) {
        return false;
    }
    const auto* VD = dyn_cast<VarDecl>(DR->getDecl());
    if(VD->hasGlobalStorage()) {
        GlobalsFound.push_back(VD->getDeclName());
        return true;
    }
    return false;
}

const std::vector<DeclarationName>& SideEffectsBetweenSequencePointsCheck::
                                    GlobalASTVisitor::getGlobalsFound() {
    return GlobalsFound;
}

} // namespace clang::tidy::bugprone
