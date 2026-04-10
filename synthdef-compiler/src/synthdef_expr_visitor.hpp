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
//  synthdef_expr_visitor.hpp
//  synthdef-compiler
//
//  Created by James McCartney on 11/8/24.
//

#ifndef synthdef_expr_visitor_hpp
#define synthdef_expr_visitor_hpp

#include "synthdef_expr.hpp"
#include "synthdef_matrix.hpp"

namespace synthdef {

struct ExprVisitor {
    virtual ~ExprVisitor() = default;
    
    virtual void visit(Constant* e) = 0;
    virtual void visit(SampleRate* e) = 0;
    virtual void visit(SampleDur* e) = 0;
    virtual void visit(Control* e) = 0;
    virtual void visit(NoteParam* e) = 0;
    virtual void visit(Inlet* e) = 0;
    virtual void visit(Outlet* e) = 0;
    virtual void visit(UnaryOpExpr* e) = 0;
    virtual void visit(BinaryOpExpr* e) = 0;
    virtual void visit(CompareOpExpr* e) = 0;
    virtual void visit(CastOpExpr* e) = 0;
//    virtual void visit(MatMulExpr* e) = 0;
    virtual void visit(ReduceExpr* e) = 0;
//    virtual void visit(ScanExpr* e) = 0;
    virtual void visit(VecTakeExpr* e) = 0;
    virtual void visit(VecDropExpr* e) = 0;
    virtual void visit(VecStrideExpr* e) = 0;
    virtual void visit(VecStutterExpr* e) = 0;
    virtual void visit(VecNCycExpr* e) = 0;
    virtual void visit(VecReverseExpr* e) = 0;
    virtual void visit(VecTransposeExpr* e) = 0;
    virtual void visit(VecRotateExpr* e) = 0;
    virtual void visit(VecAtExpr* e) = 0;
    virtual void visit(VecPutExpr* e) = 0;
    virtual void visit(VecJoinExpr* e) = 0;
    virtual void visit(VarExpr* e) = 0;
    virtual void visit(PhiNodeExpr* e) = 0;
    virtual void visit(SelectExpr* e) = 0;
    virtual void visit(IfElseExpr* e) = 0;
    virtual void visit(SwitchExpr* e) = 0;
    virtual void visit(ForLoopExpr* e) = 0;
    virtual void visit(VoicerExpr* e) = 0;
    virtual void visit(SpectralFrameInput* e) = 0;
    virtual void visit(SpectralChainExpr* e) = 0;
    virtual void visit(URandExpr* e) = 0;
    virtual void visit(BiRandExpr* e) = 0;
    virtual void visit(Rand64Expr* e) = 0;
    virtual void visit(MaxDelay* e) = 0;
    virtual void visit(DelayFixRead* e) = 0;
    virtual void visit(DelayVarRead* e) = 0;
    virtual void visit(DelayWrite* e) = 0;
    virtual void visit(DelayInit* e) = 0;
    virtual void visit(BufFixRead* e) = 0;
    virtual void visit(BufVarRead* e) = 0;
    virtual void visit(BufWrite* e) = 0;
    virtual void visit(BufLength* e) = 0;
    virtual void visit(DebugExpr* e) = 0;
};

} // namespace synthdef

#endif /* synthdef_expr_visitor_hpp */
