# Saffron-Based Manifest: `pantry.sf`

## Concept

What if the project manifest was executable Saffron instead of static TOML? Like Elixir's `mix.exs` or Gradle's `build.gradle.kts`.

---

## Current: `pantry.toml` (static)

```toml
[package]
name = "turmeric-demo"
version = "0.1.0"
entry = "src/main.sf"

[dependencies]
turmeric = { path = "../../turmeric", version = "^0.1.0" }
http = "^2.0.0"

[dev-dependencies]
benchmark = "*"

[scripts]
build = "saffron build --target wasm32 src/main.sf -o build/app.wasm"
dev = "build && cd build && python3 -m http.server 8080"
test = "saffron run test/test_main.sf"
```

---

## Dead Simple: Most Projects

Most projects don't need dynamic config. `pantry.sf` can be just as minimal as TOML:

```saffron
import "@pantry" as Pantry

Pantry.project {
    name = "hello"
    version = "0.1.0"
    entry = "src/main.sf"
}
```

With one dependency:

```saffron
import "@pantry" as Pantry

Pantry.project {
    name = "my-cli"
    version = "1.0.0"
    entry = "src/main.sf"

    dep "http", "^2.0.0"
    dep "json", "^1.5.0"

    script "test", "saffron run test/test_main.sf"
}
```

Library:

```saffron
import "@pantry" as Pantry

Pantry.project {
    name = "my-utils"
    version = "0.3.0"
    entry = "src/lib.sf"
    type = "library"
}
```

These use **receiver closures** — the block after `Pantry.project` has implicit `this` bound to the `Project` instance. All bare names (`name`, `dep`, `script`) resolve as fields/methods on the project. See `docs/design/receiver-closures.md` for the language feature.

The function signature: `fun project(config: Project.() => Nil)` — the `.()` receiver syntax means `this` is the Project inside the block.

---

## With Dependencies and Scripts

```saffron
import "@pantry" as Pantry

Pantry.project {
    name = "turmeric-demo"
    version = "0.1.0"
    entry = "src/main.sf"

    dep("turmeric", path: "../../turmeric", version: "^0.1.0")
    dep("http", "^2.0.0")

    dev_dep("benchmark", "*")

    script("build", "saffron build --target wasm32 src/main.sf -o build/app.wasm")
    script("test", "saffron run test/test_main.sf")
}
```

---

## Dynamic Scripts (logic inside)

Scripts can be closures too — receiver closures all the way down:

```saffron
import "@pantry" as Pantry

Pantry.project {
    name = "fullstack"
    version = "1.0.0"
    entry = "src/main.sf"

    dep("http", "^2.0.0")

    script("dev") {
        run("build")
        OS.exec("cd build && python3 -m http.server 8080")
    }

    script("deploy") {
        var env = OS.env("DEPLOY_ENV")
        if (env == "") { env = "staging" }
        run("build")
        run("test")
        OS.exec("./scripts/deploy.sh " + env)
    }
}
```

---

## What Becomes Possible

### 1. Conditional dependencies

```saffron
Pantry.project {
    name = "my-server"
    version = "1.0.0"

    dep("http", "^2.0.0")

    if (OS.platform() == "linux") {
        dep("epoll-io", "^1.0.0")
    } else {
        dep("kqueue-io", "^1.0.0")
    }
}
```

### 2. Dynamic version computation

```saffron
Pantry.project {
    name = "my-app"
    // Read version from git tags
    version = OS.exec("git describe --tags 2>/dev/null").trim()
    if (version == "") { version = "0.0.0-dev" }

    entry = "src/main.sf"
}
```

### 3. Scripts as real functions with logic

```saffron
Pantry.project {
    name = "fullstack"

    script("build") {
        IO.println("Building frontend...")
        run_in("frontend", "pantry build")

        IO.println("Building backend...")
        run_in("backend", "pantry build")

        IO.println("Copying assets...")
        OS.exec("cp -r frontend/build/* backend/static/")
    }

    script("deploy") {
        var env: String = OS.env("DEPLOY_ENV")
        if (env == "") { env = "staging" }

        run("build")
        run("test")

        IO.println("Deploying to ${env}...")
        OS.exec("./scripts/deploy.sh ${env}")
    }
}
```

### 4. Workspace with computed members

```saffron
Pantry.workspace {
    // Auto-discover packages in packages/ directory
    var dirs: List<String> = OS.list_dir("packages")
    for (dir in dirs) {
        if (IO.file_exists("packages/${dir}/pantry.sf")) {
            member("packages/${dir}")
        }
    }

    shared_dep("http", "^2.0.0")
    shared_dep("json", "^1.5.0")
}
```

### 5. Environment-specific configuration

```saffron
Pantry.project {
    name = "web-api"
    version = "2.0.0"
    entry = "src/main.sf"

    env("dev") {
        opt_level = 0
        define("DEBUG", "true")
        define("API_URL", "http://localhost:3000")
    }

    env("prod") {
        opt_level = 2
        define("DEBUG", "false")
        define("API_URL", "https://api.myapp.com")
    }

    script("dev") {
        build(env: "dev")
        OS.exec("./build/web-api")
    }

    script("release") {
        build(env: "prod")
        OS.exec("tar czf release.tar.gz build/web-api")
    }
}
```

### 6. Custom transforms declared inline

```saffron
Pantry.project {
    name = "my-app"
    dep("turmeric", path: "../turmeric")

    // Register transforms
    transform("sfx", fun (source: String, path: String): String {
        // Inline transform — no separate package needed for simple ones
        return source.replace("<div>", "div {").replace("</div>", "}")
    }

    // Or from a package
    transform_from("turmeric", extensions: [".sfx"])
}
```

### 7. Hooks (lifecycle events)

```saffron
Pantry.project {
    name = "my-app"

    before_build(fun () {
        IO.println("Generating version file...")
        var version: String = OS.exec("git describe --tags").trim()
        IO.write_file("src/version.sf", "var VERSION = \"${version}\"\n")
    }

    after_build(fun () {
        IO.println("Build complete!")
        var size: String = OS.exec("wc -c < build/my-app").trim()
        IO.println("Binary size: ${size} bytes")
    }

    after_test(fun (results: Pantry.TestResults) {
        if (results.failed > 0) {
            OS.exec("notify-send 'Tests failed: ${results.failed}'")
        }
    }
}
```

### 8. Dynamic dependency features / feature flags

```saffron
Pantry.project {
    name = "http-client"
    version = "2.0.0"

    feature("tls") {
        dep("openssl", "^3.0.0")
        define("HAS_TLS", "true")
    }

    feature("http2") {
        dep("nghttp2", "^1.0.0")
        define("HAS_HTTP2", "true")
    }

    // Default features
    default_features = ["tls"]
}
```

Consumers:
```saffron
Pantry.project {
    dep("http-client", version: "^2.0.0", features: ["tls", "http2"])
}
```

### 9. Multi-target builds

```saffron
Pantry.project {
    name = "turmeric-demo"

    target("wasm32") {
        entry = "src/main.sf"
        output = "build/app.wasm"
        dep("turmeric", path: "../turmeric")
    }

    target("native") {
        entry = "src/server.sf"
        output = "build/server"
        dep("http", "^2.0.0")
    }

    script("dev") {
        build_target("wasm32")
        build_target("native")
        // Run both: wasm in browser, native as API server
        Process.spawn("./build/server")
        OS.exec("cd build && python3 -m http.server 8080")
    }
}
```

### 10. Plugin system

```saffron
Pantry.project {
    name = "my-app"

    // Plugins can add commands, transforms, hooks
    plugin("pantry-docker", "^1.0.0")  // adds: pantry docker build
    plugin("pantry-deploy", "^2.0.0")  // adds: pantry deploy staging

    // Plugin configuration
    configure("pantry-docker") { docker =>
        docker.set("base_image", "alpine:3.18")
        docker.set("expose", [8080])
    }
}
```

---

## Trade-offs vs TOML

| | `pantry.toml` | `pantry.sf` |
|--|---|---|
| **Readability** | Very clear, scannable | More complex, requires understanding Saffron |
| **Static analysis** | Tools can parse without executing | Must execute to resolve config |
| **Reproducibility** | Deterministic | Could depend on env, time, network |
| **Power** | Limited to key-value | Full language expressivity |
| **Tooling** | Easy to edit programmatically | Harder to modify via tools |
| **Security** | Safe (no execution) | Manifest can run arbitrary code |
| **Ecosystem** | Registries can inspect without running | Registries must sandbox or limit |

---

## Recommendation: Hybrid Approach

Keep `pantry.toml` as the standard manifest (most projects don't need dynamic config). Add `pantry.sf` as an **optional override** that Pantry checks first:

1. Pantry looks for `pantry.sf` first, then `pantry.toml`
2. `pantry.sf` is executed and produces a `Pantry.Project` struct
3. The result is equivalent to what `pantry.toml` declares — same data model underneath
4. For registries: `pantry.toml` is required for publishing (static, inspectable)
5. `pantry.sf` is for local/workspace use where dynamic config matters

This gives you the power when you need it without sacrificing the ecosystem simplicity for the common case.

---

## The `@pantry` stdlib module (sketch)

```saffron
//! Build configuration API for pantry.sf manifests.

class Project {
    var name: String
    var version: String
    var entry: String
    var target: String

    fun dep(name: String, version: String)
    fun dep(name: String, path: String, version: String)
    fun dep(name: String, git: String, tag: String)
    fun dev_dep(name: String, version: String)

    fun script(name: String, cmd: String)
    fun script(name: String, func: () => Nil)

    fun env(name: String, config: (Env) => Nil)
    fun feature(name: String, config: () => Nil)
    fun target(name: String, config: (Target) => Nil)
    fun transform(name: String, func: (String, String) => String)
    fun transform_from(package: String, extensions: List<String>)
    fun plugin(name: String, version: String)

    fun before_build(hook: () => Nil)
    fun after_build(hook: () => Nil)
    fun after_test(hook: (TestResults) => Nil)

    fun run(script_name: String)
    fun run_in(dir: String, cmd: String)
    fun build(env: String)
    fun build_target(target: String)

    fun define(key: String, value: String)
}

class Env {
    var opt_level: Int
    fun define(key: String, value: String)
}

class Target {
    var entry: String
    var output: String
    fun dep(name: String, version: String)
    fun dep(name: String, path: String)
}

class Workspace {
    fun member(path: String)
    fun shared_dep(name: String, version: String)
}

class TestResults {
    var passed: Int
    var failed: Int
    var skipped: Int
}

fun project(config: (Project) => Nil)
fun workspace(config: (Workspace) => Nil)
```
