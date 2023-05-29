from contextlib import redirect_stdout

exprs = [
    "Binary   : Expr *left, Token operator, Expr* right",
    "Grouping : Expr* expression",
    "Literal  : Value value",
    "Unary    : Token operator, Expr* right",
    "Variable : Token name",
    "Assign   : Token name, Expr* value",
    "Logical  : Expr* left, Token operator, Expr* right",
    "Call     : Expr* callee, Token paren, ExprArray arguments",
    "Get      : Expr* object, Token name",
    "Set      : Expr* object, Token name, Expr* value",
    "Super    : Token keyword, Token method",
    "This     : Token keyword",
    "Yield    : Expr* expression",
    "Lambda   : TokenArray params, StmtArray body",
    "List     : ExprArray items",
    # "Arguments: Expr* items",
    # "Parameters: "
]

stmts = [
    "Expression : Expr* expression",
    "Var        : Token name, Expr* initializer",
    "Block      : StmtArray statements",
    "Function   : Token name, TokenArray params," +
    " StmtArray body, FunctionType functionType",
    "Class      : Token name, struct Variable* superclass," +
    " StmtArray methods",
    "If         : Expr* condition, Stmt* thenBranch," +
    " Stmt* elseBranch",
    "While      : Expr* condition, Stmt* body",
    "For        : Stmt* initializer, Expr* condition, Expr* increment, Stmt* body",
    "Break      : Token keyword",
    "Return     : Token keyword, Expr* value",
    "Import     : Expr* expression",
]

# TODO: Argument list, etc

types = dict(expr=exprs, stmt=stmts)

file = open("src/ast/ast.c", "w")
with redirect_stdout(file):
    print('#include "ast.h"')
    print()

    for group, items in types.items():
        titleGroup = group.title()
        print(f"""
void init{titleGroup}Array({titleGroup}Array* {group}Array) {{
    {group}Array->count = 0;
    {group}Array->capacity = 0;
    {group}Array->{group}s = NULL;
}}      
        
void write{titleGroup}Array({titleGroup}Array * {group}Array, {titleGroup}* {group}) {{
    if ({group}Array->capacity < {group}Array->count + 1) {{
        int oldCapacity = {group}Array->capacity;
        {group}Array->capacity = GROW_CAPACITY(oldCapacity);
        {group}Array->{group}s = GROW_ARRAY({titleGroup}*, {group}Array->{group}s,
                                       oldCapacity, {group}Array->capacity);
    }}

    {group}Array->{group}s[{group}Array->count] = {group};
    {group}Array->count++;
}}

void free{titleGroup}Array({titleGroup}Array * {group}Array) {{
    FREE_ARRAY({titleGroup}*, {group}Array->{group}s, {group}Array->capacity);
    init{titleGroup}Array({group}Array);
}}
        """)
file = open("src/ast/ast.h", "w")
with redirect_stdout(file):
    all_types = []

    print("""#ifndef CRAFTING_INTERPRETERS_AST_H
#define CRAFTING_INTERPRETERS_AST_H""")
    print('#include "../scanner.h"')
    print('#include "../value.h"')
    print('#include "../memory.h"')
    print()

    print("""typedef enum {
    TYPE_FUNCTION,
    TYPE_SCRIPT,
    TYPE_METHOD,
    TYPE_INITIALIZER,
} FunctionType;
""")

    print('#define ALLOCATE_NODE(type, nodeType) (type*) allocateNode(sizeof(type), nodeType)')
    print()

    for group, items in types.items():
        for item in items:
            name, args = item.split(":")
            name = name.strip()
            all_types.append(name)

    print("typedef enum {")
    for node_type in all_types:
        print(f"    NODE_{node_type.upper()},")
    print("} NodeType;")
    print()

    print("typedef struct {")
    print(f"    NodeType type;")
    print(f"    int lineno;")
    print(f"    bool isMarked;")
    print(f"    struct Node *next;")
    print("} Node;")
    print()
    print("Node *allocateNode(size_t size, NodeType type);")
    print()

    for group, items in types.items():
        print("typedef struct {")
        print(f"    Node self;")
        print("}", f"{group.title()};")
        print()

        titleGroup = group.title()

        print("typedef struct {")
        print(f"    int count;")
        print(f"    int capacity;")
        print(f"    {group.title()}** {group.lower()}s;")
        print("}", f"{group.title()}Array;")
        print()

        print(f'void init{titleGroup}Array({titleGroup}Array* {group}Array);')
        print(f'void write{titleGroup}Array({titleGroup}Array * {group}Array, {titleGroup}* {group});')
        print(f'void free{titleGroup}Array({titleGroup}Array * {group}Array);')

    for group, items in types.items():
        for item in items:
            name, args = item.split(":")
            name = name.strip()
            args = args.split(",")

            print(f"struct {name.title()}", "{")
            print(f"    {group.title()} self;")
            for arg in args:
                print(f"    {arg.strip()};")

            print("};")
            print()

    print("#endif //CRAFTING_INTERPRETERS_AST_H")
