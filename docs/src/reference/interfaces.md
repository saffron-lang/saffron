# Interfaces

Interfaces define a contract that classes must fulfill. They can contain abstract methods (which must be implemented) and default methods (which are inherited).

## Defining an interface

```saffron
interface Printable {
    fun to_string(): String
}
```

## Implementing an interface

Use `extends` — the same keyword as class inheritance:

```saffron
class Point extends Printable {
    var x: Number
    var y: Number

    fun init(x: Number, y: Number) {
        this.x = x
        this.y = y
    }

    fun to_string(): String {
        return "(${this.x}, ${this.y})"
    }
}
```

The compiler will error if you don't implement all abstract methods.

## Default methods

Interfaces can provide default implementations:

```saffron
interface Drawable {
    fun draw(): String

    fun description(): String {
        return "a drawable object"
    }
}

class Circle extends Drawable {
    var radius: Number
    fun init(radius: Number) { this.radius = radius }

    fun draw(): String {
        return "O"
    }
    // description() is inherited from Drawable
}

var c = Circle(5.0)
IO.println(c.description())  // "a drawable object"
```

## Multiple interfaces

A class can extend multiple interfaces:

```saffron
interface Flyable {
    fun fly(): String
}

interface Swimmable {
    fun swim(): String
}

interface Walkable {
    fun walk(): String
}

class Duck extends Flyable, Swimmable, Walkable {
    fun init() {}
    fun fly(): String { return "flap flap" }
    fun swim(): String { return "paddle paddle" }
    fun walk(): String { return "waddle waddle" }
}
```

## Mixing classes and interfaces

A class can extend one parent class and multiple interfaces:

```saffron
class Animal {
    var name: String
    fun init(name: String) { this.name = name }
}

interface Trainable {
    fun learn(trick: String): String
}

class Dog extends Animal, Trainable {
    fun init(name: String) { super.init(name) }
    fun learn(trick: String): String {
        return "${this.name} learned ${trick}!"
    }
}
```
