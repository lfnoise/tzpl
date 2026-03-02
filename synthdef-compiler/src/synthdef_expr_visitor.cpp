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
void Inlet::accept(ExprVisitor& visitor) { visitor.visit(this); }
void Outlet::accept(ExprVisitor& visitor) { visitor.visit(this); }
void UnaryOpExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BinaryOpExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void CompareOpExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void CastOpExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
//void MatMulExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void ReduceExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
//void ScanExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void VarExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void PhiNodeExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void SelectExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void IfElseExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void SwitchExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void ForLoopExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }

#if 0
void MatAt::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatPut::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatPermute::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatReverse::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatTake::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatSkip::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatStride::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatStutter::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatShift::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatRotate::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatTranspose::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatCyc::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatCat::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatLace::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MatReshape::accept(ExprVisitor& visitor) { visitor.visit(this); }
#endif

void URandExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void BiRandExpr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void Rand64Expr::accept(ExprVisitor& visitor) { visitor.visit(this); }
void MaxDelay::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayFixRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayVarRead::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayWrite::accept(ExprVisitor& visitor) { visitor.visit(this); }
void DelayInit::accept(ExprVisitor& visitor) { visitor.visit(this); }

}
