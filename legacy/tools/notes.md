```ts
type Iterator<T> =
    (T) => Iterator<T>
    next() => T
    next?() => Bool
    iter() => Iterator<T>

type List extends Iterator<T>

let a: Iterator<Number> = [1, 2, 3]
let b: Number = a.next()
let c: Iterator<Number> = a.iter()

let filter: <K>(Iterator<K>, K => Bool) => Iterator<K> =
    (iter, f) => 
        let result: List = []
        var item: K = iter.next()
        result.push(item)
        return result

let func: f(Number) => Number =
    f(Number) => Int
// x f(Int) => Number
```

