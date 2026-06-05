# Parsley

Parsley is a typed API framework for Saffron backends. It wraps `@http/server` with structured endpoint registration, request/response helpers, and automatic spec generation for client codegen.

## Setup

```toml
# pantry.toml
[dependencies]
parsley = { path = "../parsley" }
```

```saffron
import "parsley/router" as P
import "parsley/response" as Res
import "parsley/request" as Req
import "@http/server" as Http
```

## Defining Routes

```saffron
var api = P.router("/api/v1")

api.get("/packages", fun (req: Http.Request): Http.Response => {
    var packages = Db.list_packages()
    Res.json(packages)
})

api.get("/packages/:name", fun (req: Http.Request): Http.Response => {
    var name = Req.param(req, "name")
    var pkg = Db.get_package(name)
    if (pkg == nil) { return Res.not_found("package not found") }
    Res.json(pkg)
})

api.post("/packages/publish", fun (req: Http.Request): Http.Response => {
    var body = Req.parse_body(req)
    if (body == nil) { return Res.bad_request("invalid JSON") }
    // ...
    Res.created(result)
})
```

## Mounting to Server

```saffron
var app = Http.server(3000)
api.mount(app)
app.serve()
```

`mount()` registers all endpoints with the HTTP server, matching methods and paths.

## Response Helpers

| Function | Status | Description |
|----------|--------|-------------|
| `Res.json(data)` | 200 | JSON response from any serializable value |
| `Res.created(data)` | 201 | Created with JSON body |
| `Res.not_found(msg)` | 404 | Error JSON: `{"error": msg}` |
| `Res.bad_request(msg)` | 400 | Validation error |
| `Res.unauthorized(msg)` | 401 | Auth required |

## Request Helpers

```saffron
// Path parameters
var name = Req.param(req, "name")       // from /packages/:name

// Query string
var params = Req.query_params(req)      // Map<String, String>
var q = params.get("q")

// JSON body
var body = Req.parse_body(req)          // Map<String, Any> or nil
```

## API Spec Generation

Parsley can generate a machine-readable spec from your routes — used by the client generator to produce typed Basil clients.

```saffron
import "parsley/spec" as Spec

// Get spec as JSON (for gen_client tool)
var spec_json: String = Spec.to_json(api.spec())
IO.write_file("api_spec.json", spec_json)

// Or as readable TOML-style manifest
var manifest: String = Spec.to_manifest(api.spec())
IO.write_file("api_spec.toml", manifest)
```

### Spec format

```json
[
  {"method": "GET", "path": "/api/v1/packages", "response_type": "", "body_type": "", "auth": "false"},
  {"method": "GET", "path": "/api/v1/packages/:name", "response_type": "", "body_type": "", "auth": "false"},
  {"method": "POST", "path": "/api/v1/packages/publish", "response_type": "", "body_type": "", "auth": "true"}
]
```

## Client Generation

Generate a typed Basil frontend client from the spec:

```bash
saffron run parsley/tools/gen_client.sf api_spec.json frontend/src/api.sf
```

See [Basil docs](basil.md) for how the generated client works.

## Endpoint Metadata

Endpoints returned from `api.get()`/`api.post()` are `Endpoint` objects you can annotate:

```saffron
class Endpoint {
    var method: String
    var path: String
    var handler: (Http.Request) => Http.Response
    var response_type: String    // type name for codegen
    var body_type: String        // request body type for codegen
    var auth_required: Bool
    var description: String
}
```

Set metadata for richer generated clients:

```saffron
var ep = api.get("/packages", list_handler)
ep.response_type = "ListPackagesResponse"
ep.description = "List all published packages with pagination"

var pub_ep = api.post("/packages/publish", publish_handler)
pub_ep.body_type = "PublishRequest"
pub_ep.response_type = "PublishResponse"
pub_ep.auth_required = true
pub_ep.description = "Publish a new package version"
```

## Full Example

```saffron
import "parsley/router" as P
import "parsley/response" as Res
import "parsley/request" as Req
import "parsley/spec" as Spec
import "@http/server" as Http

var api = P.router("/api/v1")

api.get("/users", fun (req: Http.Request): Http.Response => {
    Res.json(Db.list_users())
})

api.get("/users/:id", fun (req: Http.Request): Http.Response => {
    var id = Req.param(req, "id")
    var user = Db.get_user(id)
    if (user == nil) { return Res.not_found("user not found") }
    Res.json(user)
})

api.post("/users", fun (req: Http.Request): Http.Response => {
    var body = Req.parse_body(req)
    if (body == nil) { return Res.bad_request("invalid body") }
    var user = Db.create_user(body.get("name"), body.get("email"))
    Res.created(user)
})

// Serve
var app = Http.server(8080)
api.mount(app)

// Write spec for client generation
IO.write_file("api_spec.json", Spec.to_json(api.spec()))

app.serve()
```
