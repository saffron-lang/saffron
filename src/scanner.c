#include <stdio.h>
#include <string.h>

#include "common.h"
#include "scanner.h"
#include "chunk.h"
#include "memory.h"

typedef struct {
    const char *start;
    const char *current;
    int line;
} Scanner;

Scanner scanner;

void initScanner(const char *source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
}

static bool isAtEnd() {
    return *scanner.current == '\0';
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int) (scanner.current - scanner.start);
    token.line = scanner.line;
    return token;
}

static Token errorToken(const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int) strlen(message);
    token.line = scanner.line;
    return token;
}

static char advance() {
    scanner.current++;
    return scanner.current[-1];
}

static bool match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;
    scanner.current++;
    return true;
}

static char peek() {
    return *scanner.current;
}

static char peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

static void skipWhitespace() {
    for (;;) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                scanner.line++;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    // A comment goes until the end of the line.
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static Token string() {
    while (peek() != '"' && !isAtEnd()) {
        if (peek() == '\n') scanner.line++;
        advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    // The closing quote.
    advance();
    return makeToken(TOKEN_STRING);
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isNameMod(char c) {
    return c == '?' || c == '!';
}

static Token number() {
    while (isDigit(peek())) advance();

    // Look for a fractional part.
    if (peek() == '.' && isDigit(peekNext())) {
        // Consume the ".".
        advance();

        while (isDigit(peek())) advance();
    }

    return makeToken(TOKEN_NUMBER);
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static TokenType checkKeyword(int start, int length,
                              const char *rest, TokenType type) {
    if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
    switch (scanner.start[0]) {
        case 'a':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'n':
                        return checkKeyword(2, 1, "d", TOKEN_AND);
                    case 's':
                        return TOKEN_AS;
                }
            }
            break;
        case 'c':
            return checkKeyword(1, 4, "lass", TOKEN_CLASS);
        case 'e':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'l':
                        return checkKeyword(2, 2, "se", TOKEN_ELSE);
                    case 'x':
                        return checkKeyword(2, 5, "tends", TOKEN_EXTENDS);
                    case 'n':
                        return checkKeyword(2, 2, "um", TOKEN_EXTENDS);
                }
            }
            break;
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a':
                        return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o':
                        return checkKeyword(2, 1, "r", TOKEN_FOR);
                    case 'u':
                        return checkKeyword(2, 1, "n", TOKEN_FUN);
                }
            }
            break;
        case 'i':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'f':
                        return TOKEN_IF;
                    case 'm':
                        return checkKeyword(2, 4, "port", TOKEN_IMPORT);
                    case 'n':
                        if (scanner.current - scanner.start > 2) {
                            return checkKeyword(3, 6, "erface", TOKEN_INTERFACE);
                        } else {
                            return TOKEN_IN;
                        }
                }
            }
            break;
        case 'n':
            return checkKeyword(1, 2, "il", TOKEN_NIL);
        case 'o':
            return checkKeyword(1, 1, "r", TOKEN_OR);
        case 'r':
            if (scanner.current - scanner.start > 1 && scanner.start[1] == 'e') {
                switch (scanner.start[2]) {
                    case 't':
                        return checkKeyword(3, 3, "urn", TOKEN_RETURN);
                    case 's':
                        return checkKeyword(3, 3, "ume", TOKEN_RESUME);
                }
            }
            break;
        case 's':
            return checkKeyword(1, 4, "uper", TOKEN_SUPER);
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h':
                        return checkKeyword(2, 2, "is", TOKEN_THIS);
                    case 'r':
                        return checkKeyword(2, 2, "ue", TOKEN_TRUE);
                    case 'y':
                        return checkKeyword(2, 2, "pe", TOKEN_TYPE);
                }
            }
            break;
        case 'v':
            return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w':
            return checkKeyword(1, 4, "hile", TOKEN_WHILE);
        case 'y':
            return checkKeyword(1, 4, "ield", TOKEN_YIELD);
    }
    return TOKEN_IDENTIFIER;
}

static Token atom() {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    if (isNameMod(peek())) advance();

    return makeToken(TOKEN_ATOM);
}

static Token identifier() {
    while (isAlpha(peek()) || isDigit(peek())) advance();
    if (isNameMod(peek())) advance();

    return makeToken(identifierType());
}

Token scanToken() {
    skipWhitespace();
    scanner.start = scanner.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = advance();
    if (c == ':' && isAlpha(peek())) return atom();
    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(':
            return makeToken(TOKEN_LEFT_PAREN);
        case ')':
            return makeToken(TOKEN_RIGHT_PAREN);
        case '{':
            return makeToken(TOKEN_LEFT_BRACE);
        case '}':
            return makeToken(TOKEN_RIGHT_BRACE);
        case '[':
            return makeToken(TOKEN_LEFT_BRACKET);
        case ']':
            return makeToken(TOKEN_RIGHT_BRACKET);
        case ';':
            return makeToken(TOKEN_SEMICOLON);
        case ',':
            return makeToken(TOKEN_COMMA);
        case '.':
            return makeToken(TOKEN_DOT);
        case '-':
            return makeToken(match('-') ? TOKEN_MINUS_MINUS : TOKEN_MINUS);
        case '+':
            return makeToken(match('+') ? TOKEN_PLUS_PLUS : TOKEN_PLUS);
        case '%':
            return makeToken(TOKEN_MODULO);
        case '/':
            return makeToken(TOKEN_SLASH);
        case '*':
            return makeToken(TOKEN_STAR);
        case '!':
            return makeToken(
                    match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            return makeToken(
                    match('=')
                    ? TOKEN_EQUAL_EQUAL
                    : (
                            match('>')
                            ? TOKEN_ARROW
                            : TOKEN_EQUAL
                    )
            );
        case '<':
            return makeToken(
                    match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(
                    match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '|':
            return makeToken(
                    match('>') ? TOKEN_PIPE : TOKEN_BITWISE_OR);
        case ':':
            return makeToken(TOKEN_COLON);
        case '"':
            return string();

    }

    return errorToken("Unexpected character.");
}

void initTokenArray(TokenArray *tokenArray) {
    tokenArray->count = 0;
    tokenArray->capacity = 0;
    tokenArray->tokens = NULL;
    tokenArray->lines = NULL;
}

void writeTokenArray(TokenArray *tokenArray, Token token, int line) {
    if (tokenArray->capacity < tokenArray->count + 1) {
        int oldCapacity = tokenArray->capacity;
        tokenArray->capacity = GROW_CAPACITY(oldCapacity);
        tokenArray->tokens = GROW_ARRAY(Token, tokenArray->tokens,
                                        oldCapacity, tokenArray->capacity);
        tokenArray->lines = GROW_ARRAY(int, tokenArray->lines,
                                       oldCapacity, tokenArray->capacity);
    }

    tokenArray->tokens[tokenArray->count] = token;
    tokenArray->lines[tokenArray->count] = line;
    tokenArray->count++;
}

// TODO: There's something nasty in here with token arrays getting misaligned
// or something, not sure yet exactly whats happening but it seems like memory
// boundaries are being violated.

void freeTokenArray(TokenArray *tokenArray) {
    FREE_ARRAY(Token, tokenArray->tokens, tokenArray->capacity);
    FREE_ARRAY(int, tokenArray->lines, tokenArray->capacity);
    initTokenArray(tokenArray);
}

TokenArray tokenize() {
    int i = 0;
    TokenArray tokenArray;
    initTokenArray(&tokenArray);

    while (true) {
        Token token = scanToken();
        writeTokenArray(&tokenArray, token, scanner.line);
        if (token.type == TOKEN_EOF) {
            break;
        }
    }

    return tokenArray;
}