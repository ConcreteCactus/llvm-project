//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SideEffectOutsideConeOfEvaluationCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Analysis/FlowSensitive/Models/SideEffectOutsideConeOfEvaluationModel.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

void SideEffectOutsideConeOfEvaluationCheck::registerMatchers(MatchFinder *Finder) {
  // FIXME: Add matchers.

  auto Somewhere = [](const ast_matchers::internal::Matcher<Stmt>& M) {
      return anyOf(M, hasDescendant(M));
  };
  const ast_matchers::internal::Matcher<Stmt>& Interest =
      anyOf(Somewhere(unaryOperator().bind("x")),
            Somewhere(binaryOperator().bind("x")),
            Somewhere(callExpr().bind("x")));

  const ast_matchers::internal::Matcher<Stmt>& SMatcher = 
      callExpr(callee(functionDecl(
                      hasName("contract_assert"))),
                      hasArgument(0, Interest));
  Finder->addMatcher(SMatcher, this);
}

void SideEffectOutsideConeOfEvaluationCheck::check(
        const MatchFinder::MatchResult &Result)
{
  using Model = dataflow::SideEffectOutsideConeOfEvaluationModel;
  using Diagnoser = dataflow::SideEffectOutsideConeOfEvaluationDiagnoser;
  using Diagnostic = dataflow::SideEffectOutsideConeOfEvaluationDiagnostic;

  const auto *E = Result.Nodes.getNodeAs<Expr>("x");

  if (const auto* CallE = dyn_cast<CallExpr>(E)) {
      if (const auto* D = dyn_cast<FunctionDecl>(CallE->getCalleeDecl())) {
          Diagnoser Diagnoser;
          auto SideEffects = dataflow::diagnoseFunction<Model, Diagnostic>(
                  *D, *Result.Context, Diagnoser);
          if (SideEffects) {
              for (const Diagnostic& SE : *SideEffects) {
                  diag(SE.Range.getBegin(), "side effect outside cone of eval");
              }
          }
      }
  }

  if (const auto* BinOp = dyn_cast<BinaryOperator>(E)) {
      if (BinOp->isAssignmentOp()) {
        diag(BinOp->getBeginLoc(), "side effect outside cone of eval");
      }
      return;
  }

  if (const auto* UnOp = dyn_cast<UnaryOperator>(E)) {
      UnaryOperator::Opcode Op = UnOp->getOpcode();
      if (Op == UO_PreInc || Op == UO_PreDec || Op == UO_PostInc
              || Op == UO_PostDec) {
        diag(UnOp->getBeginLoc(), "side effect outside cone of eval");
      }
      return;
  }
}

} // namespace clang::tidy::bugprone
