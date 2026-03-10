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


void URandExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BiRandExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void Rand64Expr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MaxDelay::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayFixRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayVarRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayWrite::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayInit::accept(ExprVisitor& visitor) { visitor.visit(this); }

}
