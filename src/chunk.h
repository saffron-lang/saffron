#ifndef saffron_chunk_h
#define saffron_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
    OP_LIST,
    OP_MAP,
    OP_CONSTANT,
    OP_CLOSURE,
    OP_NEGATE,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_ADD,
    OP_SUBTRACT,
    OP_MODULO,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_POP,
    OP_DUP,
    OP_CLOSE_UPVALUE,
    OP_IN_PLACE_ADD,
    OP_IN_PLACE_SUBTRACT,
    OP_DEFINE_GLOBAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_CALL,
    OP_GETITEM,
    OP_SETITEM,
    OP_PIPE,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_INVOKE,
    OP_GET_SUPER,
    OP_SUPER_INVOKE,
    OP_METHOD,
    OP_FIELD,
    OP_FIELD_META,
    OP_CLASS,
    OP_INHERIT,
    OP_YIELD,
    OP_RESUME,
    OP_RETURN,
    OP_IMPORT,
    OP_ENUM,
    OP_VARIANT,
    OP_CONSTRUCT_VARIANT,
    OP_GET_TAG,
    OP_MATCH_TAG,
    OP_IS,
    OP_SLICE,
    OP_THROW,
    OP_TRY_BEGIN,
    OP_TRY_END,
    OP_PACK_REST,
    OP_CALL_SPREAD,
    OP_RANGE,
} OpCode;

typedef struct {
    int offset;
    int line;
} LineStart;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    ValueArray constants;
    int lineCount;
    int lineCapacity;
    LineStart* lines;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);
int getLine(Chunk* chunk, int instruction);

#endif