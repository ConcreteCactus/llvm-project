#include "ConflictingGlobalAccesses.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

using TraversalAggregation = 
    ConflictingGlobalAccesses::TraversalAggregation;
using GlobalRWVisitor =
    ConflictingGlobalAccesses::GlobalRWVisitor;
using TraversalResultKind =
    ConflictingGlobalAccesses::TraversalResultKind;
using TraversalResult =
    ConflictingGlobalAccesses::TraversalResult;
using ObjectAccessTree =
    ConflictingGlobalAccesses::ObjectAccessTree;
using FieldMap = ObjectAccessTree::FieldMap;
using ObjectTraversalAggregation =
    ConflictingGlobalAccesses::ObjectTraversalAggregation;

static bool isGlobalDecl(const VarDecl* VD) {
    return 
        VD &&
        VD->hasGlobalStorage() && 
        VD->getLocation().isValid() &&
        !VD->getType().isConstQualified();
}

AST_MATCHER(Expr, twoGlobalWritesBetweenSequencePoints) {
    const Expr* E = &Node;
    GlobalRWVisitor Visitor;

    if(const BinaryOperator* Op = dyn_cast<BinaryOperator>(E)) {
        const BinaryOperator::Opcode Code = Op->getOpcode();
        if(Code == BO_LAnd || Code == BO_LOr || Code == BO_Comma) {
            return false;
        }
        Visitor.startTraversal(Op->getLHS());
        Visitor.startTraversal(Op->getRHS());
    }

    if(const CallExpr* CE = dyn_cast<CallExpr>(E)) {
        for(uint32_t I = 0; I < CE->getNumArgs(); I++) {
            Visitor.startTraversal(const_cast<Expr*>(CE->getArg(I)));
        }
    }

    const std::vector<TraversalAggregation>& Globals =
                                                      Visitor.getGlobalsFound();
    for(uint32_t I = 0; I < Globals.size(); I++) {
        if(Globals[I].shouldBeReported()) {
            return true;
        }
    }
    const std::vector<ObjectTraversalAggregation>& ObjectGlobals = 
                                                Visitor.getObjectGlobalsFound();
    for(uint32_t I = 0; I < ObjectGlobals.size(); I++) {
        if(ObjectGlobals[I].shouldBeReported()) {
            return true;
        }
    }
    return false;
}

ConflictingGlobalAccesses::ConflictingGlobalAccesses(
        StringRef Name,
        ClangTidyContext* Context)
    : ClangTidyCheck(Name, Context) {}

void
ConflictingGlobalAccesses::registerMatchers(
        MatchFinder* Finder) {
    Finder->addMatcher(
            stmt(traverse(TK_AsIs, expr(twoGlobalWritesBetweenSequencePoints())
                    .bind("gw"))), this);
}

void
ConflictingGlobalAccesses::check(
        const MatchFinder::MatchResult& Result) {
    const Expr* E = Result.Nodes.getNodeAs<Expr>("gw");
    if(E && E->getBeginLoc().isValid()) {
        diag(E->getBeginLoc(), "read/write conflict on global variable");
    } else {
        diag("read/write conflict on global variable");
    }
}

GlobalRWVisitor::GlobalRWVisitor() : TraversalIndex(0), IsInFunction(false) {}

void
GlobalRWVisitor::startTraversal(Expr* E) {
    TraversalIndex++;
    FunctionsChecked.clear();
    IsInFunction = false;
    TraverseStmt(E);
    traverseFunctionsToBeChecked();
}

void
GlobalRWVisitor::traverseFunctionsToBeChecked() {
    IsInFunction = true;
    // Expect FunctionsChecked to grow while walking.
    for(uint32_t I = 0; I < FunctionsChecked.size(); I++) {
        TraverseStmt(FunctionsChecked[I].second);
    }
}

bool
GlobalRWVisitor::isVariable(const Expr* E) {
    const Type* T = E->getType().getTypePtrOrNull();
    return isa<DeclRefExpr>(E) && (!T->isRecordType() || T->isUnionType());
}

bool
GlobalRWVisitor::VisitDeclRefExpr(DeclRefExpr* DR) {
    if(!isa<VarDecl>(DR->getDecl())) {
        return true;
    }
    const auto* VD = dyn_cast<VarDecl>(DR->getDecl());
    if(!isVariable(DR)) {
        return handleAccessedObject(DR, /*IsWrite*/false, /*IsUnchecked*/false);
    }
    if(isGlobalDecl(VD)) {
        addGlobal(VD->getDeclName(), VD->getBeginLoc(), /*IsWrite*/false, 
                  /*IsUnchecked*/false);
        return true;
    }
    return true;
}

bool GlobalRWVisitor::VisitMemberExpr(MemberExpr* ME) {
    return handleAccessedObject(ME, /*IsWrite*/false, /*IsUnchecked*/false);
}

bool
GlobalRWVisitor::handleModifiedVariable(const DeclRefExpr* DR,
                                        bool IsUnchecked) {
    if(!isa<VarDecl>(DR->getDecl())) {
        return true;
    }
    const auto* VD = dyn_cast<VarDecl>(DR->getDecl());

    if(isGlobalDecl(VD)) {
        addGlobal(VD->getDeclName(), VD->getBeginLoc(), /*IsWrite*/true,
                  IsUnchecked);
        return false;
    }
    return true;
}

bool
GlobalRWVisitor::handleAccessedObject(const Expr* E, bool IsWrite, 
                                      bool IsUnchecked) {
    const Expr* CurrentNode = E;
    int NodeCount = 0;
    while(isa<MemberExpr>(CurrentNode)) {
        const MemberExpr* CurrentField = dyn_cast<MemberExpr>(CurrentNode);
        if(CurrentField->isArrow()) {
            return true;
        }

        const ValueDecl* Decl = CurrentField->getMemberDecl();
        if(!isa<FieldDecl>(Decl)) {
            return true;
        }

        CurrentNode = CurrentField->getBase();
        NodeCount++;
    }

    if(!isa<DeclRefExpr>(CurrentNode)) {
        return true;
    }
    const DeclRefExpr* Base = dyn_cast<DeclRefExpr>(CurrentNode);

    if(!isa<VarDecl>(Base->getDecl())) {
        return true;
    }
    const VarDecl* BaseDecl = dyn_cast<VarDecl>(Base->getDecl());

    if(!isGlobalDecl(BaseDecl)) {
        return true;
    }

    FieldIndexArray FieldIndices(NodeCount);
    CurrentNode = E;
    while(isa<MemberExpr>(CurrentNode)) {
        const MemberExpr* CurrentField = dyn_cast<MemberExpr>(CurrentNode);
        const FieldDecl* Decl = 
            dyn_cast<FieldDecl>(CurrentField->getMemberDecl());

        FieldIndices[NodeCount - 1] = Decl->getFieldIndex();
        const RecordDecl* Record = Decl->getParent();
        if(Record && Record->isUnion()) {
            // Not sure what to do if Record isn't a value.
            FieldIndices[NodeCount - 1] |= FiUnion;
        }

        CurrentNode = CurrentField->getBase();
        NodeCount--;
    }

    addField(BaseDecl->getDeclName(), FieldIndices, Base->getBeginLoc(),
             IsWrite, IsUnchecked);
    return false;
}

bool
GlobalRWVisitor::handleModified(const Expr* Modified, bool IsUnchecked) {
    if(!Modified) {
        return true;
    }

    if(isVariable(Modified)) {
        return handleModifiedVariable(dyn_cast<DeclRefExpr>(Modified),
                                      IsUnchecked);
    }

    return handleAccessedObject(Modified, /*IsWrite*/true, IsUnchecked);
}

bool
GlobalRWVisitor::VisitUnaryOperator(UnaryOperator* Op) {
    UnaryOperator::Opcode Code = Op->getOpcode();
    if(Code == UO_PostInc || Code == UO_PostDec
    || Code == UO_PreInc  || Code == UO_PreDec) {
        return handleModified(Op->getSubExpr(), /*IsUnchecked*/false);
    }
    return true;
}

bool
GlobalRWVisitor::VisitBinaryOperator(BinaryOperator* Op) {
    if(Op->isAssignmentOp()) {
        return handleModified(Op->getLHS(), /*IsUnchecked*/false);
    }

    return true;
}

void
GlobalRWVisitor::visitCallExprArgs(CallExpr* CE) {
    const Type* CT = CE->getCallee()->getType().getTypePtrOrNull();
    if(const auto* PT = dyn_cast_if_present<PointerType>(CT)) {
        CT = PT->getPointeeType().getTypePtrOrNull();
    }
    const auto* ProtoType = dyn_cast_if_present<FunctionProtoType>(CT);

    for(uint32_t I = 0; I < CE->getNumArgs(); I++) {
        Expr* Arg = CE->getArg(I);

        if(!ProtoType || I >= ProtoType->getNumParams()) {
            continue;
        }

        if(const auto* Op = dyn_cast<UnaryOperator>(Arg)) {
            if(Op->getOpcode() != UO_AddrOf) {
                continue;
            }

            if(const auto* PtrType = dyn_cast_if_present<PointerType>(
                        ProtoType->getParamType(I).getTypePtrOrNull())) {
                if(PtrType->getPointeeType().isConstQualified()) {
                    continue;
                }

                if(handleModified(Op->getSubExpr(), /*IsUnchecked*/true)) {
                    continue;
                }
            }
        }

        if(const auto* RefType = dyn_cast_if_present<ReferenceType>(
                    ProtoType->getParamType(I).getTypePtrOrNull())) {
            if(RefType->getPointeeType().isConstQualified()) {
                continue;
            }

            if(handleModified(Arg, /*IsUnchecked*/true)) {
                continue;
            }
        }
    }
}

bool
GlobalRWVisitor::VisitCallExpr(CallExpr* CE) {
    visitCallExprArgs(CE);

    if(!isa_and_nonnull<FunctionDecl>(CE->getCalleeDecl())) {
        return true;
    }
    const auto* FD = dyn_cast<FunctionDecl>(CE->getCalleeDecl());

    if(!FD->hasBody()) {
        return true;
    }

    for(uint32_t I = 0; I < FunctionsChecked.size(); I++) {
        if(FunctionsChecked[I].first == FD->getDeclName()) {
            return true;
        }
    }
    FunctionsChecked.emplace_back(FD->getDeclName(), FD->getBody());

    return true;
}

const std::vector<TraversalAggregation>& 
GlobalRWVisitor::getGlobalsFound() const {
    return GlobalsFound;
}

const std::vector<ObjectTraversalAggregation>& 
GlobalRWVisitor::getObjectGlobalsFound() const {
    return ObjectGlobalsFound;
}

void
GlobalRWVisitor::addGlobal(DeclarationName Name, SourceLocation Loc,
                           bool IsWrite, bool IsUnchecked) {
    AccessKind Access = (IsInFunction || IsUnchecked) ? 
                            (IsWrite ? AkUncheckedWrite : AkUncheckedRead) :
                            (IsWrite ? AkWrite          : AkRead);
    for(uint32_t I = 0; I < GlobalsFound.size(); I++) {
        if(GlobalsFound[I].getDeclName() == Name) {
            GlobalsFound[I].addGlobalRW(Loc, Access, TraversalIndex);
            return;
        }
    }

    GlobalsFound.emplace_back(Name, Loc, Access, TraversalIndex);
}

void
GlobalRWVisitor::addGlobal(const DeclRefExpr* DR, bool IsWrite,
                           bool IsUnchecked) {
    const auto* VD = dyn_cast<VarDecl>(DR->getDecl());
    assert(VD); // Risky
    addGlobal(VD->getDeclName(), DR->getBeginLoc(), IsWrite, IsUnchecked);
}

void
GlobalRWVisitor::addField(DeclarationName Name, FieldIndexArray FieldIndices,
                          SourceLocation Loc, bool IsWrite, bool IsUnchecked) {
    AccessKind Access = (IsInFunction || IsUnchecked) ? 
                            (IsWrite ? AkUncheckedWrite : AkUncheckedRead) :
                            (IsWrite ? AkWrite          : AkRead);
    for(uint32_t I = 0; I < ObjectGlobalsFound.size(); I++) {
        if(ObjectGlobalsFound[I].getDeclName() == Name) {
            ObjectGlobalsFound[I].addFieldRW(Loc, FieldIndices, Access,
                                             TraversalIndex);
            return;
        }
    }
    ObjectGlobalsFound.emplace_back(Name, Loc, FieldIndices, Access,
                                    TraversalIndex);
}

TraversalResultKind ConflictingGlobalAccesses::akToTr(
        AccessKind Ak) {
    return 1 << Ak;
}

TraversalAggregation::TraversalAggregation() {}

TraversalAggregation::TraversalAggregation(DeclarationName Name,
                                           SourceLocation  Loc,
                                           AccessKind      Access,
                                           int             Index)
    : DeclName(Name), MainPart(Index, Loc, Access) {}

void
TraversalAggregation::addGlobalRW(SourceLocation Loc, AccessKind Access, 
                                  int Index) {
    if(!isValid()) {
        MainPart = TraversalResult(Index, Loc, Access);
        return;
    }
    if(isReportedByWunsequenced()) {
        return;
    }
    if(MainPart.IndexCreated == Index) {
        MainPart.addNewAccess(Loc, Access);
        return;
    }
    if(!hasTwoAccesses()) {
        OtherPart = TraversalResult(Index, Loc, Access);
        return;
    }
    if(OtherPart.IndexCreated == Index) {
        OtherPart.addNewAccess(Loc, Access);
        return;
    }
    // We know that the current state is not reported by -Wunsequenced, so
    // either one of the parts has only unchecked accesses, or both parts have
    // only reads.
    switch(Access) {
        case AkWrite:
        case AkUncheckedWrite: {
            if(OtherPart.Kind & (TrRead | TrWrite)) {
                MainPart = OtherPart;
            }
            OtherPart = TraversalResult(Index, Loc, Access);
            break;
        }
        case AkRead: {
            if(!(MainPart.Kind & TrWrite) 
            && (OtherPart.Kind & (TrWrite | TrUncheckedWrite))) {
                MainPart = OtherPart;
            }
            OtherPart = TraversalResult(Index, Loc, Access);
            break;
        }
        case AkUncheckedRead:
        default: {
            break;
        }
    }
}

bool
TraversalAggregation::isValid() const {
    return MainPart.Kind != TrInvalid;
}

DeclarationName
TraversalAggregation::getDeclName() const {
    return DeclName;
}

bool
TraversalAggregation::hasTwoAccesses() const {
    return OtherPart.Kind != TrInvalid;
}

bool
TraversalAggregation::hasConflictingOperations() const {
    return hasTwoAccesses() 
        && (MainPart.Kind | OtherPart.Kind) & (TrWrite | TrUncheckedWrite);
}

bool
TraversalAggregation::isReportedByWunsequenced() const {
    return ((MainPart.Kind | OtherPart.Kind) & TrWrite)
        && (MainPart.Kind  & (TrWrite | TrRead))
        && (OtherPart.Kind & (TrWrite | TrRead));
}

bool
TraversalAggregation::shouldBeReported() const {
    return hasConflictingOperations() 
        && !isReportedByWunsequenced();
}

TraversalResult::TraversalResult() : IndexCreated(-1), Kind(TrInvalid) {}

TraversalResult::TraversalResult(int Index, SourceLocation Location,
                                 AccessKind Access)
        : IndexCreated(Index), Kind(akToTr(Access)) {
    Loc[Access] = Location;
}

void
TraversalResult::addNewAccess(SourceLocation NewLoc, AccessKind Access) {
    Kind |= 1 << Access;
    Loc[Access] = NewLoc;
}

ObjectTraversalAggregation::ObjectTraversalAggregation(
        DeclarationName Name, SourceLocation Loc, FieldIndexArray FieldIndices,
        AccessKind Access, int Index)
        : DeclName(Name), AccessTree(TraversalAggregation()) {
    addFieldRW(Loc, FieldIndices, Access, Index);
}

void
ObjectTraversalAggregation::addFieldRW(SourceLocation Loc, 
                                       FieldIndexArray FieldIndices,
                                       AccessKind Access, int Index) {
    ObjectAccessTree* CurrentNode = &AccessTree;
    for(uint16_t I = 0; I < FieldIndices.size(); I++) {
        bool     IsUnion  = (FieldIndices[I] &  FiUnion) != 0;
        uint16_t FieldKey =  FieldIndices[I] & ~FiUnion;

        ObjectAccessTree* PrevNode = CurrentNode;
        FieldMap::iterator It = CurrentNode->Fields.find(FieldKey);
        if(It == CurrentNode->Fields.end()) {
            CurrentNode = new ObjectAccessTree(IsUnion 
                        ? CurrentNode->UnionTemporalAccesses 
                        : CurrentNode->OwnAccesses);
            PrevNode->Fields[FieldKey] = 
                std::unique_ptr<ObjectAccessTree>(CurrentNode);
        } else {
            CurrentNode = It->second.get();
        }

        if(IsUnion) {
            if(!PrevNode->IsUnion) {
                PrevNode->IsUnion = IsUnion; // Setting the parent of the 
                                             // field instead of the field
                                             // itself.
                PrevNode->UnionTemporalAccesses = PrevNode->OwnAccesses;
            }
            PrevNode->addFieldToAllExcept(FieldKey, Loc, Access, Index);
        }

    }
    CurrentNode->addFieldToAll(Loc, Access, Index);
}

bool
ObjectTraversalAggregation::shouldBeReported() const {
    return shouldBeReportedRec(&AccessTree);
}

bool
ObjectTraversalAggregation::shouldBeReportedRec(const ObjectAccessTree* Node) 
    const {
    if(Node->OwnAccesses.hasConflictingOperations()) {
        return true;
    }
    FieldMap::const_iterator FieldIt = Node->Fields.begin();
    for(; FieldIt != Node->Fields.end(); FieldIt++) {
        if(shouldBeReportedRec(FieldIt->second.get())) {
            return true;
        }
    }
    return false;
}

DeclarationName
ObjectTraversalAggregation::getDeclName() const {
    return DeclName;
}

ObjectAccessTree::ObjectAccessTree(TraversalAggregation Own)
    : OwnAccesses(Own), IsUnion(false) {}

void
ObjectAccessTree::addFieldToAll(SourceLocation Loc, AccessKind Access,
                                int Index) {
    OwnAccesses.addGlobalRW(Loc, Access, Index);
    UnionTemporalAccesses.addGlobalRW(Loc, Access, Index);
    FieldMap::const_iterator FieldIt = Fields.begin();
    for(; FieldIt != Fields.end(); FieldIt++) {
        FieldIt->second->addFieldToAll(Loc, Access, Index);
    }
}

void
ObjectAccessTree::addFieldToAllExcept(uint16_t ExceptIndex, SourceLocation Loc,
                                      AccessKind Access, int Index) {

    UnionTemporalAccesses.addGlobalRW(Loc, Access, Index);
    FieldMap::const_iterator FieldIt = Fields.begin();
    for(; FieldIt != Fields.end(); FieldIt++) {
        if(FieldIt->first != ExceptIndex) {
            FieldIt->second->addFieldToAll(Loc, Access, Index);
        }
    }
}

} // namespace clang::tidy::bugprone
