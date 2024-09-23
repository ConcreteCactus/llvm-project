#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_SIDEEFFECTBETWEENSEQUENCE\
POINTSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_SIDEEFFECTBETWEENSEQUENCE\
POINTSCHECK_H

#include "../ClangTidyCheck.h"

namespace clang::tidy::bugprone {

class SideEffectsBetweenSequencePointsCheck : public ClangTidyCheck {
public:
    SideEffectsBetweenSequencePointsCheck(StringRef Name,
                                          ClangTidyContext* Context);
    void registerMatchers(ast_matchers::MatchFinder *Finder) override;
    void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

    class FunctionCallback : public MatchCallback { // Not used
    public:
        FunctionCallback(SideEffectsBetweenSequencePointsCheck* Check);
        void run(const ast_matchers::MatchFinder::MatchResult& Result) override;
    private:
        SideEffectsBetweenSequencePointsCheck* const Check;
    };

private:
    FunctionCallback FuncCallback; // Not used
    std::vector<DeclarationName> FunctionNames; // Not used
};

} // namespace clang::tidy::bugprone

#endif
