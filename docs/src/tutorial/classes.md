# Classes

## Defining a class

Classes group data (fields) and behavior (methods):

```saffron
class Point {
    var x: Float
    var y: Float

    fun init(x: Float, y: Float) {
        this.x = x
        this.y = y
    }

    fun distance_to(other: Point): Float {
        var dx = this.x - other.x
        var dy = this.y - other.y
        return (dx * dx + dy * dy).sqrt()
    }
}

var p = Point(3.0, 4.0)
IO.println(p.distance_to(Point(0.0, 0.0)))  // 5.0
```

## The `init` method

`init` is the constructor. It's called automatically when you create an instance by calling the class name as a function. Use it to initialize fields via `this`:

```saffron
class Color {
    var r: Int
    var g: Int
    var b: Int

    fun init(r: Int, g: Int, b: Int) {
        this.r = r
        this.g = g
        this.b = b
    }
}

var red = Color(255, 0, 0)
```

## Inheritance

Use `extends` to inherit from a parent class:

```saffron
class Animal {
    var name: String

    fun init(name: String) {
        this.name = name
    }

    fun speak(): String {
        return "..."
    }
}

class Dog extends Animal {
    fun speak(): String {
        return "Woof!"
    }
}

class Cat extends Animal {
    fun speak(): String {
        return "Meow!"
    }
}

var dog = Dog("Rex")
IO.println("${dog.name} says ${dog.speak()}")  // Rex says Woof!
```

## Calling super

Use `super` to call the parent's method:

```saffron
class LoudDog extends Dog {
    fun speak(): String {
        return super.speak() + "!!"
    }
}
```

## Operator overloading

Classes can define special methods to overload operators:

```saffron
class Vec2 {
    var x: Float
    var y: Float

    fun init(x: Float, y: Float) {
        this.x = x
        this.y = y
    }

    fun add(other: Vec2): Vec2 {
        return Vec2(this.x + other.x, this.y + other.y)
    }

    fun eq(other: Vec2): Bool {
        return this.x == other.x and this.y == other.y
    }
}

var a = Vec2(1.0, 2.0)
var b = Vec2(3.0, 4.0)
var c = a + b  // Vec2(4.0, 6.0)
```

Available operator methods: `add`, `sub`, `mul`, `div`, `mod`, `lt`, `gt`, `eq`.

See also [Operator Overloading](../reference/operator-overloading.md) for the full reference.
