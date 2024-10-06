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
        bool HasWrite, HasConflict;
    public:
        GlobalRWAggregation(DeclarationName Name,
                            SourceLocation  Loc,
                            bool            IsWrite,
                            int             Index);
        void addGlobalRW(SourceLocation Loc, bool IsWrite, int Index);
        DeclarationName getDeclName();
        bool hasConflict();
    };

    class GlobalRWVisitor
        : public RecursiveASTVisitor<GlobalRWVisitor> {
    public:
        void startTraversal(Expr* E);
        const std::vector<GlobalRWAggregation>& getGlobalsFound();

        // overrides (please don't call these directly)
        bool VisitDeclRefExpr(DeclRefExpr* S);
        bool VisitExpr(Expr* E);
        bool TraverseStmt(Stmt* S, DataRecursionQueue* Queue = nullptr);
    private:
        std::vector<GlobalRWAggregation> GlobalsFound;
        std::vector<DeclarationName> FunctionsChecked;
        int TraversalIndex;
        static bool isGlobalDecl(const VarDecl* D);
        static bool isGlobalExpr(const Expr*    E);
        void addGlobal(DeclarationName Name, SourceLocation Loc, bool IsWrite);
    };

private:
    std::vector<DeclarationName> FunctionNames;
};

} // namespace clang::tidy::bugprone

#endif
