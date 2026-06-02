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

### `CSV.to_string(rows: List<List<String>>): String`

Convert rows back to CSV text:

```saffron
var text = CSV.to_string([["a", "b"], ["1", "2"]])
// "a,b\n1,2"
```

### `CSV.headers(rows: List<List<String>>): List<String>`

Extract the header row (first row):

```saffron
var rows = CSV.parse("name,age\nalice,30")
var cols = CSV.headers(rows)  // ["name", "age"]
```

### `CSV.as_maps(rows: List<List<String>>): List<Map<String, String>>`

Convert rows to a list of maps using the first row as headers:

```saffron
var rows = CSV.parse("name,age\nalice,30\nbob,25")
var records = CSV.as_maps(rows)
// [{"name": "alice", "age": "30"}, {"name": "bob", "age": "25"}]

IO.println(records[0].get("name"))  // "alice"
```

## Example

```saffron
import "@csv" as CSV

var contents = IO.read_file("data.csv")
var rows = CSV.parse(contents)

// Use as_maps for easy field access
var records = CSV.as_maps(rows)
for (record in records) {
    IO.println("${record.get("name")}: ${record.get("age")}")
}
```
