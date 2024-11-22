#ifndef LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_SIDEEFFECTBETWEENSEQUENCE\
POINTSCHECK_H
#define LLVM_CLANG_TOOLS_EXTRA_CLANG_TIDY_BUGPRONE_SIDEEFFECTBETWEENSEQUENCE\
POINTSCHECK_H

#include "../ClangTidyCheck.h"
#include "clang/AST/RecursiveASTVisitor.h"

namespace clang::tidy::bugprone {

class ConflictingGlobalAccesses : public ClangTidyCheck {
public:
    ConflictingGlobalAccesses(StringRef Name,
                                          ClangTidyContext* Context);
    void registerMatchers(ast_matchers::MatchFinder *Finder) override;
    void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

    using AccessKind = uint8_t;
    static constexpr AccessKind AkRead           = 0;
    static constexpr AccessKind AkWrite          = 1;
    static constexpr AccessKind AkUncheckedRead  = 2;
    static constexpr AccessKind AkUncheckedWrite = 3;
    using TraversalResultKind = uint8_t;
    // Bits are corresponding to the AccessKind enum values.
    static constexpr TraversalResultKind TrInvalid = 0;
    static constexpr TraversalResultKind TrRead    = 1 << AkRead;
    static constexpr TraversalResultKind TrWrite   = 1 << AkWrite;
    static constexpr TraversalResultKind
        TrUncheckedRead = 1 << AkUncheckedRead;
    static constexpr TraversalResultKind 
        TrUncheckedWrite = 1 << AkUncheckedWrite;

private:
    static TraversalResultKind akToTr(AccessKind Ak);
    static constexpr uint8_t AkCount = 4;

    using FieldIndex = uint16_t;
    static constexpr FieldIndex FiUnion = 0x8000;
    using FieldIndexArray = SmallVector<FieldIndex>;
    // Note: This bit signals whether the field is a *field of* a struct or a
    // union, not whether the type of the field itself is a struct or a union.
public:

    // The result of a single traversal. Multiple traversal results are
    // aggregated inside the TraversalAggregation.
    struct TraversalResult {
        int                 IndexCreated;
        SourceLocation      Loc[AkCount];
        TraversalResultKind Kind;
        
        TraversalResult();
        TraversalResult(int Index, SourceLocation Loc, AccessKind Access);
        void addNewAccess(SourceLocation Loc, AccessKind Access);
    };

    class TraversalAggregation {
        DeclarationName DeclName;
        TraversalResult MainPart, OtherPart;
        // Pairings that are not reportable: Read-Read, Read-Write,
        // Read-UncheckedRead, Write-Write, UncheckedRead-UncheckedRead.
    public:
        TraversalAggregation();
        TraversalAggregation(DeclarationName Name, SourceLocation Loc,
                             AccessKind Access, int Index);
        void addGlobalRW(SourceLocation Loc, AccessKind Access, int Index);
        DeclarationName getDeclName() const;

        bool isValid() const;
        // Some conflicts are already flagged by -Wunsequenced, we don't want 
        // to report those.
        bool shouldBeReported() const;
        bool hasConflictingOperations() const;
    private:
        bool hasTwoAccesses() const;
        bool isReportedByWunsequenced() const;
    };

    struct ObjectAccessTree {
        using FieldMap = std::map<int, std::unique_ptr<ObjectAccessTree>>;
        TraversalAggregation OwnAccesses;
        TraversalAggregation UnionTemporalAccesses; // In a union, new fields
                                                    // should inherit from
                                                    // UnionTemporalAccesses
                                                    // instead of OwnAccesses.
        FieldMap             Fields;
        bool                 IsUnion;

        ObjectAccessTree(TraversalAggregation Own);
        void addFieldToAll(SourceLocation Loc, AccessKind Access, int Index);
        void addFieldToAllExcept(uint16_t ExceptIndex, SourceLocation Loc,
                                 AccessKind Access, int Index);
    };

    class ObjectTraversalAggregation {
        DeclarationName DeclName;
        ObjectAccessTree AccessTree;

    public:
        ObjectTraversalAggregation(DeclarationName Name, SourceLocation Loc,
                                   FieldIndexArray FieldIndices,
                                   AccessKind Access, int Index);
        void addFieldRW(SourceLocation Loc, FieldIndexArray FieldIndices, 
                        AccessKind Access, int Index);
        DeclarationName getDeclName() const;
        bool shouldBeReported() const;
    private:
        bool shouldBeReportedRec(const ObjectAccessTree* Node) const;
    };

    class GlobalRWVisitor : public RecursiveASTVisitor<GlobalRWVisitor> {
    friend RecursiveASTVisitor<GlobalRWVisitor>;
    public:
        GlobalRWVisitor();

        void startTraversal(Expr* E);
        const std::vector<TraversalAggregation>& getGlobalsFound() const;
        const std::vector<ObjectTraversalAggregation>&
            getObjectGlobalsFound() const;

    protected:
        // RecursiveASTVisitor overrides
        bool VisitDeclRefExpr(DeclRefExpr* S);
        bool VisitUnaryOperator(UnaryOperator* Op);
        bool VisitBinaryOperator(BinaryOperator* Op);
        bool VisitCallExpr(CallExpr* CE);
        bool VisitMemberExpr(MemberExpr* ME);

    private:
        void visitCallExprArgs(CallExpr* CE);
        void traverseFunctionsToBeChecked();

        std::vector<TraversalAggregation> GlobalsFound;
        std::vector<ObjectTraversalAggregation> ObjectGlobalsFound;
        std::vector<std::pair<DeclarationName, Stmt*>> FunctionsChecked;
        int TraversalIndex;
        bool IsInFunction;
        void addGlobal(DeclarationName Name, SourceLocation Loc, bool IsWrite,
                       bool IsUnchecked);
        void addGlobal(const DeclRefExpr* DR, bool IsWrite, bool IsUnchecked);
        void addField(DeclarationName Name, FieldIndexArray FieldIndices,
                      SourceLocation Loc, bool IsWrite, bool IsUnchecked);
        bool handleModified(const Expr* Modified, bool IsUnchecked);
        bool handleModifiedVariable(const DeclRefExpr* DE, bool IsUnchecked);
        bool handleAccessedObject(const Expr* E, bool IsWrite, 
                                  bool IsUnchecked);
        bool isVariable(const Expr* E);
    };
};

} // namespace clang::tidy::bugprone

#endif
