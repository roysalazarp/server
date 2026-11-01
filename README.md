# ARK

A small HTTPS web server and framework written from scratch in C, along with an example app that runs on it.

ARK runs on a single OS thread using cooperative coroutines (tasks) and an epoll-based event loop. It handles TLS through OpenSSL, parses HTTP with a zero-copy parser, renders pages with its own HTML component/template language, stores data in SQLite, and gives each request its own fixed-size arena allocator instead of using `malloc`.

## Features

- **Coroutine concurrency:** each connection and route handler runs as a lightweight task with its own small stack (10–45 KB), scheduled cooperatively on one thread. Built on a fork of [libtask](https://swtch.com/libtask/) with epoll support.
- **HTTPS only:** listens on port 443 and terminates TLS with OpenSSL. Non-blocking `SSL_accept`/`SSL_read`/`SSL_write` yield back to the scheduler while they wait.
- **Fast HTTP parsing:** request, URL and cookie parsing adapted from Justine Tunney's Cosmopolitan/redbean parser, with a `gperf` perfect hash for header lookup.
- **Arena memory:** each request gets an `mmap`'d arena that is released in one call when the request ends. Startup data (templates, public files) lives in a separate persistent arena.
- **HTML templates:** components are defined, composed and validated at startup and compiled into memory, so request handlers only look them up and fill in values.
- **Preloaded assets:** everything under `assets/` is read into memory at boot and served from there.
- **SQLite storage** in WAL mode, **Argon2** password hashing and **UUID** session IDs for authentication.
- **htmx** on the frontend for interactivity without a JS build step.

## Project layout

```
.
├── build.sh               # Builds the shared libraries and the server binary
├── include/ark/           # Public headers for the ARK libraries
├── lib/                   # ARK libraries, each built as its own .so
│   ├── http/              #   HTTP message, URL and cookie parsing
│   ├── json/              #   JSON (stub)
│   ├── memory/            #   Arena allocator
│   ├── profiler/          #   Block timing profiler
│   ├── server/            #   Socket setup and accept
│   ├── task/              #   Coroutine scheduler (libtask fork)
│   ├── template/          #   Template validation, compilation and rendering
│   └── utils/             #   File listing, masks, helpers
├── src/                   # Example application built on ARK
│   ├── main.c             #   Startup, TLS setup and request router
│   ├── routes/            #   Route handlers (home, login, signup, logout, 404)
│   ├── parsers/           #   Input validation (email, password)
│   └── assets/
│       ├── templates/     #   HTML components (*.html)
│       └── public/        #   Static files served under /public
├── migrations/            # SQLite schema
├── .devcontainer/         # Debian dev container with all dependencies
└── .vscode/               # Build task and gdb launch config
```

## How it works

### Request lifecycle

1. `main` loads every file in `assets/`, validates and compiles the HTML templates, opens SQLite, sets up the TLS context, and starts the scheduler with one `handler` task.
2. `handler` waits for the listening socket to become readable, accepts the connection, maps a 12 KB request arena, and performs the TLS handshake. If the handshake isn't ready yet, it yields instead of blocking.
3. The request is read into the arena and parsed in place. The parser stores offsets into the raw buffer instead of copying strings.
4. The router matches the method and path and spawns a task for the route:

   | Route | Handler |
   | --- | --- |
   | `/public/*` | Static file from memory (`.css`, `.js`, `.json`) |
   | `GET /` | `home` |
   | anything else | `not_found` |
   | `GET /login`, `POST /login/create-session`, `GET /signup`, `POST /signup/create-account`, `POST /logout` | Implemented, currently disabled |

5. The route writes the response, shuts down TLS, closes the socket and frees the arena.

### Templates

Templates are plain `.html` files in `src/assets/templates/`. Each file can define one or more named components:

```html
<x-def name:layout>
    <main>
        <x-slot />
    </main>
</x-def>

<x-def name:main>
    <x-use name:layout>
        <x-fill>
            <h1>Hello</h1>
        </x-fill>
    </x-use>
</x-def>
```

- `<x-def name:NAME>` defines a component.
- `<x-use name:NAME>` inserts another component. It can be self-closing: `<x-use name:NAME/>`.
- `<x-slot />` marks where content goes inside a component, and `<x-fill>` supplies that content. Fills are matched to slots in order.
- `ctx:NAME` on an element marks a block that can be repeated with runtime data, and `@@placeholder` marks a value inside it:

  ```html
  <div ctx:people>@@name @@lastname</div>
  ```

  Route handlers fill these blocks with `calc_rendered_length` and `render_ctx_values` (work in progress).

Components are composed at startup. A route looks up a compiled component by name with `find_element_index` and sends it.

## Acknowledgements

- [libtask](https://swtch.com/libtask/) by Russ Cox (MIT), via a fork that adds epoll support.
- HTTP, URL and cookie parsing adapted from [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) by Justine Tunney (ISC).
- [htmx](https://htmx.org/) on the frontend.

## License

<!-- TODO: LICENSE.txt is currently empty. Choose a license (e.g. MIT) and add it. -->
See [LICENSE.txt](LICENSE.txt). Vendored code keeps its original license (see `lib/task/COPYRIGHT` and the headers in `lib/http/`).
