#include <printf.h>
#include "astprint.h"
#include "../object.h"

void printTree(StmtArray *statements) {
    if (statements == NULL) {
        return;
    }
    for (int i = 0; i < statements->count; i++) {
        printNode((Node *) statements->stmts[i]);
        printf("\n");
    }
}

static void printToken(Token token) {
    for (int i = 0; i < token.length; i++) {
        printf("%c", token.start[i]);
    }
}

static void printExprArray(ExprArray exprArray) {
    for (int i = 0; i < exprArray.count; i++) {
        printNode((Node *) exprArray.exprs[i]);
        if (i != exprArray.count - 1) {
            printf(", ");
        }
    }
}

static void printTokenArray(TokenArray tokenArray) {
    for (int i = 0; i < tokenArray.count; i++) {
        printToken(tokenArray.tokens[i]);
        if (i != tokenArray.count - 1) {
            printf(", ");
        }
    }
}

int indent = 0;

void printIndent() {
    for (int i = 0; i < indent; i++) {
        printf("    ");
    }
}

void printNode(Node *node) {
    switch (node->type) {
        case NODE_BINARY: {
            struct Logical *casted = (struct Logical *) node;
            printNode((Node *) casted->left);
            printf(" ");
            printToken(casted->operator);
            printf(" ");
            printNode((Node *) casted->right);
            break;
        }
        case NODE_GROUPING: {
            struct Grouping *casted = (struct Grouping *) node;
            printf("(");
            printNode((Node *) casted->expression);
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
            printToken(casted->operator);
            printNode((Node *) casted->right);
            break;
        }
        case NODE_VARIABLE: {
            struct Variable *casted = (struct Variable *) node;
            printToken(casted->name);
            break;
        }
        case NODE_ASSIGN: {
            struct Assign *casted = (struct Assign *) node;
            printIndent();
            printToken(casted->name);
            printf(" = ");
            printNode((Node *) casted->value);
            break;
        }
        case NODE_LOGICAL: {
            struct Logical *casted = (struct Logical *) node;
            printNode((Node *) casted->left);
            printf(" ");
            printToken(casted->operator);
            printf(" ");
            printNode((Node *) casted->right);
            break;
        }
        case NODE_CALL: {
            struct Call *casted = (struct Call *) node;
            printNode((Node *) casted->callee);
            printf("(");
            printExprArray(casted->arguments);
            printf(")");
            break;
        }
        case NODE_GET: {
            struct Get *casted = (struct Get *) node;
            printNode((Node *) casted->object);
            printf(".");
            printToken(casted->name);
            break;
        }
        case NODE_SET: {
            struct Set *casted = (struct Set *) node;
            printNode((Node *) casted->object);
            printf(".");
            printToken(casted->name);
            printf(" = ");
            printNode((Node *) casted->value);
            printf(";");
            break;
        }
        case NODE_SUPER: {
            struct Super *casted = (struct Super *) node;
            printf("super.");
            printToken(casted->method);
            break;
        }
        case NODE_THIS:
            printf("this");
            break;
        case NODE_YIELD: {
            struct Yield *casted = (struct Yield *) node;
            printf("yield ");
            printNode((Node *) casted->expression);
            break;
        }
        case NODE_LAMBDA: {
            struct Lambda *casted = (struct Lambda *) node;
            printf("fun (");
            printTokenArray(casted->params);
            printf(") => {\n");
            indent++;
            printTree(&casted->body);
            indent--;
            printf("}");
            break;
        }
        case NODE_LIST: {
            struct List *casted = (struct List *) node;
            printf("[");
            printExprArray(casted->items);
            printf("]");
            break;
        }
        case NODE_EXPRESSION: {
            struct Expression *casted = (struct Expression *) node;
            printIndent();
            printNode((Node *) casted->expression);
            printf(";");
            break;
        }
        case NODE_VAR: {
            struct Var *casted = (struct Var *) node;
            printIndent();
            printf("var ");
            printToken(casted->name);
            if (casted->initializer) {
                printf(" = ");
                printNode((Node *) casted->initializer);
            }
            printf(";");
            break;
        }
        case NODE_BLOCK: {
            struct Block *casted = (struct Block *) node;
            printIndent();
            printf("{\n");
            indent++;
            printTree(&casted->statements);
            indent--;
            printf("}");
            break;
        }
        case NODE_FUNCTION: {
            struct Function *casted = (struct Function *) node;
            printIndent();
            if (casted->functionType == TYPE_FUNCTION) {
                printf("fun ");
            }

            printToken(casted->name);
            printf("(");
            printTokenArray(casted->params);
            printf(") {\n");
            indent++;
            printTree(&casted->body);
            indent--;
            printIndent();
            printf("}");
            break;
        }
        case NODE_CLASS: {
            struct Class *casted = (struct Class *) node;
            printIndent();
            printf("class ");
            printToken(casted->name);
            if (casted->superclass) {
                printf("< ");
                printNode((Node *) casted->superclass);
            }

            printf(" {\n");
            indent++;
            for (int i = 0; i < casted->methods.count; i++) {
                printNode((Node *) casted->methods.stmts[i]);
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
            printNode((Node *) casted->condition);
            printf(")\n");
            indent++;
            printNode((Node *) casted->thenBranch);
            indent--;

            if (casted->elseBranch) {
                printIndent();
                printf("else ");
                indent++;
                printNode((Node *) casted->elseBranch);
                indent--;
            }
            break;
        }
        case NODE_WHILE: {
            struct While *casted = (struct While *) node;
            printIndent();
            printf("while (");
            printNode((Node *) casted->condition);
            printf(")\n");
            indent++;
            printNode((Node *) casted->body);
            indent--;
            break;
        }
        case NODE_FOR: {
            struct For *casted = (struct For *) node;
            printIndent();
            printf("for (");
            printNode((Node *) casted->initializer);
            printNode((Node *) casted->condition);
            printf(";");
            printNode((Node *) casted->increment);
            printf(")\n");
            indent++;
            printNode((Node *) casted->body);
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
            printNode((Node *) casted->value);
            printf(";");
            break;
        }
        case NODE_IMPORT: {
            struct Import *casted = (struct Import *) node;
            printIndent();
            printf("import ");
            printNode((Node *) casted->expression);
            printf(";");
            break;
        }
    }
}