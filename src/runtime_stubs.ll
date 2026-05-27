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

; AST.type_to_string stub — returns empty string for any type
define i64 @type_to_string(i64 %t) {
entry:
  %empty = call i8* @malloc(i64 4)
  store i8 65, i8* %empty
  %p1 = getelementptr i8, i8* %empty, i64 1
  store i8 110, i8* %p1
  %p2 = getelementptr i8, i8* %empty, i64 2
  store i8 121, i8* %p2
  %p3 = getelementptr i8, i8* %empty, i64 3
  store i8 0, i8* %p3
  %r = ptrtoint i8* %empty to i64
  ret i64 %r
}
