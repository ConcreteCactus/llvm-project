#ifndef CLANG_ANALYSIS_FLOWSENSITIVE_MODELS_SIDEEFFECTOUTSIDECONEOFEVALUATIONMODEL_H
#define CLANG_ANALYSIS_FLOWSENSITIVE_MODELS_SIDEEFFECTOUTSIDECONEOFEVALUATIONMODEL_H

#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"

namespace clang {
namespace dataflow {

class SideEffectOutsideConeOfEvaluationModel;
class SideEffectOutsideConeOfEvaluationDiagnosis;

class DeclaredVariablesLattice {
public:
    DeclaredVariablesLattice() {
    }
    bool operator==(const DeclaredVariablesLattice& other) const;
    LatticeJoinEffect join(const DeclaredVariablesLattice& other);

    void addDecl(const ValueDecl* D);
    void removeDecl(const ValueDecl* D);
    bool hasDecl(const ValueDecl* D) const;

private:
    llvm::DenseSet<const ValueDecl*> declarations;
};

class SideEffectOutsideConeOfEvaluationModel :
    public DataflowAnalysis<SideEffectOutsideConeOfEvaluationModel,
                            DeclaredVariablesLattice>
{
public:
    SideEffectOutsideConeOfEvaluationModel(ASTContext &Ctx, Environment &Env);

    DeclaredVariablesLattice initialElement() { return {}; }
    void transfer(const CFGElement &Elt, DeclaredVariablesLattice &L,
                  Environment &Env);
};

struct SideEffectOutsideConeOfEvaluationDiagnostic {
    CharSourceRange Range;
};

class SideEffectOutsideConeOfEvaluationDiagnoser {
public:
    llvm::SmallVector<SideEffectOutsideConeOfEvaluationDiagnostic>
    operator()(const CFGElement &Elt, ASTContext &Ctx,
               const TransferStateForDiagnostics<DeclaredVariablesLattice> &L);
};

} // namespace dataflow
} // namespace clang

#endif
