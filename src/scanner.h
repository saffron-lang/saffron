#ifndef saffron_scanner_h
#define saffron_scanner_h

typedef enum {
    // Single-character tokens.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_MODULO,
    TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,

    // One or two character tokens.
    TOKEN_BANG, TOKEN_BANG_EQUAL, TOKEN_IN,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_PIPE,
    TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS,
    TOKEN_BITWISE_OR, TOKEN_BITWISE_AND, TOKEN_BITWISE_XOR, TOKEN_BITWISE_NOT,
    TOKEN_SHIFT_LEFT, TOKEN_SHIFT_RIGHT,
    TOKEN_COLON, TOKEN_AT,
    TOKEN_DOT_DOT,

    // Literals.
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER, TOKEN_ATOM,

    // Keywords.
    TOKEN_AND, TOKEN_ELSE, TOKEN_FALSE,
    TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_LET, TOKEN_MATCH, TOKEN_NIL, TOKEN_OR,
    TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
    TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE,
    TOKEN_YIELD, TOKEN_AWAIT, TOKEN_RESUME,
    TOKEN_BREAK, TOKEN_CONTINUE,
    TOKEN_THROW, TOKEN_TRY, TOKEN_CATCH, TOKEN_FINALLY,
    TOKEN_STRING_INTERP,
    TOKEN_ARROW, TOKEN_AS,

    // Types
    TOKEN_ENUM, TOKEN_EXTENDS, TOKEN_TYPE, TOKEN_CLASS, TOKEN_INTERFACE,
    TOKEN_DATACLASS,

    TOKEN_IMPORT, TOKEN_IS,

    TOKEN_DOC_COMMENT,

    TOKEN_ERROR, TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    const char *start;
    int length;
    int line;
    int column;
} Token;

typedef struct {
    int count;
    int capacity;
    Token *tokens;
    int *lines;
} TokenArray;

void initScanner(const char *source);

void writeTokenArray(TokenArray *tokenArray, Token token, int line);

void initTokenArray(TokenArray *tokenArray);

void freeTokenArray(TokenArray * tokenArray);

Token scanToken();

TokenArray tokenize();

#endif