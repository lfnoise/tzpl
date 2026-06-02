# Existential Types: Tzopilotl vs. Haskell, Rust, Swift, and Go

This document compares Tzopilotl's `some C` existential types with the
analogous features in four other statically typed languages. The goal is to
locate Tzopilotl's design in the broader landscape and to explain *why* it
made the choices it did. Real-time safety is part of that story — but a
narrower part than it first appears: Tzopilotl's substrate (a TLSF O(1)
allocator and a bounded-pause incremental GC) *permits* runtime allocation, so
several design choices are driven by static typing and simplicity rather than
by RT-safety, and the document is careful to keep the two apart.

All five languages solve the same core problem: *erase a concrete type behind
an interface so that values of different underlying types can be stored
together and used uniformly, while keeping a record of how to call the
interface's operations on each one.* They differ in the surface syntax, the
runtime representation, when and how the "how to call it" record (the
**witness dictionary** / **vtable** / **itab**) is built, and which
operations are even *allowed* to appear in such an interface.

---

## 1. The common machinery

Every implementation here is a variation on the same idea, the
**dictionary-passing** (or **vtable**) translation of bounded polymorphism:

> A value behind an interface is represented as a pair *(payload, dictionary)*,
> where the dictionary holds a function pointer (or an index resolving to one)
> for each operation the interface requires, specialized to the payload's
> hidden concrete type.

The interesting differences are:

| Axis | What varies |
|---|---|
| **Naming discipline** | Is the interface *nominal* (you declare conformance) or *structural* (any type with the right shape qualifies)? |
| **When the dictionary is built** | At compile time, at the boxing/pack site, or lazily at runtime? |
| **Object safety** | Which operation signatures are allowed? In particular, can the hidden type appear in more than one position (the *binary-method problem*)? |
| **Payload storage** | Always heap-boxed, or inline for small values? |
| **World** | Open (conformances added anywhere, anytime) or closed (known at definition)? |

Tzopilotl is real-time safe, but it is worth being precise about *why* —
because it is easy to over-attribute its design to real-time concerns.
**Allocation is permitted.** The VM uses a TLSF allocator (O(1)
`malloc`/`free`) and an incremental GC with bounded pause times, so creating
heap objects during execution — including on the audio thread — is fine. What
real-time safety actually forbids is the **system allocator**, other
**potentially blocking syscalls**, and any GC pause without an upper bound.

That distinction matters for the comparisons below, because the naive
inference "building a dictionary at runtime allocates, therefore it is unsafe"
is *false* for Tzopilotl: it could build witness dictionaries lazily through
TLSF and remain real-time safe. It resolves witnesses at the **pack site** for
a different reason — the concrete type is statically known there and the
overload resolver is a compile-time component, so a runtime resolution
mechanism would be redundant machinery, not a safety violation. Where
real-time safety *does* decide the comparison is in which other languages'
runtimes qualify at all: Go and Haskell fall out not because they "allocate"
but because they lean on the *system* allocator and on GCs not designed around
a bounded pause.

---

## 2. At a glance

| | **Tzopilotl** | **Haskell** | **Rust** | **Swift** | **Go** |
|---|---|---|---|---|---|
| Surface syntax | `some C` | `forall a. C a => …` (boxed in a constructor) | `dyn Trait` | `any P` | `interface{ … }` |
| Interface declaration | `constraint C<T> = requires { … }` | `class C a where …` | `trait T { … }` | `protocol P { … }` | `type I interface { … }` |
| Naming | **Structural** | Nominal | Nominal | Nominal | **Structural** |
| Dictionary built | **At pack site** (compile-time-resolved indices) | At box construction (captured class dict) | At coercion (static vtable) | At coercion (static PWT) | **Lazily at runtime** (cached itab) |
| Representation | `{concreteType, payloadWords, methodIndices…}` heap obj | boxed constructor capturing the dict | fat ptr `(data*, vtable*)` | existential container (inline buf ≤3 words, else box) + PWT | `iface{itab*, data*}` |
| Inline small payloads | **Yes** (inline value words) | No | No | Yes | No |
| Object safety | **Explicit analysis, rejects unsafe** | Not enforced (unusable but legal) | **Explicit, strict** | "Self/assoc-type requirement" restriction | Sidestepped by structure |
| Binary methods (`+(T,T)`) | Rejected at constraint use | Compiles, can't be used across boxes | Not object-safe | Not usable as existential | Expressed via interface-typed args + runtime assert |
| World | Closed (per module) | Open (orphan instances) | Open (coherence-checked) | Open (retroactive conformance) | Open (structural) |
| Real-time safe | **Yes (by design)** | No (lazy GC, thunks) | Yes | Mostly (box alloc for large payloads) | No (lazy itab + GC) |

---

## 3. Tzopilotl: `some C`

```
constraint Drawable<T> = requires { draw(T) String; };

struct Circle { r Float; }
struct Square { side Int; }
fn draw(c Circle) String = "circle";
fn draw(s Square) String = "square";

let shapes [some Drawable] = [Circle { r: 1.0 }, Square { side: 3 }];
shapes draw println;          -- [circle, square]  (auto-mapped, per-element dispatch)
```

**Mechanism.** A `some C` value is a heap object
`{ Type* concreteType; u8 payloadWords; u16 numMethods; Word slots[] }`. The
`slots` array holds first the value's own words (the payload, copied inline,
`concreteType->sizeWords` of them), then one global code index per required
method. The dictionary is the trailing run of method indices; dispatch
(`op_call_witness`) reads `slot[payloadWords + methodSlot]`, looks up the
global `CodeBlock`, pushes a frame, copies the inline payload words into the
callee's parameter slots, and runs.

**When the dictionary is built.** At the **pack site** — the point where a
concrete value is coerced to `some C` (a `let`, an argument, a collection
literal element). The concrete type is statically known there, so the type
checker's `materializeWitness` resolves each required function to a fixed
global index via ordinary overload resolution. The pack itself allocates one
`Existential` object (through TLSF — fine for real-time use); what it *avoids*
is any **resolution** work at runtime — no runtime monomorphization, no lazy
dictionary build, no resolver reified into the VM. This keeps dispatch
deterministic and the runtime small, and it falls out of static typing rather
than being forced by real-time safety (lazy, TLSF-backed resolution would also
be RT-safe; it is simply unnecessary when the type is already known).

**Structural, not nominal.** A type satisfies `Drawable` simply by having a
`draw(T) String` in scope — there is no `impl … for …` / `instance` ceremony.
This is like Go's interfaces, not Haskell/Rust/Swift.

**Object safety is enforced and explicit.** `isExistentialSafe` analyzes the
constraint: each required function must have **exactly one** parameter that is
*exactly* the type variable `T` (the dispatching argument); no other parameter
may mention `T`; the return must be `T`-free (return-`T` re-packing is
deferred). A constraint like `Addable = requires { +(T,T) T }` is the
**binary-method problem** — `T` in two argument positions — and Tzopilotl
**rejects it at the point you try to use it existentially**, with a diagnostic
naming the offending requirement. The numeric constraints that pervade the
codebase therefore cannot be made existential, which is the correct outcome.

**Composition and modules.** Composed constraints concatenate their component
method lists (object-safety rejection propagates with a message naming the bad
component); constraints export/import across modules, so `some C` works for an
imported `C`, including an imported composition. Both fell out of the
recursive machinery with no new code.

**Heterogeneous collections + auto-map.** `[some C]`, `List<some C>`, and
`#[some C]` pack each element on construction; a constraint method auto-maps
over such a collection, dispatching per element through that element's own
dictionary.

**Deferred (not yet implemented):** multi-argument witness dispatch (only the
receiver argument today), and methods returning the hidden type `T`
(re-packing the result).

---

## 4. Haskell: existential quantification

```haskell
{-# LANGUAGE ExistentialQuantification #-}
data Drawable = forall a. Draw a => MkDrawable a

draws :: [Drawable]
draws = [MkDrawable Circle, MkDrawable Square]
```

**Mechanism.** Haskell already compiles type classes by **dictionary
passing**: a `Draw a =>` constraint is an extra, invisible argument carrying
the method table. An existential `data` type with a class context *captures
that dictionary inside the box* at construction. `MkDrawable x` stores both `x`
and the `Draw` dictionary for `x`'s type; unpacking gives you back a value plus
its dictionary, with the concrete type existentially hidden (you may only use
it through the captured class methods).

**Closest kinship to Tzopilotl.** Both are *dictionary-capturing at the box
site*. The difference is what's in the box: Haskell stores a pointer to a
heap-allocated, lazily-evaluated class dictionary (itself possibly built from
superclass dictionaries via thunks); Tzopilotl stores the payload *inline*
plus a flat array of *integer global indices*, fully resolved, no thunks.

**Naming.** Nominal: a type participates only via an `instance Draw T where …`
declaration. Orphan instances make the world genuinely open — an instance can
live in a third module — which is exactly the kind of late, non-local
resolution a real-time system cannot tolerate.

**Object safety.** Haskell does *not* reject "object-unsafe" classes. You can
existentially quantify over `Eq` (`(==) :: a -> a -> Bool`, a binary method),
but you simply *cannot call `==` on two `MkEq` boxes*, because each hides a
possibly-different type and the dictionary only knows how to compare its own
type with itself. The restriction is enforced by the type checker at the *use
site*, not by forbidding the class. Tzopilotl instead rejects the *constraint*
when used existentially, up front, with a targeted message — a more
ergonomic failure mode for the same underlying limitation.

**Not real-time safe.** Lazy evaluation, thunk allocation, and a tracing GC
with no upper bound on pause make Haskell's model unsuitable for the audio
thread regardless of the elegance of dictionary passing.

---

## 5. Rust: `dyn Trait` trait objects

```rust
trait Draw { fn draw(&self) -> String; }
impl Draw for Circle { fn draw(&self) -> String { "circle".into() } }
impl Draw for Square { fn draw(&self) -> String { "square".into() } }

let shapes: Vec<Box<dyn Draw>> = vec![Box::new(Circle), Box::new(Square)];
for s in &shapes { println!("{}", s.draw()); }
```

**Mechanism.** A `dyn Trait` value is a **fat pointer**: `(data pointer,
vtable pointer)`. The vtable for each `(concrete type, trait)` pair is built
**statically by the compiler** and lives in the binary's read-only data; the
coercion `Box::new(Circle) as Box<dyn Draw>` just attaches the address of
`Circle`'s `Draw` vtable. So the dictionary exists at compile time and the
"pack" is a pointer store — even cheaper than Tzopilotl's object construction,
but the payload is *always behind a pointer* (no inline small-value buffer).

**Object safety is explicit and strict** — and is the design Tzopilotl's
analysis most resembles. Rust's rules: no generic methods, the trait must not
require `Self: Sized`, and **`Self` may not appear in method signatures except
as the receiver**. That last rule is precisely the binary-method exclusion:
`fn eq(&self, other: &Self)` makes `Eq` not object-safe, because a `dyn Eq`
has erased the type that `other: &Self` would need. Tzopilotl's "exactly one
parameter is exactly `T`, return is `T`-free" rule is the same constraint
phrased for a structural, multi-argument-function world.

**Naming.** Nominal, with coherence (the orphan rule) ensuring at most one
`impl` per `(type, trait)` — so unlike Haskell the world is open but
globally consistent.

**`dyn` vs `impl`.** Worth noting the dual: Rust's `impl Trait` is the
*universal/opaque* counterpart ("some specific type the *callee* picks, erased
from the *caller*"), whereas `dyn Trait` is the *existential* ("some type the
*caller* picked, erased from the *callee*"). Tzopilotl's `some C` is the `dyn`
side of this duality. (Confusingly, Swift names them the opposite way around —
see below.)

**Real-time safe.** Yes — static vtables, predictable dispatch. Rust reaches
RT-safety by having *no* GC at all and manual ownership; Tzopilotl reaches it
differently, with a TLSF allocator and a bounded-pause incremental GC, so it
can allocate freely where Rust manages memory by hand. On the dispatch
mechanism itself they are close peers; the divergences are
nominal-vs-structural and heap-pointer-vs-inline payload.

---

## 6. Swift: `any P` existentials and protocol witness tables

```swift
protocol Draw { func draw() -> String }
extension Circle: Draw { func draw() -> String { "circle" } }
extension Square: Draw { func draw() -> String { "square" } }

let shapes: [any Draw] = [Circle(), Square()]
for s in shapes { print(s.draw()) }
```

**Mechanism.** A Swift existential is an **existential container**: a small
fixed-size buffer (historically three machine words) that stores the payload
**inline if it fits**, else a pointer to a heap box, *plus* pointers to the
**protocol witness table** (PWT, the method dictionary) and value-witness
table (for copy/destroy). The PWT is generated statically by the compiler per
conformance and referenced at the coercion site.

**Closest kinship on representation.** Swift's inline-buffer-or-box container
is the nearest analogue to Tzopilotl's **inline payload words**: both avoid a
heap indirection for small values. Tzopilotl always allocates the one
`Existential` object (it is a GC heap object), but the *payload* lives inline
in that object's `slots`, not behind a further pointer — structurally similar
to Swift packing a small value into the container's buffer.

**The `some`/`any` naming inversion.** This is the sharpest terminological
contrast in this whole comparison. In **Swift**, `some P` is the *opaque
result type* (universal — like Rust's `impl Trait`), and `any P` is the
*existential*. In **Tzopilotl**, `some C` **is the existential**. So the same
keyword names opposite features. A reader fluent in Swift must consciously
re-map: Tzopilotl `some C` ≈ Swift `any P`, not Swift `some P`.

**Object safety.** Swift's historical restriction is that a protocol with
`Self` requirements or **associated types** "can only be used as a generic
constraint," not as an existential type — the same binary-method / hidden-type
obstruction, surfaced as the famous *"Protocol can only be used as a generic
constraint because it has Self or associated type requirements"* error.
Swift 5.7's `any` and primary associated types relaxed parts of this, but the
core obstruction (you can't call a `Self -> Self -> Bool` method across two
erased values) remains. Tzopilotl's analysis covers the same ground for its
function-style (non-method) requirements.

**Naming.** Nominal, with retroactive conformance via `extension` — an open
world like Rust's, coherence-checked.

**Real-time safety.** Mostly — static PWTs and inline small payloads are
RT-friendly. A payload larger than the inline buffer triggers a heap
allocation through the *system* allocator on boxing, and ARC retain/release
plus Swift's runtime are not built around a bounded pause; *that* — the system
allocator and unbounded runtime work, not the act of allocating — is what a
hard-real-time context must avoid. Tzopilotl boxes too (its `Existential` is
always a heap object), but through its TLSF allocator and bounded-pause GC,
which is why the same operation stays RT-safe for it.

---

## 7. Go: interfaces

```go
type Draw interface { draw() string }
func (c Circle) draw() string { return "circle" }
func (s Square) draw() string { return "square" }

shapes := []Draw{Circle{}, Square{}}
for _, s := range shapes { fmt.Println(s.draw()) }
```

**Mechanism.** A Go interface value is `iface{ itab*, data* }`. The **itab**
(interface table) pairs the interface type with a concrete type and holds the
method pointers — it is the dictionary. Critically, **itabs are built lazily
at runtime** the first time a given `(interface, concrete)` pair is needed,
then cached in a global hash table. The `data` word points to the value (boxed
on the heap if it doesn't fit in a word).

**Closest kinship on naming discipline.** Go and Tzopilotl are the two
**structural** systems here: a type satisfies an interface/constraint merely by
having the right methods — no `impl`/`instance`/`extension` declaration. A
Tzopilotl `constraint C<T> = requires { draw(T) String; }` and a Go
`interface { draw() string }` express the same "any type with this shape"
idea. This is the axis on which Tzopilotl departs furthest from
Haskell/Rust/Swift and aligns with Go.

**But the opposite choice on dictionary timing.** Go builds itabs *lazily at
runtime* — allocating via the system allocator and inserting into a global,
lock-protected hash table on first use; Tzopilotl resolves every witness to a
global index *at the compile-time-known pack site*. It is tempting to call
Go's laziness the real-time disqualifier, but laziness and allocation are not
themselves the problem (Tzopilotl could build a dictionary lazily through TLSF
and stay safe). What Go actually does that an audio thread cannot is the
*system-allocator* call and the *lock* on the shared itab table. Tzopilotl's
pack-site resolution avoids both — not as its primary purpose, but as a side
effect of the concrete type already being known at compile time.

**Object safety.** Go has no `Self` type, so the binary-method problem doesn't
arise the same way. A "compare two shapes" operation is written as
`Equal(other Shape) bool` taking the *interface* type, and the implementation
recovers the concrete type with a runtime **type assertion** / type switch. So
Go permits binary-method-like APIs but pushes the "are these the same hidden
type?" question to a *dynamic* check, trading Tzopilotl's static rejection for
runtime flexibility (and a possible runtime failure).

**Real-time safety.** No — but, precisely: it is the *system-allocator* itab
and interface-conversion boxing, the *lock* on the shared itab table, and a
concurrent GC not built around a hard pause bound that make Go unsuitable —
not the mere fact that it allocates. Tzopilotl allocates too; it just does so
through machinery with O(1) and bounded-pause guarantees.

---

## 8. Where Tzopilotl sits — synthesis

Tzopilotl's existentials are best understood as **Go's structural interfaces
with Rust's compile-time resolution discipline and Swift's inline payload
storage, gated by an explicit object-safety analysis like Rust's** — running
on a real-time-safe substrate (TLSF + bounded-pause GC) that *permits*
allocation rather than forbidding it.

- **From Go:** structural conformance. No `impl`/`instance` ceremony; a type
  qualifies by having the required functions in scope. This fits Tzopilotl's
  function-centric (non-OO) surface, where `draw(c Circle)` is a free function,
  not a method on `Circle`.
- **From Rust/Swift:** the dictionary is resolved at compile time and the
  "pack" attaches a fixed table — but Tzopilotl resolves to **global code
  indices** (integers) rather than raw function pointers, fitting its
  direct-threaded VM and its movable GC heap.
- **From Swift:** the payload lives **inline** in the existential object, not
  behind a second pointer — cheaper access, fewer indirections.
- **From Rust:** an **explicit, enforced object-safety rule** that rejects
  binary-method constraints up front, rather than Haskell's "legal but
  unusable" or Go's "defer it to a runtime assertion."

The one property **only Tzopilotl** carries through the whole design is **hard
real-time safety** — but its source must be stated precisely, because it is
easy to mis-locate. RT-safety comes from the *runtime substrate*: the TLSF
allocator (O(1)) and the bounded-pause incremental GC. Allocation is allowed;
what is forbidden is the system allocator, blocking syscalls, and unbounded
pauses. So pack-site resolution is **not** forced by RT-safety — Tzopilotl
could resolve lazily through TLSF and remain safe. It packs at the pack site
because the concrete type is statically known there and the resolver is a
compile-time component, so runtime resolution would be redundant machinery.
Where RT-safety *does* decide the comparison is which peers' runtimes qualify
at all: Go and Haskell fall out because they lean on the system allocator and
on GCs without a bounded pause — again, not because they allocate. The genuine
novelty is orthogonal to RT-safety: taking Go's structural ergonomics *and*
Rust/Swift's fully-static resolution at once, which static typing at the pack
site makes free.

### Trade-offs Tzopilotl accepts

- **Closed-per-module world.** Because witnesses resolve at the pack site from
  the functions visible there, you cannot retroactively make a foreign type
  satisfy a constraint from a third module the way Haskell orphan instances or
  Swift retroactive `extension`s allow. Note this follows from *static
  pack-site resolution*, not from real-time safety — a lazier, TLSF-backed
  resolver could reopen it without sacrificing RT-safety; it simply has not
  been needed.
- **Single dispatching argument (today).** Multi-argument witness dispatch and
  return-`T` re-packing are deferred. Rust/Swift handle return-`Self` (it
  re-wraps); Tzopilotl will need the re-pack path to match. Genuine binary
  methods stay rejected in *all* of these systems (Go only "supports" them via
  dynamic assertion).

---

## 9. One-line summary per language

- **Tzopilotl** — structural constraints, witness dictionary resolved to
  global indices **at the pack site** (enabled by static typing, not forced by
  RT-safety), inline payload, explicit object-safety rejection of binary
  methods; real-time safety comes from the TLSF allocator and bounded-pause GC,
  which *permit* allocation rather than forbidding it.
- **Haskell** — nominal classes compiled by dictionary passing; existential
  `data` captures the dictionary at the box; object-unsafe classes are *legal
  but unusable*; not RT-safe.
- **Rust** — nominal traits; `dyn Trait` fat pointer with a **static vtable**;
  strict, explicit object-safety rules; RT-safe; payload always behind a
  pointer.
- **Swift** — nominal protocols; `any P` existential container (inline buffer
  or box) + static **protocol witness table**; `some P` means the *opposite*
  (opaque/universal); `Self`/associated-type requirements restrict existential
  use.
- **Go** — structural interfaces; `iface{itab, data}` with **lazily-built,
  cached itabs**; no `Self`, so binary-method-like APIs go through runtime type
  assertions; not RT-safe.
