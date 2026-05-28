target triple = "arm64-apple-macosx14.0.0"
define i64 @check_with_imports(i64 %a, i64 %b) {
entry:
  %empty = call i8* @malloc(i64 1)
  store i8 0, i8* %empty
  %r = ptrtoint i8* %empty to i64
  ret i64 %r
}
define i64 @check(i64 %a) {
entry:
  %empty = call i8* @malloc(i64 1)
  store i8 0, i8* %empty
  %r = ptrtoint i8* %empty to i64
  ret i64 %r
}
; Token: multi-field variant (kind, line, col) → array encoding [tag=0, kind, line, col]
define i64 @Token(i64 %kind, i64 %line, i64 %col) {
entry:
  %raw = call i8* @malloc(i64 32)
  %arr = bitcast i8* %raw to [4 x i64]*
  %s0 = getelementptr [4 x i64], [4 x i64]* %arr, i64 0, i64 0
  store i64 0, i64* %s0
  %s1 = getelementptr [4 x i64], [4 x i64]* %arr, i64 0, i64 1
  store i64 %kind, i64* %s1
  %s2 = getelementptr [4 x i64], [4 x i64]* %arr, i64 0, i64 2
  store i64 %line, i64* %s2
  %s3 = getelementptr [4 x i64], [4 x i64]* %arr, i64 0, i64 3
  store i64 %col, i64* %s3
  %r = ptrtoint [4 x i64]* %arr to i64
  ret i64 %r
}
; TokenKind single-field constructors: (tag << 56) | value
define i64 @TkInt(i64 %v) { %t = shl i64 0, 56
  %r = or i64 %t, %v
  ret i64 %r }
define i64 @TkFloat(i64 %v) { %t = shl i64 1, 56
  %r = or i64 %t, %v
  ret i64 %r }
define i64 @TkString(i64 %v) { %t = shl i64 2, 56
  %r = or i64 %t, %v
  ret i64 %r }
define i64 @TkIdent(i64 %v) { %t = shl i64 6, 56
  %r = or i64 %t, %v
  ret i64 %r }
define i64 @TkDocComment(i64 %v) { %t = shl i64 59, 56
  %r = or i64 %t, %v
  ret i64 %r }
define i64 @TkModuleDoc(i64 %v) { %t = shl i64 60, 56
  %r = or i64 %t, %v
  ret i64 %r }
declare i8* @malloc(i64)


; Param enum constructor: [tag=0, name, type_ann]
define linkonce_odr i64 @Param(i64 %f0, i64 %f1) {
entry:
  %raw = call i8* @malloc(i64 24)
  %arr = bitcast i8* %raw to [3 x i64]*
  %s0 = getelementptr [3 x i64], [3 x i64]* %arr, i64 0, i64 0
  store i64 0, i64* %s0
  %s1 = getelementptr [3 x i64], [3 x i64]* %arr, i64 0, i64 1
  store i64 %f0, i64* %s1
  %s2 = getelementptr [3 x i64], [3 x i64]* %arr, i64 0, i64 2
  store i64 %f1, i64* %s2
  %r = ptrtoint [3 x i64]* %arr to i64
  ret i64 %r
}

; AST.Type enum constructors (max_fields=2, array encoding)
; Type variants: IntType=0, FloatType=1, BoolType=2, StringType=3, NilType=4,
; AnyType=5, ClassType=6(name), EnumType=7(name), GenericType=8(base,args),
; UnionType=9(types), FuncType=10(params,ret), NullableType=11(inner)

; No-field variants use simple encoding: (tag << 56)
; But max_fields for Type enum is 2 (GenericType, FuncType have 2 fields)
; So ALL variants use array encoding: [tag, f0, f1?]

define i64 @IntType() {
entry:
  %raw = call i8* @malloc(i64 8)
  %arr = bitcast i8* %raw to [1 x i64]*
  %s = getelementptr [1 x i64], [1 x i64]* %arr, i64 0, i64 0
  store i64 0, i64* %s
  %r = ptrtoint [1 x i64]* %arr to i64
  ret i64 %r
}

define i64 @FloatType() {
entry:
  %raw = call i8* @malloc(i64 8)
  %arr = bitcast i8* %raw to [1 x i64]*
  %s = getelementptr [1 x i64], [1 x i64]* %arr, i64 0, i64 0
  store i64 1, i64* %s
  %r = ptrtoint [1 x i64]* %arr to i64
  ret i64 %r
}

define i64 @BoolType() {
entry:
  %raw = call i8* @malloc(i64 8)
  %arr = bitcast i8* %raw to [1 x i64]*
  %s = getelementptr [1 x i64], [1 x i64]* %arr, i64 0, i64 0
  store i64 2, i64* %s
  %r = ptrtoint [1 x i64]* %arr to i64
  ret i64 %r
}

define i64 @StringType() {
entry:
  %raw = call i8* @malloc(i64 8)
  %arr = bitcast i8* %raw to [1 x i64]*
  %s = getelementptr [1 x i64], [1 x i64]* %arr, i64 0, i64 0
  store i64 3, i64* %s
  %r = ptrtoint [1 x i64]* %arr to i64
  ret i64 %r
}

define i64 @NilType() {
entry:
  %raw = call i8* @malloc(i64 8)
  %arr = bitcast i8* %raw to [1 x i64]*
  %s = getelementptr [1 x i64], [1 x i64]* %arr, i64 0, i64 0
  store i64 4, i64* %s
  %r = ptrtoint [1 x i64]* %arr to i64
  ret i64 %r
}

define i64 @AnyType() {
entry:
  %raw = call i8* @malloc(i64 8)
  %arr = bitcast i8* %raw to [1 x i64]*
  %s = getelementptr [1 x i64], [1 x i64]* %arr, i64 0, i64 0
  store i64 5, i64* %s
  %r = ptrtoint [1 x i64]* %arr to i64
  ret i64 %r
}

define i64 @ClassType(i64 %name) {
entry:
  %raw = call i8* @malloc(i64 16)
  %arr = bitcast i8* %raw to [2 x i64]*
  %s0 = getelementptr [2 x i64], [2 x i64]* %arr, i64 0, i64 0
  store i64 6, i64* %s0
  %s1 = getelementptr [2 x i64], [2 x i64]* %arr, i64 0, i64 1
  store i64 %name, i64* %s1
  %r = ptrtoint [2 x i64]* %arr to i64
  ret i64 %r
}

define i64 @EnumType(i64 %name) {
entry:
  %raw = call i8* @malloc(i64 16)
  %arr = bitcast i8* %raw to [2 x i64]*
  %s0 = getelementptr [2 x i64], [2 x i64]* %arr, i64 0, i64 0
  store i64 7, i64* %s0
  %s1 = getelementptr [2 x i64], [2 x i64]* %arr, i64 0, i64 1
  store i64 %name, i64* %s1
  %r = ptrtoint [2 x i64]* %arr to i64
  ret i64 %r
}

define i64 @GenericType(i64 %base, i64 %args) {
entry:
  %raw = call i8* @malloc(i64 24)
  %arr = bitcast i8* %raw to [3 x i64]*
  %s0 = getelementptr [3 x i64], [3 x i64]* %arr, i64 0, i64 0
  store i64 8, i64* %s0
  %s1 = getelementptr [3 x i64], [3 x i64]* %arr, i64 0, i64 1
  store i64 %base, i64* %s1
  %s2 = getelementptr [3 x i64], [3 x i64]* %arr, i64 0, i64 2
  store i64 %args, i64* %s2
  %r = ptrtoint [3 x i64]* %arr to i64
  ret i64 %r
}

define i64 @UnionType(i64 %types) {
entry:
  %raw = call i8* @malloc(i64 16)
  %arr = bitcast i8* %raw to [2 x i64]*
  %s0 = getelementptr [2 x i64], [2 x i64]* %arr, i64 0, i64 0
  store i64 9, i64* %s0
  %s1 = getelementptr [2 x i64], [2 x i64]* %arr, i64 0, i64 1
  store i64 %types, i64* %s1
  %r = ptrtoint [2 x i64]* %arr to i64
  ret i64 %r
}

define i64 @FuncType(i64 %params, i64 %ret) {
entry:
  %raw = call i8* @malloc(i64 24)
  %arr = bitcast i8* %raw to [3 x i64]*
  %s0 = getelementptr [3 x i64], [3 x i64]* %arr, i64 0, i64 0
  store i64 10, i64* %s0
  %s1 = getelementptr [3 x i64], [3 x i64]* %arr, i64 0, i64 1
  store i64 %params, i64* %s1
  %s2 = getelementptr [3 x i64], [3 x i64]* %arr, i64 0, i64 2
  store i64 %ret, i64* %s2
  %r = ptrtoint [3 x i64]* %arr to i64
  ret i64 %r
}

define i64 @NullableType(i64 %inner) {
entry:
  %raw = call i8* @malloc(i64 16)
  %arr = bitcast i8* %raw to [2 x i64]*
  %s0 = getelementptr [2 x i64], [2 x i64]* %arr, i64 0, i64 0
  store i64 11, i64* %s0
  %s1 = getelementptr [2 x i64], [2 x i64]* %arr, i64 0, i64 1
  store i64 %inner, i64* %s1
  %r = ptrtoint [2 x i64]* %arr to i64
  ret i64 %r
}
