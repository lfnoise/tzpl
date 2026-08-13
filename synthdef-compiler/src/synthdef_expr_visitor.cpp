// Tzopilotl
// Copyright (C) 2026 James McCartney
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
//  synthdef_expr_visitor.cpp
//  synthdef-compiler
//
//  Created by James McCartney on 11/8/24.
//

#include "synthdef_expr_visitor.hpp"

namespace synthdef {

void Constant::accept(ExprVisitor& visitor) { visitor.visit(this); }
void SampleRate::accept(ExprVisitor& visitor) { visitor.visit(this); }
void SampleDur::accept(ExprVisitor& visitor) { visitor.visit(this); }
void SharedInExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void Control::accept(ExprVisitor& visitor) { visitor.visit(this); }
void NoteParam::accept(ExprVisitor& visitor) { visitor.visit(this); }
void Inlet::accept(ExprVisitor& visitor) { visitor.visit(this); }
void Outlet::accept(ExprVisitor& visitor) { visitor.visit(this); }
void UnaryOpExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BinaryOpExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void CompareOpExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void CastOpExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
//void MatMulExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void ReduceExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
//void ScanExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecTakeExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecDropExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecStrideExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecStutterExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecNCycExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecReverseExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecTransposeExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecRotateExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecAtExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecPutExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VecJoinExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VarExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void PhiNodeExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void SelectExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void IfElseExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void SwitchExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void ForLoopExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VoicerExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void SpectralFrameInput::accept(ExprVisitor& visitor) { visitor.visit(this); }
void SpectralChainExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }


void URandExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BiRandExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void Rand64Expr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MaxDelay::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayFixRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayVarRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayWrite::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayInit::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BufFixRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BufVarRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BufWrite::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BufLength::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BankLookup::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BankFixRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BankVarRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BankRootKey::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BankSampleRate::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BankLength::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DebugExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }

}
