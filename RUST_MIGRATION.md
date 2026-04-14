# GroveEngine - Plan de Migration Rust

## Pourquoi Migrer ?

### Problèmes actuels en C++
- Debugging du renderer = semaines de souffrance
- Threading naïf non testé = bombe à retardement (race conditions, crashes aléatoires)
- Erreurs mémoire silencieuses (dangling pointers, use-after-free)
- Bugs qui disparaissent quand on debug (Heisenbugs)

### Ce que Rust apporte
- **Sécurité mémoire garantie à la compilation**
- **Zero data races** - le compilateur refuse le code concurrent bugué
- **Erreurs explicites** - pas de crashes silencieux
- **Écosystème moderne** - cargo, tokio, wgpu, serde

---

## Architecture Cible

```
GroveEngine v2 (Rust)
├── crates/
│   ├── grove-core/           # Engine core (exe)
│   │   ├── engine.rs         # Main loop, scheduling
│   │   ├── iio.rs            # Pub/sub thread-safe (tokio channels)
│   │   └── module_system.rs  # Module loading & execution
│   │
│   ├── grove-api/            # Interface stable (abi_stable)
│   │   └── lib.rs            # Traits & types pour hot-reload
│   │
│   ├── grove-renderer/       # wgpu renderer
│   │   ├── sprite_batch.rs
│   │   ├── tilemap.rs
│   │   ├── particles.rs
│   │   ├── text.rs
│   │   └── post_process.rs
│   │
│   └── grove-ffi/            # FFI pour modules C++ legacy
│       └── lib.rs
│
└── modules/                   # Hot-reloadable (cdylib)
    ├── game-logic/
    ├── ui-module/
    └── input-module/
```

---

## Comparaison C++ vs Rust

### Threading

**C++ (dangereux) :**
```cpp
std::thread t1([&]() { moduleA->update(delta); });
std::thread t2([&]() { moduleB->update(delta); });
// Race condition sur l'IIO partagé
// Crash aléatoire 1 fois sur 50
```

**Rust (safe) :**
```rust
std::thread::spawn(|| {
    module_a.update(delta);  // COMPILE ERROR si race possible
});
```

### Rendering

**C++ (bgfx) - Problèmes silencieux :**
```cpp
memcpy(tvb.data, vertices.data(), vertexCount * sizeof(Vertex));
bgfx::setTexture(0, s_texture, texture);  // Handle invalide = crash silencieux
bgfx::submit(viewId, program);  // Program corrompu = écran noir mystère
```

**Rust (wgpu) - Erreurs explicites :**
```rust
queue.write_buffer(&self.vertex_buffer, 0, bytemuck::cast_slice(&self.vertices));
render_pass.set_pipeline(&self.pipeline);  // Compile error si invalide
render_pass.draw(0..self.vertex_count, 0..1);
```

### Gestion des erreurs

| Problème | C++ | Rust |
|----------|-----|------|
| Null pointer | Runtime crash | `Option<T>` - compile error |
| Buffer overflow | Silent corruption | Bounds check - panic explicite |
| Use after free | UB silencieux | Compile error |
| Data race | Crash aléatoire | Compile error |
| Integer overflow | Silent wraparound | Compile error (mode strict) |

---

## Compilateur Mode Strict ("Nazi Mode")

### Configuration Cargo.toml

```toml
[lints.rust]
unsafe_code = "forbid"
missing_docs = "deny"
missing_debug_implementations = "deny"
rust_2018_idioms = "deny"
trivial_casts = "deny"
trivial_numeric_casts = "deny"
unused_lifetimes = "deny"
unused_qualifications = "deny"
unused_results = "deny"

[lints.clippy]
all = "deny"
pedantic = "deny"
nursery = "warn"
cargo = "warn"

# Interdictions absolues
unwrap_used = "deny"
expect_used = "deny"
panic = "deny"
indexing_slicing = "deny"
arithmetic_side_effects = "deny"
as_conversions = "deny"
clone_on_ref_ptr = "deny"
dbg_macro = "deny"
default_numeric_fallback = "deny"
float_cmp = "deny"
get_unwrap = "deny"
implicit_clone = "deny"
lossy_float_literal = "deny"
mem_forget = "deny"
shadow_unrelated = "deny"
string_add = "deny"
todo = "deny"
unimplemented = "deny"
```

### Ce qui est interdit

```rust
// unwrap interdit
let val = some_option.unwrap();
// Gestion explicite obligatoire
let val = some_option.ok_or(Error::Missing)?;

// Indexation interdite (peut panic)
let x = array[i];
// Bounds check explicite
let x = array.get(i).ok_or(Error::OutOfBounds)?;

// Overflow potentiel interdit
let sum = a + b;
// Checked arithmetic
let sum = a.checked_add(b).ok_or(Error::Overflow)?;

// Cast 'as' interdit (perte de données silencieuse)
let small = big_number as u8;
// Conversion explicite
let small = u8::try_from(big_number)?;

// panic!() interdit
panic!("something went wrong");
// Retourner une erreur
return Err(Error::SomethingWrong);
```

---

## Hot-Reload en Rust

### Option 1 : `abi_stable` (Recommandé pour production)

```rust
// grove-api/src/lib.rs
use abi_stable::{
    StableAbi,
    library::RootModule,
    package_version_strings,
    sabi_types::VersionStrings,
    std_types::RStr,
};

#[repr(C)]
#[derive(StableAbi)]
#[sabi(kind(Prefix))]
pub struct ModuleRef {
    pub name: extern "C" fn() -> RStr<'static>,
    pub init: extern "C" fn(),
    pub update: extern "C" fn(f32),
    pub shutdown: extern "C" fn(),
}

impl RootModule for ModuleRef_Ref {
    const BASE_NAME: &'static str = "grove_module";
    const NAME: &'static str = "grove_module";
    const VERSION_STRINGS: VersionStrings = package_version_strings!();

    abi_stable::declare_root_module_statics!{ModuleRef_Ref}
}
```

```rust
// Module hot-reloadable
use abi_stable::{export_root_module, prefix_type::PrefixTypeTrait};

#[export_root_module]
fn get_module() -> ModuleRef_Ref {
    ModuleRef { name, init, update, shutdown }.leak_into_prefix()
}

extern "C" fn name() -> RStr<'static> { "GameLogic".into() }
extern "C" fn init() { /* ... */ }
extern "C" fn update(delta: f32) { /* logique de jeu */ }
extern "C" fn shutdown() { /* ... */ }
```

### Option 2 : `hot-lib-reloader` (Simple pour dev)

```rust
// Cargo.toml
[dependencies]
hot-lib-reloader = "0.7"

// Core
use hot_lib_reloader::*;

hot_lib_reloader::define_lib_reloader! {
    unsafe GameLogicLib {
        source: "target/debug",
        lib: "game_logic",
        functions: {
            fn update(delta: f32);
        }
    }
}

fn main() {
    let mut lib = GameLogicLib::new().unwrap();
    loop {
        lib.update_lib().unwrap();  // Recharge si fichier modifié
        lib.update(0.016);
    }
}
```

### Option 3 : WASM (Pour mods utilisateurs)

```rust
use wasmtime::*;

pub struct WasmModule {
    instance: Instance,
    update: TypedFunc<f32, ()>,
}

impl WasmModule {
    pub fn hot_reload(&mut self, engine: &Engine, path: &Path) -> Result<()> {
        *self = Self::load(engine, path)?;
        Ok(())
    }
}
```

### Comparaison

| Méthode | Complexité | Performance | Safety |
|---------|------------|-------------|--------|
| `abi_stable` | Moyenne | Native | ABI vérifié |
| `hot-lib-reloader` | Facile | Native | Moins safe |
| WASM | Moyenne | -10-20% | Sandboxed |
| FFI C++ | Facile | Native | Manuel |

---

## Renderer wgpu

### Pourquoi wgpu > bgfx

| Aspect | bgfx (C++) | wgpu (Rust) |
|--------|------------|-------------|
| API | C-style, raw pointers | Safe, typé |
| Buffers | `void*`, taille manuelle | `&[T]`, bounds checked |
| Erreurs | Silent fail / crash | `Result<T, E>` explicite |
| Debug | RenderDoc + prières | Validation layers intégrées |
| Backends | DX9-12, GL, Vulkan, Metal | DX12, GL, Vulkan, Metal, WebGPU |

### Exemple Sprite Batch

```rust
use wgpu::util::DeviceExt;
use glam::{Vec2, Vec4};
use bytemuck::{Pod, Zeroable};

#[repr(C)]
#[derive(Copy, Clone, Pod, Zeroable)]
struct Vertex {
    position: [f32; 2],
    uv: [f32; 2],
    color: [f32; 4],
}

pub struct SpriteBatch {
    pipeline: wgpu::RenderPipeline,
    vertex_buffer: wgpu::Buffer,
    bind_group: wgpu::BindGroup,
    vertices: Vec<Vertex>,
}

impl SpriteBatch {
    pub fn flush(&mut self, queue: &wgpu::Queue, render_pass: &mut wgpu::RenderPass) {
        queue.write_buffer(&self.vertex_buffer, 0, bytemuck::cast_slice(&self.vertices));

        render_pass.set_pipeline(&self.pipeline);
        render_pass.set_bind_group(0, &self.bind_group, &[]);
        render_pass.set_vertex_buffer(0, self.vertex_buffer.slice(..));
        render_pass.draw(0..self.vertices.len() as u32, 0..1);

        self.vertices.clear();
    }
}
```

### Validation Layers

```rust
let instance = wgpu::Instance::new(wgpu::InstanceDescriptor {
    backends: wgpu::Backends::all(),
    flags: wgpu::InstanceFlags::VALIDATION,  // Erreurs claires!
    ..Default::default()
});

// Messages d'erreur explicites:
// [wgpu validation error] Buffer size 1024 is too small for write of 2048 bytes
// [wgpu validation error] Bind group layout mismatch at index 0
```

---

## Réseau avec Tokio

### IIO Thread-Safe

```rust
use tokio::sync::broadcast;

pub struct IntraIO {
    sender: broadcast::Sender<Message>,
}

impl IntraIO {
    pub fn new() -> Self {
        let (sender, _) = broadcast::channel(1024);
        Self { sender }
    }

    pub fn publish(&self, topic: &str, data: Vec<u8>) {
        let _ = self.sender.send(Message { topic: topic.into(), data });
    }

    pub fn subscribe(&self) -> broadcast::Receiver<Message> {
        self.sender.subscribe()
    }
}
// ZERO data race possible. Garanti par le compilateur.
```

### NetworkIO

```rust
use tokio::net::TcpStream;
use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize)]
struct Message {
    topic: String,
    data: Vec<u8>,
}

pub struct NetworkIO {
    local_tx: broadcast::Sender<Message>,
    peers: HashMap<PeerId, TcpStream>,
}

impl NetworkIO {
    pub async fn publish(&self, topic: &str, data: &[u8]) {
        let msg = Message { topic: topic.into(), data: data.into() };

        // Local
        let _ = self.local_tx.send(msg.clone());

        // Remote (async, non-bloquant)
        for (_, peer) in &mut self.peers {
            let bytes = bincode::serialize(&msg).unwrap();
            peer.write_all(&bytes).await.ok();
        }
    }
}
```

---

## SIMD

### Auto-vectorisation

```bash
RUSTFLAGS="-C target-cpu=native" cargo build --release
```

### Bibliothèque `wide` (recommandée)

```rust
use wide::*;

fn dot_product(a: &[f32], b: &[f32]) -> f32 {
    let mut sum = f32x8::ZERO;

    for (ca, cb) in a.chunks_exact(8).zip(b.chunks_exact(8)) {
        let va = f32x8::from(ca);
        let vb = f32x8::from(cb);
        sum = va.mul_add(vb, sum);
    }

    sum.reduce_add()
}
```

### Intrinsics (si nécessaire)

```rust
#[cfg(target_arch = "x86_64")]
use std::arch::x86_64::*;

unsafe fn dot_product_avx(a: &[f32], b: &[f32]) -> f32 {
    let mut sum = _mm256_setzero_ps();

    for i in (0..a.len()).step_by(8) {
        let va = _mm256_loadu_ps(a.as_ptr().add(i));
        let vb = _mm256_loadu_ps(b.as_ptr().add(i));
        sum = _mm256_fmadd_ps(va, vb, sum);
    }
    // ...
}
```

---

## Dev Assisté par IA/LLM

### Pourquoi Rust > C++ pour le dev LLM

**Boucle de dev :**
```
C++:   Prompt → Code → Compile ✓ → Run → Crash → Debug 2h → Fix
Rust:  Prompt → Code → Compile ✗ → Fix → Compile ✓ → Run ✓
```

Le compilateur Rust agit comme un **second LLM qui review le code**.

**Exemple - Code généré par LLM :**

```cpp
// C++ - LLM génère ça, ça compile, ça crash
char* getName() {
    std::string name = "hello";
    return name.c_str();  // Dangling pointer - découvert 3h plus tard
}
```

```rust
// Rust - LLM génère ça
fn get_name() -> &str {
    let name = String::from("hello");
    &name  // COMPILE ERROR immédiat - LLM corrige
}
```

---

## Plan de Migration

### Phase 1 : Core Rust (Semaine 1)
- [ ] Setup workspace Cargo
- [ ] IIO avec `tokio::sync::broadcast`
- [ ] ModuleSystem avec traits
- [ ] FFI stubs pour modules C++ existants

### Phase 2 : Renderer wgpu (Semaine 2)
- [ ] Window + surface (`winit` + `wgpu`)
- [ ] Sprite batch
- [ ] Tilemap renderer
- [ ] Text rendering (`glyphon`)
- [ ] Particles

### Phase 3 : Intégration (Semaine 3)
- [ ] Hot-reload avec `hot-lib-reloader`
- [ ] Brancher modules C++ legacy via FFI
- [ ] Tests
- [ ] Premier jeu qui tourne

### Phase 4 : Production (Semaine 4+)
- [ ] Migration vers `abi_stable`
- [ ] Mode compilateur strict activé
- [ ] NetworkIO avec tokio
- [ ] Documentation

---

## Dépendances Principales

```toml
[workspace.dependencies]
# Async runtime
tokio = { version = "1", features = ["rt-multi-thread", "sync"] }

# Rendering
wgpu = "0.19"
winit = "0.29"
glyphon = "0.5"  # Text rendering

# Math
glam = "0.25"
bytemuck = { version = "1", features = ["derive"] }

# Serialization
serde = { version = "1", features = ["derive"] }
bincode = "1"

# Hot-reload
abi_stable = "0.11"
hot-lib-reloader = "0.7"  # Dev only
libloading = "0.8"

# SIMD
wide = "0.7"
```

---

## Ressources

- **The Rust Book** : https://doc.rust-lang.org/book/
- **Rust for Rustaceans** (avancé)
- **wgpu tutorial** : https://sotrh.github.io/learn-wgpu/
- **tokio tutorial** : https://tokio.rs/tokio/tutorial
- **abi_stable docs** : https://docs.rs/abi_stable
- **Bevy Engine** (référence ECS Rust) : https://bevyengine.org/

---

## Décision

**Date** : 2026-01-23

**Choix** : Migration vers Rust avec architecture hybride

**Raisons** :
1. Engine encore barebone - coût de migration faible
2. Threading pas testé - éviter la dette technique
3. Renderer douloureux - wgpu résout le problème
4. Compilateur strict = filet de sécurité pour dev LLM
5. Hot-reload possible avec `abi_stable`

**Risques acceptés** :
- Courbe d'apprentissage Rust (~2-3 mois)
- Hot-reload légèrement plus complexe qu'en C++
- SIMD moins mature (mais suffisant avec `wide`)
