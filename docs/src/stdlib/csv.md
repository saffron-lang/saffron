# CSV

```saffron
import "@csv" as CSV
```

Parse and generate CSV data.

## Functions

### `CSV.parse(text: String): List<List<String>>`

Parse CSV text into a list of rows:

```saffron
var data = CSV.parse("name,age\nalice,30\nbob,25")
// [["name", "age"], ["alice", "30"], ["bob", "25"]]
```

### `CSV.stringify(rows: List<List<String>>): String`

Convert rows back to CSV text:

```saffron
var text = CSV.stringify([["a", "b"], ["1", "2"]])
// "a,b\n1,2"
```

## Example

```saffron
import "@csv" as CSV

var contents = IO.read_file("data.csv")
var rows = CSV.parse(contents)

// Skip header, process data
import { skip, each } from "@iter"
rows |> skip(1) |> each(fun (row: List<String>) {
    IO.println("${row[0]}: ${row[1]}")
})
```
