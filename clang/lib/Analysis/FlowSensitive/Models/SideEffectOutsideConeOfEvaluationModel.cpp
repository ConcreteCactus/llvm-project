#include "clang/Analysis/FlowSensitive/Models/SideEffectOutsideConeOfEvaluationModel.h"

namespace clang {
namespace dataflow {

bool DeclaredVariablesLattice::operator==(const DeclaredVariablesLattice& other)
    const
{
    if (other.declarations.size() != declarations.size()) {
        return false;
    }

    int blocks = declarations.size();
    for (int i = blocks - 1; i >= 0; i--) {
        if (other.declarations[i].size() != declarations[i].size()) {
            return false;
        }

        int decls = declarations[i].size();
        if (decls > 0 &&
                other.declarations[i][decls - 1] != declarations[i][decls - 1])
        {
            return false;
        }
    }

    return true;
}

LatticeJoinEffect DeclaredVariablesLattice::join(
        const DeclaredVariablesLattice& other)
{
    assert(*this == other);
    return LatticeJoinEffect::Unchanged;
}

void DeclaredVariablesLattice::addBlock() {
    declarations.emplace_back();
}

void DeclaredVariablesLattice::removeBlock() {
    declarations.pop_back();
}

void DeclaredVariablesLattice::addDecl(const ValueDecl* D) {
    declarations[declarations.size() - 1].push_back(D);
}

bool DeclaredVariablesLattice::hasDecl(const ValueDecl* D) const {
    int blocks = declarations.size();
    for (int i = blocks - 1; i >= 0; i--) {
        int decls = declarations[i].size();
        for (int j = decls - 1; j >= 0; j--) {
            if (declarations[i][j] == D) {
                return true;
            }
        }
    }

    return false;
}

// TODO write a testcase where the declaration is a field
static bool isDeclOutside(
        const ValueDecl* VD, const DeclaredVariablesLattice& L,
        const Environment &Env)
{
    return !L.hasDecl(VD);
}

static bool isExprOutside(const Expr* E, const DeclaredVariablesLattice& L,
                          const Environment &Env)
{
    if (const auto* DRE = dyn_cast<DeclRefExpr>(E)) {
        return isDeclOutside(DRE->getDecl(), L, Env);
    }

    const Value* V = Env.getValue(*E);
    const Value* Property;
    const BoolValue* Proposition;

    if (!V || !(Property = V->getProperty("value_inside_cone"))
        || !(Proposition = cast_or_null<BoolValue>(Property))
        || !Env.proves(Proposition->formula()))
    {
        return true;
    }

    return false;
}

static void markOrigin(const Expr* E, bool isInside, Environment &Env) {
    Value* V = Env.getValue(*E);

    if (!V) {
        Value* NewV = Env.createValue(E->getType());
        Env.setValue(*E, *NewV);
        V = Env.getValue(*E);
    }

    V->setProperty("value_inside_cone",
                   Env.getBoolLiteralValue(isInside));
}

SideEffectOutsideConeOfEvaluationModel::SideEffectOutsideConeOfEvaluationModel(
        ASTContext &Ctx, Environment &Env) : DataflowAnalysis(Ctx)
{
}

void SideEffectOutsideConeOfEvaluationModel::transfer(
        const CFGElement &Elt, DeclaredVariablesLattice &L, Environment &Env)
{
    if (Elt.getKind() == CFGElement::Kind::ScopeBegin) {
        L.addBlock();
    } else if (Elt.getKind() != CFGElement::Kind::ScopeEnd) {
        L.removeBlock();
    } else if (Elt.getKind() != CFGElement::Kind::Statement) {
        return;
    }

    const Stmt* S = static_cast<const CFGStmt*>(&Elt)->getStmt();

    if (const auto* DS = dyn_cast<const DeclStmt>(S)) {
        if (const auto* VD = dyn_cast<const ValueDecl>(DS->getSingleDecl())) {
            L.addDecl(VD);
        }
    }

    if (const auto* Op = dyn_cast<const UnaryOperator>(S)) {
        switch (Op->getOpcode()) {
            case UO_AddrOf:
                markOrigin(Op, isExprOutside(Op->getSubExpr(), L, Env), Env);
                break;
            default:
                break;
        }
        return;
    }

    if (const auto* C = dyn_cast<const CastExpr>(S)) {
        QualType CastedType = C->getType();
        QualType OriginalType = C->getSubExpr()->getType();
        if (CastedType.getNonReferenceType() == OriginalType) {
            markOrigin(C, isExprOutside(C->getSubExpr(), L, Env), Env);
        }
    }

    // TODO: add test for lambda captured variables
    // TODO: add test for malloc new calloc stuff like that

}

static llvm::SmallVector<SideEffectOutsideConeOfEvaluationDiagnostic>
checkEffectedValue(const Expr *E, ASTContext &Ctx, const Environment &Env,
                   const DeclaredVariablesLattice& L)
{
    if (isExprOutside(E, L, Env)) {
        return llvm::SmallVector<SideEffectOutsideConeOfEvaluationDiagnostic>{
            { Ctx.getSourceManager().getExpansionRange(E->getSourceRange()) }
        };
    }

    return {};
}

llvm::SmallVector<SideEffectOutsideConeOfEvaluationDiagnostic>
SideEffectOutsideConeOfEvaluationDiagnoser::operator()(
    const CFGElement &Elt, ASTContext &Ctx,
    const TransferStateForDiagnostics<DeclaredVariablesLattice> &State)
{
    const Environment &Env = State.Env;
    const DeclaredVariablesLattice &L = State.Lattice;

    if (Elt.getKind() != CFGElement::Kind::Statement) {
        return {};
    }

    const Stmt* S = static_cast<const CFGStmt*>(&Elt)->getStmt();

    if (const auto* Op = dyn_cast<const UnaryOperator>(S)) {
        switch (Op->getOpcode()) {
            case UO_PostInc:
            case UO_PostDec:
            case UO_PreInc:
            case UO_PreDec:
                return checkEffectedValue(Op->getSubExpr(), Ctx, Env, L);
            default:
                break;
        }
        return {};
    }

    if (const auto* Op = dyn_cast<const BinaryOperator>(S)) {
        switch (Op->getOpcode()) {
            case BO_Assign:
            case BO_MulAssign:
            case BO_DivAssign:
            case BO_RemAssign:
            case BO_AddAssign:
            case BO_SubAssign:
            case BO_ShlAssign:
            case BO_ShrAssign:
            case BO_AndAssign:
            case BO_XorAssign:
            case BO_OrAssign:
                return checkEffectedValue(Op->getLHS(), Ctx, Env, L);
            default:
                break;
        }
        return {};
    }

    return {};
}

} // namespace dataflow
} // namespace clang
