#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_SIDEEFFECTBETWEENSEQUENCE\
POINTSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_SIDEEFFECTBETWEENSEQUENCE\
POINTSCHECK_H

#include "../ClangTidyCheck.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace clang::tidy::bugprone {

// The purpose of this interface is to create binding names for special
// matchers. The implementers of this interface will need to be able to
// own the generated Strings.
class IBindingNameOwner {
public:
    virtual StringRef generateBindingName(uint32_t Id) = 0;
};

using BindingNameGenerator = struct BindingNameGenerator {
    IBindingNameOwner* Owner;
    int Counter;

    StringRef generateNextBindingName() {
        StringRef Name = Owner->generateBindingName(Counter);
        Counter++;
        return Name;
    }
};

class SideEffectsBetweenSequencePointsCheck : public ClangTidyCheck,
                                              public IBindingNameOwner {
public:
    SideEffectsBetweenSequencePointsCheck(StringRef Name,
                                          ClangTidyContext* Context);
    void registerMatchers(ast_matchers::MatchFinder *Finder) override;
    void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

    StringRef generateBindingName(uint32_t Id) override;

    class FunctionCallback : public MatchCallback {
    public:
        FunctionCallback(SideEffectsBetweenSequencePointsCheck* Check);
        void run(const ast_matchers::MatchFinder::MatchResult& Result) override;
    private:
        SideEffectsBetweenSequencePointsCheck* const Check;
    };

    class GlobalASTVisitor : public RecursiveASTVisitor<GlobalASTVisitor> {
    public:
        bool VisitStmt(Stmt* S);
        const std::vector<DeclarationName>& getGlobalsFound();
    private:
        std::vector<DeclarationName> GlobalsFound;
    };

private:
    FunctionCallback             FuncCallback;
    std::vector<DeclarationName> FunctionNames;
    std::vector<std::string>     BindingNames;
};

} // namespace clang::tidy::bugprone

#endif
