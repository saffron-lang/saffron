# Random

```saffron
import "@random" as Random
```

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `Random.float()` | `Number` | Random float in [0, 1) |
| `Random.int(min, max)` | `Number` | Random integer in [min, max] |
| `Random.choice(list)` | element | Random element from list |
| `Random.shuffle(list)` | `List` | Shuffled copy |
| `Random.sample(list, count)` | `List` | Random sample of `count` elements |
| `Random.seed(n)` | -- | Seed the random number generator |

## Example

```saffron
import "@random" as Random

// Roll a die
var roll = Random.int(1, 6)
IO.println("You rolled: ${roll}")

// Pick a card
var suits = ["Hearts", "Diamonds", "Clubs", "Spades"]
IO.println(Random.choice(suits))

// Shuffle a deck
var deck = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
IO.println(Random.shuffle(deck))

// Random float between 0 and 1
IO.println(Random.float())

// Get 3 random elements from a list
var sampled = Random.sample([1, 2, 3, 4, 5, 6, 7, 8, 9, 10], 3)
IO.println(sampled)
```
