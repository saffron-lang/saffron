#include "ast.h"


void initTypeNodeArray(TypeNodeArray* typeNodeArray) {
    typeNodeArray->count = 0;
    typeNodeArray->capacity = 0;
    typeNodeArray->typeNodes = NULL;
}      
        
void writeTypeNodeArray(TypeNodeArray * typeNodeArray, TypeNode* typeNode) {
    if (typeNodeArray->capacity < typeNodeArray->count + 1) {
        int oldCapacity = typeNodeArray->capacity;
        typeNodeArray->capacity = GROW_CAPACITY(oldCapacity);
        typeNodeArray->typeNodes = GROW_ARRAY(TypeNode*, typeNodeArray->typeNodes,
                                       oldCapacity, typeNodeArray->capacity);
    }

    typeNodeArray->typeNodes[typeNodeArray->count] = typeNode;
    typeNodeArray->count++;
}

void freeTypeNodeArray(TypeNodeArray * typeNodeArray) {
    FREE_ARRAY(TypeNode*, typeNodeArray->typeNodes, typeNodeArray->capacity);
    initTypeNodeArray(typeNodeArray);
}


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


void initParameterArray(ParameterArray* parameterArray) {
    parameterArray->count = 0;
    parameterArray->capacity = 0;
    parameterArray->parameters = NULL;
}      
        
void writeParameterArray(ParameterArray * parameterArray, Parameter* parameter) {
    if (parameterArray->capacity < parameterArray->count + 1) {
        int oldCapacity = parameterArray->capacity;
        parameterArray->capacity = GROW_CAPACITY(oldCapacity);
        parameterArray->parameters = GROW_ARRAY(Parameter*, parameterArray->parameters,
                                       oldCapacity, parameterArray->capacity);
    }

    parameterArray->parameters[parameterArray->count] = parameter;
    parameterArray->count++;
}

void freeParameterArray(ParameterArray * parameterArray) {
    FREE_ARRAY(Parameter*, parameterArray->parameters, parameterArray->capacity);
    initParameterArray(parameterArray);
}

