#include <printf.h>
#include "astprint.h"
#include "../object.h"

int indent = 0;

void printIndent() {
    for (int i = 0; i < indent; i++) {
        printf("    ");
    }
}

void astUnparse(StmtArray *statements) {
    if (statements == NULL) {
        return;
    }
    for (int i = 0; i < statements->count; i++) {
        unparseNode((Node *) statements->stmts[i]);
        printf("\n");
    }
}

static void unparseToken(Token token) {
    for (int i = 0; i < token.length; i++) {
        printf("%c", token.start[i]);
    }
}
static void printToken(Token token) {
    printf("Token(\"");
    for (int i = 0; i < token.length; i++) {
        printf("%c", token.start[i]);
    }
    printf("\")");
}

static void unparseExprArray(ExprArray exprArray) {
    for (int i = 0; i < exprArray.count; i++) {
        unparseNode((Node *) exprArray.exprs[i]);
        if (i != exprArray.count - 1) {
            printf(", ");
        }
    }
}

static void printExprArray(ExprArray exprArray) {
    if (exprArray.count == 0) {
        printf("[]");
        return;
    }
    printf("[\n");
    indent++;
    for (int i = 0; i < exprArray.count; i++) {
        printIndent();
        printNode((Node *) exprArray.exprs[i]);
        if (i != exprArray.count - 1) {
            printf(",\n");
        }
    }

    indent--;
    printf("\n");
    printIndent();
    printf("]");
}

static void printTypeArray(TypeArray typeArray) {
    if (typeArray.count == 0) {
        printf("[]");
        return;
    }
    printf("[\n");
    indent++;
    for (int i = 0; i < typeArray.count; i++) {
        printIndent();
        printNode((Node *) typeArray.types[i]);
        if (i != typeArray.count - 1) {
            printf(",\n");
        }
    }

    indent--;
    printf("\n");
    printIndent();
    printf("]");
}

static void unparseTokenArray(TokenArray tokenArray) {
    for (int i = 0; i < tokenArray.count; i++) {
        unparseToken(tokenArray.tokens[i]);
        if (i != tokenArray.count - 1) {
            printf(", ");
        }
    }
}

static void printTokenArray(TokenArray tokenArray) {
    if (tokenArray.count == 0) {
        printf("[]");
        return;
    }
    printf("[\n");
    indent++;
    for (int i = 0; i < tokenArray.count; i++) {
        printIndent();
        printToken(tokenArray.tokens[i]);
        if (i != tokenArray.count - 1) {
            printf(",\n");
        }
    }
    indent--;
    printf("\n");
    printIndent();
    printf("]");
}

static void printFunctionType(FunctionType type) {
    printf("%d", type);
}

void printTree(StmtArray *statements) {
    if (statements == NULL) {
        return;
    }
    if (statements->count == 0) {
        printf("[]");
        return;
    }
    printf("[\n");
    indent++;
    for (int i = 0; i < statements->count; i++) {
        printIndent();
        printNode((Node *) statements->stmts[i]);
        if (i != statements->count - 1) {
            printf(",\n");
        }
    }

    indent--;
    printf("\n");
    printIndent();
    printf("]");
    printf("\n");
}

void unparseNode(Node *node) {
    if (node == NULL) {
        printf("NULL");
        return;
    }
    switch (node->type) {
        case NODE_BINARY: {
            struct Binary *casted = (struct Binary *) node;
            unparseNode((Node *) casted->left);
            printf(" ");
            unparseToken(casted->operator);
            printf(" ");
            unparseNode((Node *) casted->right);
            break;
        }
        case NODE_GROUPING: {
            struct Grouping *casted = (struct Grouping *) node;
            printf("(");
            unparseNode((Node *) casted->expression);
            printf(")");
            break;
        }
        case NODE_LITERAL: {
            struct Literal *casted = (struct Literal *) node;
            if (casted->value.type == VAL_OBJ) {
                printf("\"");
                printValue(casted->value);
                printf("\"");
            } else {
                printValue(casted->value);
            }
            break;
        }
        case NODE_UNARY: {
            struct Unary *casted = (struct Unary *) node;
            unparseToken(casted->operator);
            unparseNode((Node *) casted->right);
            break;
        }
        case NODE_VARIABLE: {
            struct Variable *casted = (struct Variable *) node;
            unparseToken(casted->name);
            break;
        }
        case NODE_ASSIGN: {
            struct Assign *casted = (struct Assign *) node;
            printIndent();
            unparseToken(casted->name);
            printf(" = ");
            unparseNode((Node *) casted->value);
            break;
        }
        case NODE_LOGICAL: {
            struct Logical *casted = (struct Logical *) node;
            unparseNode((Node *) casted->left);
            printf(" ");
            unparseToken(casted->operator);
            printf(" ");
            unparseNode((Node *) casted->right);
            break;
        }
        case NODE_CALL: {
            struct Call *casted = (struct Call *) node;
            unparseNode((Node *) casted->callee);
            printf("(");
            unparseExprArray(casted->arguments);
            printf(")");
            break;
        }
        case NODE_GET: {
            struct Get *casted = (struct Get *) node;
            unparseNode((Node *) casted->object);
            printf(".");
            unparseToken(casted->name);
            break;
        }
        case NODE_SET: {
            struct Set *casted = (struct Set *) node;
            unparseNode((Node *) casted->object);
            printf(".");
            unparseToken(casted->name);
            printf(" = ");
            unparseNode((Node *) casted->value);
            printf(";");
            break;
        }
        case NODE_SUPER: {
            struct Super *casted = (struct Super *) node;
            printf("super.");
            unparseToken(casted->method);
            break;
        }
        case NODE_THIS:
            printf("this");
            break;
        case NODE_YIELD: {
            struct Yield *casted = (struct Yield *) node;
            printf("yield");
            if (casted->expression) {
                printf(" ");
                unparseNode((Node *) casted->expression);
            }
            break;
        }
        case NODE_LAMBDA: {
            struct Lambda *casted = (struct Lambda *) node;
            printf("fun (");
            unparseTokenArray(casted->params);
            printf(") => {\n");
            indent++;
            astUnparse(&casted->body);
            indent--;
            printf("}");
            break;
        }
        case NODE_LIST: {
            struct List *casted = (struct List *) node;
            printf("[");
            unparseExprArray(casted->items);
            printf("]");
            break;
        }
        case NODE_EXPRESSION: {
            struct Expression *casted = (struct Expression *) node;
            printIndent();
            unparseNode((Node *) casted->expression);
            printf(";");
            break;
        }
        case NODE_VAR: {
            struct Var *casted = (struct Var *) node;
            printIndent();
            printf("var ");
            unparseToken(casted->name);
            if (casted->type) {
                printf(": ");
                unparseNode((Node *) casted->type);
            }
            if (casted->initializer) {
                printf(" = ");
                unparseNode((Node *) casted->initializer);
            }
            printf(";");
            break;
        }
        case NODE_BLOCK: {
            struct Block *casted = (struct Block *) node;
            printIndent();
            printf("{\n");
            indent++;
            astUnparse(&casted->statements);
            indent--;
            printIndent();
            printf("}");
            break;
        }
        case NODE_FUNCTION: {
            struct Function *casted = (struct Function *) node;
            printIndent();
            if (casted->functionType == TYPE_FUNCTION) {
                printf("fun ");
            }

            unparseToken(casted->name);
            printf("(");
            unparseTokenArray(casted->params);
            printf(") {\n");
            indent++;
            astUnparse(&casted->body);
            indent--;
            printIndent();
            printf("}");
            break;
        }
        case NODE_CLASS: {
            struct Class *casted = (struct Class *) node;
            printIndent();
            printf("class ");
            unparseToken(casted->name);
            if (casted->superclass) {
                printf("< ");
                unparseNode((Node *) casted->superclass);
            }

            printf(" {\n");
            indent++;
            for (int i = 0; i < casted->methods.count; i++) {
                unparseNode((Node *) casted->methods.stmts[i]);
                if (i != casted->methods.count - 1) {
                    printf("\n\n");
                }
            }
            indent--;
            printf("\n");
            printIndent();
            printf("}");
            break;
        }
        case NODE_IF: {
            struct If *casted = (struct If *) node;
            printIndent();
            printf("if (");
            unparseNode((Node *) casted->condition);
            printf(")\n");
            indent++;
            unparseNode((Node *) casted->thenBranch);
            indent--;

            if (casted->elseBranch) {
                printIndent();
                printf("else ");
                indent++;
                unparseNode((Node *) casted->elseBranch);
                indent--;
            }
            break;
        }
        case NODE_WHILE: {
            struct While *casted = (struct While *) node;
            printIndent();
            printf("while (");
            unparseNode((Node *) casted->condition);
            printf(")\n");
            indent++;
            unparseNode((Node *) casted->body);
            indent--;
            break;
        }
        case NODE_FOR: {
            struct For *casted = (struct For *) node;
            printIndent();
            printf("for (");
            unparseNode((Node *) casted->initializer);
            unparseNode((Node *) casted->condition);
            printf(";");
            unparseNode((Node *) casted->increment);
            printf(")\n");
            indent++;
            unparseNode((Node *) casted->body);
            indent--;
            break;
        }
        case NODE_BREAK:
            printIndent();
            printf("break;");
            break;
        case NODE_RETURN: {
            struct Return *casted = (struct Return *) node;
            printIndent();
            printf("return ");
            unparseNode((Node *) casted->value);
            printf(";");
            break;
        }
        case NODE_IMPORT: {
            struct Import *casted = (struct Import *) node;
            printIndent();
            printf("import ");
            unparseNode((Node *) casted->expression);
            printf(";");
            break;
        }
        case NODE_FUNCTOR: {
            struct Functor *casted = (struct Functor *) node;
            printf("(");
            for (int i = 0; i < casted->arguments.count; i++) {
                unparseNode((Node *) casted->arguments.types[i]);
                if (i != casted->arguments.count-  1) {
                    printf(", ");
                }
            }
            printf(") => ");
            unparseNode((Node *) casted->returnType);
            break;
        }
        case NODE_SIMPLE: {
            struct Simple *casted = (struct Simple *) node;
            unparseToken(casted->name);
            break;
        }
    }
}



void printNode(Node *node) {
    if (node==NULL) {
        printf("NULL");
        return;
    }

    switch (node->type) {
        case NODE_BINARY: {
            struct Binary *casted = (struct Binary *) node;
            printf("Binary(\n");
            indent++;
            printIndent();
            printf("left=");
            printNode((Node *) casted->left);
            printf(",\n");
            printIndent();
            printf("right=");
            printNode((Node *) casted->right);
            printf(",\n");
            printIndent();
            printf("op=");
            printToken(casted->operator);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_GROUPING: {
            struct Grouping *casted = (struct Grouping *) node;
            printf("Grouping(\n");
            indent++;
            printIndent();
            printf("expression=");
            printNode((Node *) casted->expression);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_LITERAL: {
            struct Literal *casted = (struct Literal *) node;
            printf("Literal(\n");
            indent++;
            printIndent();
            printf("value=");
            if (casted->value.type == VAL_OBJ) {
                printf("\"");
                printValue(casted->value);
                printf("\"");
            } else {
                printValue(casted->value);
            }
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_UNARY: {
            struct Unary *casted = (struct Unary *) node;
            printf("Unary(\n");
            indent++;
            printIndent();
            printf("right=");
            printNode((Node *) casted->right);
            printIndent();
            printf("operator=");
            printToken(casted->operator);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_VARIABLE: {
            struct Variable *casted = (struct Variable *) node;
            printf("Variable(\n");
            indent++;
            printIndent();
            printf("name=");
            printToken(casted->name);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_ASSIGN: {
            struct Assign *casted = (struct Assign *) node;
            printf("Unary(\n");
            indent++;
            printIndent();
            printf("value=");
            printNode((Node *) casted->value);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_LOGICAL: {
            struct Logical *casted = (struct Logical *) node;
            printf("Logical(\n");
            indent++;
            printIndent();
            printf("left=");
            printNode((Node *) casted->left);
            printf(",\n");
            printIndent();
            printf("right=");
            printNode((Node *) casted->right);
            printf(",\n");
            printIndent();
            printf("op=");
            printToken(casted->operator);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_CALL: {
            struct Call *casted = (struct Call *) node;
            printf("Call(\n");
            indent++;
            printIndent();
            printf("callee=");
            printNode((Node *) casted->callee);
            printf(",\n");
            printIndent();
            printf("arguments=");
            printExprArray(casted->arguments);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_GET: {
            struct Get *casted = (struct Get *) node;
            printf("Get(\n");
            indent++;
            printIndent();
            printf("object=");
            printNode((Node *) casted->object);
            printf(",\n");
            printIndent();
            printf("name=");
            printToken(casted->name);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_SET: {
            struct Set *casted = (struct Set *) node;
            printf("Set(\n");
            indent++;
            printIndent();
            printf("object=");
            printNode((Node *) casted->object);
            printf(",\n");
            printIndent();
            printf("value=");
            printNode((Node *) casted->value);
            printf(",\n");
            printIndent();
            printf("name=");
            printToken(casted->name);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_SUPER: {
            struct Super *casted = (struct Super *) node;
            printf("Super(\n");
            indent++;
            printIndent();
            printf("keyword=");
            printToken(casted->keyword);
            printf(",\n");
            printIndent();
            printf("method=");
            printToken(casted->method);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_THIS: {
            struct This *casted = (struct This *) node;
            printf("This(\n");
            indent++;
            printIndent();
            printf("keyword=");
            printToken(casted->keyword);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_YIELD: {
            struct Yield *casted = (struct Yield *) node;
            printf("Yield(\n");
            indent++;
            printIndent();
            printf("expression=");
            printNode((Node *) casted->expression);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_LAMBDA: {
            struct Lambda *casted = (struct Lambda *) node;
            printf("Lambda(\n");
            indent++;
            printIndent();
            printf("params=");
            printTokenArray(casted->params);
            printf(",\n");
            printIndent();
            printf("body=");
            printTree(&casted->body);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_LIST: {
            struct List *casted = (struct List *) node;
            printf("List(\n");
            indent++;
            printIndent();
            printf("items=");
            printExprArray(casted->items);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_EXPRESSION: {
            struct Expression *casted = (struct Expression *) node;
            printf("Expression(\n");
            indent++;
            printIndent();
            printf("expression=");
            printNode((Node *) casted->expression);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_VAR: {
            struct Var *casted = (struct Var *) node;
            printf("Var(\n");
            indent++;
            printIndent();
            printf("name=");
            printToken(casted->name);
            printf(",\n");
            printIndent();
            printf("initializer=");
            printNode((Node *) casted->initializer);
            printf(",\n");
            printIndent();
            printf("type=");
            printNode((Node *) casted->type);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_BLOCK: {
            struct Block *casted = (struct Block *) node;
            printf("Block(\n");
            indent++;
            printIndent();
            printf("statements=");
            printTree(&casted->statements);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_FUNCTION: {
            struct Function *casted = (struct Function *) node;
            printf("Function(\n");
            indent++;
            printIndent();
            printf("params=");
            printTokenArray(casted->params);
            printf(",\n");
            printIndent();
            printf("name=");
            printToken(casted->name);
            printf(",\n");
            printIndent();
            printf("functionType=");
            printFunctionType(casted->functionType);
            printf(",\n");
            printIndent();
            printf("body=");
            printTree(&casted->body);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_CLASS: {
            struct Class *casted = (struct Class *) node;
            printf("Class(\n");
            indent++;
            printIndent();
            printf("name=");
            printToken(casted->name);
            printf(",\n");
            printIndent();
            printf("superclass=");
            printNode((Node *) casted->superclass);
            printf(",\n");
            printIndent();
            printf("methods=");
            printTree(&casted->methods);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_IF: {
            struct If *casted = (struct If *) node;
            printf("If(\n");
            indent++;
            printIndent();
            printf("condition=");
            printNode((Node *) casted->condition);
            printf(",\n");
            printIndent();
            printf("thenBranch=");
            printNode(casted->thenBranch);
            printf(",\n");
            printIndent();
            printf("elseBranch=");
            printNode(casted->elseBranch);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_WHILE: {
            struct While *casted = (struct While *) node;
            printf("While(\n");
            indent++;
            printIndent();
            printf("condition=");
            printNode((Node *) casted->condition);
            printf(",\n");
            printIndent();
            printf("body=");
            printNode((Node *) casted->body);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_FOR: {
            struct For *casted = (struct For *) node;
            printf("For(\n");
            indent++;
            printIndent();
            printf("initializer=");
            printNode((Node *) casted->initializer);
            printf(",\n");
            printIndent();
            printf("condition=");
            printNode((Node *) casted->condition);
            printf(",\n");
            printIndent();
            printf("increment=");
            printNode((Node *) casted->increment);
            printf(",\n");
            printIndent();
            printf("body=");
            printNode((Node *) casted->body);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_BREAK:
            printf("Break()");
            break;
        case NODE_RETURN: {
            struct Return *casted = (struct Return *) node;
            printf("Return(\n");
            indent++;
            printIndent();
            printf("value=");
            printNode((Node *) casted->value);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_IMPORT: {
            struct Import *casted = (struct Import *) node;
            printf("Import(\n");
            indent++;
            printIndent();
            printf("expression=");
            printNode((Node *) casted->expression);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_FUNCTOR: {
            struct Functor *casted = (struct Functor *) node;
            printf("Functor(\n");
            indent++;
            printIndent();
            printf("arguments=");
            printTypeArray(casted->arguments);
            printf("\n");
            printIndent();
            printf("returnType=");
            printNode((Node *) casted->returnType);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
        case NODE_SIMPLE: {
            struct Simple *casted = (struct Simple *) node;
            printf("Simple(\n");
            indent++;
            printIndent();
            printf("name=");
            printToken(casted->name);
            indent--;
            printf("\n");
            printIndent();
            printf(")");
            break;
        }
    }
}