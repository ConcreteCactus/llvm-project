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

    class GlobalRWAggregation {
        DeclarationName DeclName;
        SourceLocation WritePos, OtherPos;
        int IndexCreated;
        bool HasWrite, HasConflict, IsAnyInFunction;
    public:
        GlobalRWAggregation();
        GlobalRWAggregation(DeclarationName Name,
                            SourceLocation  Loc,
                            bool            IsWrite,
                            int             Index,
                            bool            IsInFunction);
        void addGlobalRW(SourceLocation Loc, bool IsWrite, int Index,
                         bool IsInFunction);
        DeclarationName getDeclName();
        bool hasConflict();
    };

    class GlobalRWVisitor
        : public RecursiveASTVisitor<GlobalRWVisitor> {
    friend RecursiveASTVisitor<GlobalRWVisitor>;
    public:
        void startTraversal(Expr* E);
        const std::vector<GlobalRWAggregation>& getGlobalsFound();

    private:
        // RecursiveASTVisitor overrides
        bool VisitDeclRefExpr(DeclRefExpr* S);
        bool VisitExpr(Expr* E);
        bool TraverseStmt(Stmt* S, DataRecursionQueue* Queue = nullptr);

        std::vector<GlobalRWAggregation> GlobalsFound;
        std::vector<DeclarationName> FunctionsChecked;
        int TraversalIndex;
        bool IsInsideAFunction;
        void addGlobal(DeclarationName Name, SourceLocation Loc, bool IsWrite);
        void addGlobal(const DeclRefExpr* DR, bool IsWrite);
    };

private:
    std::vector<DeclarationName> FunctionNames;
};

} // namespace clang::tidy::bugprone

#endif
