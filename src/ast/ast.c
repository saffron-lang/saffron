#include "ast.h"


void initExprArray(ExprArray* exprArray) {
    exprArray->count = 0;
    exprArray->capacity = 0;
    exprArray->exprs = NULL;
}      
        
void writeExprArray(ExprArray * exprArray, Expr* expr) {
    if (exprArray->capacity < exprArray->count + 1) {
        int oldCapacity = exprArray->capacity;
        exprArray->capacity = GROW_CAPACITY(oldCapacity);
        exprArray->exprs = GROW_ARRAY(Expr*, exprArray->exprs,
                                       oldCapacity, exprArray->capacity);
    }

    exprArray->exprs[exprArray->count] = expr;
    exprArray->count++;
}

void freeExprArray(ExprArray * exprArray) {
    FREE_ARRAY(Expr*, exprArray->exprs, exprArray->capacity);
    initExprArray(exprArray);
}
        

void initStmtArray(StmtArray* stmtArray) {
    stmtArray->count = 0;
    stmtArray->capacity = 0;
    stmtArray->stmts = NULL;
}      
        
void writeStmtArray(StmtArray * stmtArray, Stmt* stmt) {
    if (stmtArray->capacity < stmtArray->count + 1) {
        int oldCapacity = stmtArray->capacity;
        stmtArray->capacity = GROW_CAPACITY(oldCapacity);
        stmtArray->stmts = GROW_ARRAY(Stmt*, stmtArray->stmts,
                                       oldCapacity, stmtArray->capacity);
    }

    stmtArray->stmts[stmtArray->count] = stmt;
    stmtArray->count++;
}

void freeStmtArray(StmtArray * stmtArray) {
    FREE_ARRAY(Stmt*, stmtArray->stmts, stmtArray->capacity);
    initStmtArray(stmtArray);
}
        
