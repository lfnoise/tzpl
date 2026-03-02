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
    virtual void visit(Inlet* e) = 0;
    virtual void visit(Outlet* e) = 0;
    virtual void visit(UnaryOpExpr* e) = 0;
    virtual void visit(BinaryOpExpr* e) = 0;
    virtual void visit(CompareOpExpr* e) = 0;
    virtual void visit(CastOpExpr* e) = 0;
//    virtual void visit(MatMulExpr* e) = 0;
    virtual void visit(ReduceExpr* e) = 0;
//    virtual void visit(ScanExpr* e) = 0;
    virtual void visit(VarExpr* e) = 0;
    virtual void visit(PhiNodeExpr* e) = 0;
    virtual void visit(SelectExpr* e) = 0;
    virtual void visit(IfElseExpr* e) = 0;
    virtual void visit(SwitchExpr* e) = 0;
    virtual void visit(ForLoopExpr* e) = 0;
//    virtual void visit(MatAt* e) = 0;
//    virtual void visit(MatPut* e) = 0;
//    virtual void visit(MatPermute* e) = 0;
//    virtual void visit(MatReverse* e) = 0;
//    virtual void visit(MatTake* e) = 0;
//    virtual void visit(MatSkip* e) = 0;
//    virtual void visit(MatStride* e) = 0;
//    virtual void visit(MatStutter* e) = 0;
//    virtual void visit(MatRotate* e) = 0;
//    virtual void visit(MatShift* e) = 0;
//    virtual void visit(MatTranspose* e) = 0;
//    virtual void visit(MatCyc* e) = 0;
//    virtual void visit(MatCat* e) = 0;
//    virtual void visit(MatLace* e) = 0;
//    virtual void visit(MatReshape* e) = 0;
    virtual void visit(URandExpr* e) = 0;
    virtual void visit(BiRandExpr* e) = 0;
    virtual void visit(Rand64Expr* e) = 0;
    virtual void visit(MaxDelay* e) = 0;
    virtual void visit(DelayFixRead* e) = 0;
    virtual void visit(DelayVarRead* e) = 0;
    virtual void visit(DelayWrite* e) = 0;
    virtual void visit(DelayInit* e) = 0;
};

} // namespace synthdef

#endif /* synthdef_expr_visitor_hpp */
