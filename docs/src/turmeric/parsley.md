# Parsley

Parsley is a typed API framework for Saffron backends. It wraps `@http/server` with structured endpoint registration, request/response helpers, and automatic spec generation for client codegen.

## Setup

```toml
# pantry.toml
[dependencies]
parsley = { path = "../parsley" }
```

```saffron
import "parsley/router" as Parsley
import "parsley/response" as Response
import "parsley/request" as Request
import "@http/server" as Http
```

## Defining Routes

```saffron
var api = Parsley.router("/api/v1")

api.get("/packages", fun (req: Http.Request): Http.Response => {
    var packages = Db.list_packages()
    Response.json(packages)
})

api.get("/packages/:name", fun (req: Http.Request): Http.Response => {
    var name = Request.param(req, "name")
    var pkg = Db.get_package(name)
    if (pkg == nil) { return Response.not_found("package not found") }
    Response.json(pkg)
})

api.post("/packages/publish", fun (req: Http.Request): Http.Response => {
    var body = Request.parse_body(req)
    if (body == nil) { return Response.bad_request("invalid JSON") }
    // ...
    Response.created(result)
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
| `Response.json(data)` | 200 | JSON response from any serializable value |
| `Response.created(data)` | 201 | Created with JSON body |
| `Response.not_found(msg)` | 404 | Error JSON: `{"error": msg}` |
| `Response.bad_request(msg)` | 400 | Validation error |
| `Res.unauthorized(msg)` | 401 | Auth required |

## Request Helpers

```saffron
// Path parameters
var name = Request.param(req, "name")       // from /packages/:name

// Query string
var params = Request.query_params(req)      // Map<String, String>
var q = params.get("q")

// JSON body
var body = Request.parse_body(req)          // Map<String, Any> or nil
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
import "parsley/router" as Parsley
import "parsley/response" as Response
import "parsley/request" as Request
import "parsley/spec" as Spec
import "@http/server" as Http

var api = Parsley.router("/api/v1")

api.get("/users", fun (req: Http.Request): Http.Response => {
    Response.json(Db.list_users())
})

api.get("/users/:id", fun (req: Http.Request): Http.Response => {
    var id = Request.param(req, "id")
    var user = Db.get_user(id)
    if (user == nil) { return Response.not_found("user not found") }
    Response.json(user)
})

api.post("/users", fun (req: Http.Request): Http.Response => {
    var body = Request.parse_body(req)
    if (body == nil) { return Response.bad_request("invalid body") }
    var user = Db.create_user(body.get("name"), body.get("email"))
    Response.created(user)
})

// Serve
var app = Http.server(8080)
api.mount(app)

// Write spec for client generation
IO.write_file("api_spec.json", Spec.to_json(api.spec()))

app.serve()
```
