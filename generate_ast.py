exprs = [
    "Binary   : Expr left, Token operator, Expr* right",
                "Grouping : Expr* expression",
                "Literal  : Value value",
                "Unary    : Token operator, Expr* right",
                "Variable : Token name",
                "Assign   : Token name, Expr* value",
                "Logical  : Expr* left, Token operator, Expr* right",
                "Call     : Expr* callee, Token paren, Expr* arguments, int argCount",
                "Get      : Expr* object, Token name",
                "Set      : Expr* object, Token name, Expr value",
                "Super    : Token keyword, Token method",
                "This     : Token keyword",
                "Yield    : Expr* expression",
                "Lambda   : Token* params, int paramCount," +
                        " struct Stmt* body, int statementCount",
                
]

stmts = [
    "Expression : Expr expression",
                "Print      : Expr* expression",
                "Var        : Token name, Expr* initializer",
                "Block      : Stmt* statements, int statementCount",
                "Function   : Token name, Token* params, int paramCount," +
                        " Stmt* body, int statementCount",
                "Class      : Token name, struct Variable* superclass," +
                        " struct Function* methods, int methodCount",
                "If         : Expr* condition, Stmt* thenBranch," +
                        " Stmt* elseBranch",
                "While      : Expr* condition, Stmt* body",
                "Break      : Token keyword",
                "Return     : Token keyword, Expr* value",
                "Import     : Expr* expression",
]

types = dict(Expr=exprs, Stmt=stmts)
all_types = []

print('#include "scanner.h"')
print('#include "value.h"')
print()

for group, items in types.items():
    for item in items:
        name, args = item.split(":")
        name = name.strip()
        all_types.append(name)

print("typedef enum {")
for node_type in all_types:
    print(f"    NODE_{node_type.upper()},")
    
print("}", f"NodeType;")


for group, items in types.items():
    print("typedef struct {")
    print(f"    NodeType type;")
    print("}", f"{group.title()};")
    print()

    for item in items:
        name, args = item.split(":")
        name = name.strip()
        args = args.split(",")

        print(f"struct {name.title()}", "{")
        for arg in args:
            print(f"    {arg.strip()};")

        print("};")

        print()
