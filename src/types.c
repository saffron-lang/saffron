#include <printf.h>
#include "types.h"


void populateNode(Node *node);

struct Functor* initFunctor(TypeArray types, Type* returnType) {
    struct Functor *type = ALLOCATE_NODE(struct Functor, NODE_FUNCTOR);
    type->arguments = types;
    type->returnType = NULL;
    return type;
}

struct Simple* initSimple(Token name) {
    struct Simple *type = ALLOCATE_NODE(struct Simple, NODE_FUNCTOR);
    type->name = name;
    return type;
}

Type* getTypeOf(Value value) {
#ifdef NAN_BOXING
    if (IS_BOOL(value)) {
        printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {
        printf("nil");
    } else if (IS_NUMBER(value)) {
        printf("%g", AS_NUMBER(value));
    } else if (IS_OBJ(value)) {
        printObject(value);
    }
#else
    switch (value.type) {
        case VAL_BOOL:

            break;
        case VAL_NIL:

            break;
        case VAL_NUMBER:

            break;
        case VAL_OBJ:
            break;
    }

#endif

    Token token;
    return initSimple(token);
}

void populateTypes(StmtArray *statements) {
    for (int i = 0; i < statements->count; i++) {
        populateNode((Node *) statements->stmts[i]);
    }
}

void populateNode(Node *node) {
    if (node == NULL) {
        printf("NULL");
        return;
    }
    switch (node->type) {
        case NODE_BINARY: {
            struct Binary *casted = (struct Binary *) node;
            populateNode((Node *) casted->left);
            casted->self.type = casted->left->type;
            populateNode((Node *) casted->right);
        }
        case NODE_GROUPING: {
            struct Grouping *casted = (struct Grouping *) node;
            populateNode((Node *) casted->expression);
            casted->self.type = casted->expression->type;
            break;
        }
        case NODE_LITERAL: {
            struct Literal *casted = (struct Literal *) node;
            casted->self.type = getTypeOf(casted->value);
            break;
        }
        case NODE_UNARY: {
            struct Unary *casted = (struct Unary *) node;
            populateNode((Node *) casted->right);

            switch (casted->operator.type) {
                case TOKEN_BANG:
                    casted->self.type; // TODO: Boolean
                    break;
                case TOKEN_MINUS:
                    casted->self.type = casted->right->type;
                    break;
                default:
                    return; // Unreachable.
            }

            populateNode((Node *) casted->right);
            break;
        }
        case NODE_VARIABLE: {
            struct Variable *casted = (struct Variable *) node;
            break;
        }
        case NODE_ASSIGN: {
            struct Assign *casted = (struct Assign *) node;
            break;
        }
        case NODE_LOGICAL: {
            struct Logical *casted = (struct Logical *) node;
            populateNode((Node *) casted->left);
            casted->self.type; // TODO: Boolean
            populateNode((Node *) casted->right);
            break;
        }
        case NODE_CALL: {
            struct Call *casted = (struct Call *) node;
            break;
        }
        case NODE_GET: {
            struct Get *casted = (struct Get *) node;
            break;
        }
        case NODE_SET: {
            struct Set *casted = (struct Set *) node;
            break;
        }
        case NODE_SUPER: {
            break;
        }
        case NODE_THIS:
            break;
        case NODE_YIELD: {
            struct Yield *casted = (struct Yield *) node;
            break;
        }
        case NODE_LAMBDA: {
            struct Lambda *casted = (struct Lambda *) node;
            break;
        }
        case NODE_LIST: {
            struct List *casted = (struct List *) node;
            break;
        }
        case NODE_EXPRESSION: {
            struct Expression *casted = (struct Expression *) node;
            break;
        }
        case NODE_VAR: {
            struct Var *casted = (struct Var *) node;
            break;
        }
        case NODE_BLOCK: {
            struct Block *casted = (struct Block *) node;
            break;
        }
        case NODE_FUNCTION: {
            struct Function *casted = (struct Function *) node;
            break;
        }
        case NODE_CLASS: {
            struct Class *casted = (struct Class *) node;
            break;
        }
        case NODE_IF: {
            struct If *casted = (struct If *) node;
            break;
        }
        case NODE_WHILE: {
            struct While *casted = (struct While *) node;
            break;
        }
        case NODE_FOR: {
            struct For *casted = (struct For *) node;
            break;
        }
        case NODE_BREAK:
            break;
        case NODE_RETURN: {
            struct Return *casted = (struct Return *) node;
            break;
        }
        case NODE_IMPORT: {
            struct Import *casted = (struct Import *) node;
            break;
        }
        case NODE_FUNCTOR: {
            struct Functor *casted = (struct Functor *) node;
            break;
        }
        case NODE_SIMPLE: {
            struct Simple *casted = (struct Simple *) node;
            break;
        }
    }
}