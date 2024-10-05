#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_SIDEEFFECTBETWEENSEQUENCE\
POINTSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_SIDEEFFECTBETWEENSEQUENCE\
POINTSCHECK_H

#include "../ClangTidyCheck.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace clang::tidy::bugprone {

class SideEffectsBetweenSequencePointsCheck : public ClangTidyCheck {
public:
    SideEffectsBetweenSequencePointsCheck(StringRef Name,
                                          ClangTidyContext* Context);
    void registerMatchers(ast_matchers::MatchFinder *Finder) override;
    void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

    class FunctionCallback : public MatchCallback {
    public:
        FunctionCallback(SideEffectsBetweenSequencePointsCheck* Check);
        void run(const ast_matchers::MatchFinder::MatchResult& Result) override;
    private:
        SideEffectsBetweenSequencePointsCheck* const Check;
    };

    class GlobalASTVisitor : public RecursiveASTVisitor<GlobalASTVisitor> {
    public:
        bool VisitDeclRefExpr(DeclRefExpr* S);
        bool VisitExpr(Expr* S);
        const std::vector<std::pair<DeclarationName, bool>>& getGlobalsFound();
    private:
        std::vector<std::pair<DeclarationName, bool>> GlobalsFound;
        static bool isGlobalDecl(const VarDecl* D);
        static bool isGlobalExpr(const Expr*    E);
    };

private:
    FunctionCallback             FuncCallback;
    std::vector<DeclarationName> FunctionNames;
};

} // namespace clang::tidy::bugprone

#endif
