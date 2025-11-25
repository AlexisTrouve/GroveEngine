# Plan d'implémentation - BgfxRenderer Module

## Vue d'ensemble

Module de rendu 2D basé sur bgfx, intégré à GroveEngine comme `IModule`.
Architecture task-based, MT-ready dès le départ.

### Intégration GroveEngine

Le module suit l'architecture GroveEngine standard :

| Aspect | Mécanisme | Usage |
|--------|-----------|-------|
| **Configuration** | `IDataNode` via `setConfiguration()` | Window size, backend, paths, vsync |
| **Communication** | `IIO` pub/sub | Sprites, camera, clear color par frame |
| **State** | `getState()`/`setState()` | Hot-reload du module renderer |
| **Input frame** | `process(const IDataNode& input)` | deltaTime, frameCount |

### Séparation Config vs Messages

**Via `setConfiguration(const IDataNode& config, IIO* io, ...)`** - Paramètres statiques :
```cpp
config.getInt("windowWidth", 1280);
config.getInt("windowHeight", 720);
config.getString("backend", "opengl");      // opengl, vulkan, dx11, metal
config.getString("shaderPath", "./shaders");
config.getBool("vsync", true);
config.getInt("maxSpritesPerBatch", 10000);
config.getInt("frameAllocatorSizeMB", 16);
// Window handle: config.getInt("nativeWindowHandle", 0) ou via WindowModule
```

**Via `IIO` subscribe/pull dans `process()`** - Données temps réel par frame :
```
render:sprite         → SpriteInstance à dessiner
render:sprite:batch   → Batch de sprites (array)
render:tilemap        → TilemapChunk
render:text           → TextCommand
render:particle       → ParticleInstance
render:camera         → ViewInfo (position, zoom, viewport)
render:clear          → Clear color RGBA
render:debug:line     → Debug line
render:debug:rect     → Debug rectangle
window:resize         → Event resize (si WindowModule séparé)
```

---

## Architecture globale

```
modules/
└── BgfxRenderer/
    ├── BgfxRendererModule.cpp    # Point d'entrée IModule
    │
    ├── RHI/                       # Render Hardware Interface
    │   ├── RHITypes.h            # Handles typés, enums
    │   ├── RHIDevice.h           # Interface device abstraite
    │   ├── RHICommandBuffer.h    # Command recording (MT-safe)
    │   ├── RHIResources.h        # Buffer, Texture, Shader descriptors
    │   └── BgfxDevice.cpp        # Implémentation bgfx
    │
    ├── Frame/                     # Gestion multi-frame MT-ready
    │   ├── FramePacket.h         # Données immuables d'une frame
    │   ├── FrameAllocator.h      # Allocateur linéaire lock-free
    │   └── FrameSync.h           # Double/triple buffering
    │
    ├── TaskGraph/                 # Système de tâches
    │   ├── Task.h                # Unité de travail
    │   ├── TaskGraph.h           # DAG de tâches
    │   └── ITaskScheduler.h      # Interface scheduler (single/multi)
    │
    ├── RenderGraph/               # Organisation des passes
    │   ├── RenderGraph.h         # Compilation et exécution passes
    │   ├── RenderPass.h          # Interface pass abstraite
    │   └── PassResources.h       # Gestion ressources par pass
    │
    ├── Passes/                    # Passes concrètes 2D
    │   ├── ClearPass.cpp         # Clear du framebuffer
    │   ├── SpritePass.cpp        # Sprites + batching
    │   ├── TilemapPass.cpp       # Tilemaps
    │   ├── TextPass.cpp          # Rendu texte
    │   ├── ParticlePass.cpp      # Systèmes de particules
    │   └── DebugPass.cpp         # Debug shapes/lines
    │
    ├── Resources/                 # Gestion assets GPU
    │   ├── ResourceCache.h       # Cache unifié
    │   ├── TextureManager.h      # Chargement/gestion textures
    │   ├── ShaderManager.h       # Compilation/cache shaders
    │   └── FontManager.h         # Fonts + atlas
    │
    ├── Scene/                     # Collecte des renderables
    │   ├── SceneCollector.h      # Collecte depuis IIO
    │   ├── SceneProxy.h          # Vue immuable pour rendu
    │   ├── RenderBatch.h         # Batching par texture/shader
    │   └── SortKey.h             # Tri par layer/depth
    │
    └── Shaders/                   # Sources shaders
        ├── sprite.sc             # Vertex/fragment sprites
        ├── tilemap.sc            # Tilemaps
        ├── text.sc               # Text rendering
        └── varying.def.sc        # Définitions communes
```

---

## Phase 1 : Fondations (RHI + Frame)

### 1.1 RHI Types et Handles

```cpp
// RHI/RHITypes.h
#pragma once
#include <cstdint>

namespace grove::rhi {

// Handles typés - jamais de bgfx:: exposé hors de BgfxDevice
struct TextureHandle { uint16_t id = UINT16_MAX; bool isValid() const { return id != UINT16_MAX; } };
struct BufferHandle { uint16_t id = UINT16_MAX; bool isValid() const { return id != UINT16_MAX; } };
struct ShaderHandle { uint16_t id = UINT16_MAX; bool isValid() const { return id != UINT16_MAX; } };
struct UniformHandle { uint16_t id = UINT16_MAX; bool isValid() const { return id != UINT16_MAX; } };
struct FramebufferHandle { uint16_t id = UINT16_MAX; bool isValid() const { return id != UINT16_MAX; } };

using ViewId = uint16_t;

// Render states
enum class BlendMode : uint8_t {
    None,
    Alpha,
    Additive,
    Multiply
};

enum class CullMode : uint8_t {
    None,
    CW,
    CCW
};

struct RenderState {
    BlendMode blend = BlendMode::Alpha;
    CullMode cull = CullMode::None;
    bool depthTest = false;
    bool depthWrite = false;
};

// Vertex layouts
struct VertexLayout {
    enum Attrib : uint8_t {
        Position,   // float3
        TexCoord0,  // float2
        Color0,     // uint32 RGBA
        Normal,     // float3
        Count
    };
    uint32_t stride = 0;
    uint16_t offsets[Attrib::Count] = {};
    bool has[Attrib::Count] = {};
};

// Descriptors pour création
struct TextureDesc {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t mipLevels = 1;
    enum Format { RGBA8, RGB8, R8, DXT1, DXT5 } format = RGBA8;
    const void* data = nullptr;
    uint32_t dataSize = 0;
};

struct BufferDesc {
    uint32_t size = 0;
    const void* data = nullptr;
    bool dynamic = false;
    enum Type { Vertex, Index, Instance } type = Vertex;
};

struct ShaderDesc {
    const void* vsData = nullptr;
    uint32_t vsSize = 0;
    const void* fsData = nullptr;
    uint32_t fsSize = 0;
};

} // namespace grove::rhi
```

### 1.2 RHI Device Interface

```cpp
// RHI/RHIDevice.h
#pragma once
#include "RHITypes.h"
#include <memory>
#include <string>

namespace grove::rhi {

struct DeviceCapabilities {
    uint16_t maxTextureSize;
    uint16_t maxViews;
    uint32_t maxDrawCalls;
    bool instancingSupported;
    bool computeSupported;
    std::string rendererName;
    std::string gpuName;
};

class IRHIDevice {
public:
    virtual ~IRHIDevice() = default;

    // Lifecycle
    virtual bool init(void* nativeWindowHandle, uint16_t width, uint16_t height) = 0;
    virtual void shutdown() = 0;
    virtual void reset(uint16_t width, uint16_t height) = 0;

    // Capabilities
    virtual DeviceCapabilities getCapabilities() const = 0;

    // Resource creation
    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual UniformHandle createUniform(const char* name, uint8_t numVec4s) = 0;

    // Resource destruction
    virtual void destroy(TextureHandle handle) = 0;
    virtual void destroy(BufferHandle handle) = 0;
    virtual void destroy(ShaderHandle handle) = 0;
    virtual void destroy(UniformHandle handle) = 0;

    // Dynamic updates
    virtual void updateBuffer(BufferHandle handle, const void* data, uint32_t size) = 0;
    virtual void updateTexture(TextureHandle handle, const void* data, uint32_t size) = 0;

    // View setup
    virtual void setViewClear(ViewId id, uint32_t rgba, float depth) = 0;
    virtual void setViewRect(ViewId id, uint16_t x, uint16_t y, uint16_t w, uint16_t h) = 0;
    virtual void setViewTransform(ViewId id, const float* view, const float* proj) = 0;

    // Frame
    virtual void frame() = 0;

    // Factory
    static std::unique_ptr<IRHIDevice> create();
};

} // namespace grove::rhi
```

### 1.3 Command Buffer (MT-safe)

```cpp
// RHI/RHICommandBuffer.h
#pragma once
#include "RHITypes.h"
#include <vector>
#include <cstring>

namespace grove::rhi {

// Commandes encodées - POD pour sérialisation
enum class CommandType : uint8_t {
    SetState,
    SetTexture,
    SetUniform,
    SetVertexBuffer,
    SetIndexBuffer,
    SetInstanceBuffer,
    SetScissor,
    Draw,
    DrawIndexed,
    DrawInstanced,
    Submit
};

struct Command {
    CommandType type;
    union {
        struct { RenderState state; } setState;
        struct { uint8_t slot; TextureHandle texture; UniformHandle sampler; } setTexture;
        struct { UniformHandle uniform; float data[16]; uint8_t numVec4s; } setUniform;
        struct { BufferHandle buffer; uint32_t offset; } setVertexBuffer;
        struct { BufferHandle buffer; uint32_t offset; bool is32Bit; } setIndexBuffer;
        struct { BufferHandle buffer; uint32_t start; uint32_t count; } setInstanceBuffer;
        struct { uint16_t x, y, w, h; } setScissor;
        struct { uint32_t vertexCount; uint32_t startVertex; } draw;
        struct { uint32_t indexCount; uint32_t startIndex; } drawIndexed;
        struct { uint32_t indexCount; uint32_t instanceCount; } drawInstanced;
        struct { ViewId view; ShaderHandle shader; uint32_t depth; } submit;
    };
};

// Command buffer - un par thread, write-only pendant l'enregistrement
class RHICommandBuffer {
public:
    RHICommandBuffer() = default;

    // Non-copiable, movable
    RHICommandBuffer(const RHICommandBuffer&) = delete;
    RHICommandBuffer& operator=(const RHICommandBuffer&) = delete;
    RHICommandBuffer(RHICommandBuffer&&) = default;
    RHICommandBuffer& operator=(RHICommandBuffer&&) = default;

    // Enregistrement des commandes
    void setState(const RenderState& state) {
        Command cmd; cmd.type = CommandType::SetState;
        cmd.setState.state = state;
        commands_.push_back(cmd);
    }

    void setTexture(uint8_t slot, TextureHandle tex, UniformHandle sampler) {
        Command cmd; cmd.type = CommandType::SetTexture;
        cmd.setTexture = {slot, tex, sampler};
        commands_.push_back(cmd);
    }

    void setUniform(UniformHandle uniform, const float* data, uint8_t numVec4s) {
        Command cmd; cmd.type = CommandType::SetUniform;
        cmd.setUniform.uniform = uniform;
        cmd.setUniform.numVec4s = numVec4s;
        std::memcpy(cmd.setUniform.data, data, numVec4s * 16);
        commands_.push_back(cmd);
    }

    void setVertexBuffer(BufferHandle buffer, uint32_t offset = 0) {
        Command cmd; cmd.type = CommandType::SetVertexBuffer;
        cmd.setVertexBuffer = {buffer, offset};
        commands_.push_back(cmd);
    }

    void setIndexBuffer(BufferHandle buffer, uint32_t offset = 0, bool is32Bit = false) {
        Command cmd; cmd.type = CommandType::SetIndexBuffer;
        cmd.setIndexBuffer = {buffer, offset, is32Bit};
        commands_.push_back(cmd);
    }

    void setInstanceBuffer(BufferHandle buffer, uint32_t start, uint32_t count) {
        Command cmd; cmd.type = CommandType::SetInstanceBuffer;
        cmd.setInstanceBuffer = {buffer, start, count};
        commands_.push_back(cmd);
    }

    void setScissor(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
        Command cmd; cmd.type = CommandType::SetScissor;
        cmd.setScissor = {x, y, w, h};
        commands_.push_back(cmd);
    }

    void draw(uint32_t vertexCount, uint32_t startVertex = 0) {
        Command cmd; cmd.type = CommandType::Draw;
        cmd.draw = {vertexCount, startVertex};
        commands_.push_back(cmd);
    }

    void drawIndexed(uint32_t indexCount, uint32_t startIndex = 0) {
        Command cmd; cmd.type = CommandType::DrawIndexed;
        cmd.drawIndexed = {indexCount, startIndex};
        commands_.push_back(cmd);
    }

    void drawInstanced(uint32_t indexCount, uint32_t instanceCount) {
        Command cmd; cmd.type = CommandType::DrawInstanced;
        cmd.drawInstanced = {indexCount, instanceCount};
        commands_.push_back(cmd);
    }

    void submit(ViewId view, ShaderHandle shader, uint32_t depth = 0) {
        Command cmd; cmd.type = CommandType::Submit;
        cmd.submit = {view, shader, depth};
        commands_.push_back(cmd);
    }

    // Accès lecture seule pour exécution
    const std::vector<Command>& getCommands() const { return commands_; }

    void clear() { commands_.clear(); }
    size_t size() const { return commands_.size(); }

private:
    std::vector<Command> commands_;
};

} // namespace grove::rhi
```

### 1.4 Frame Allocator (lock-free)

```cpp
// Frame/FrameAllocator.h
#pragma once
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cassert>

namespace grove {

// Allocateur linéaire lock-free, reset chaque frame
class FrameAllocator {
public:
    static constexpr size_t DEFAULT_SIZE = 16 * 1024 * 1024; // 16 MB

    explicit FrameAllocator(size_t size = DEFAULT_SIZE)
        : buffer_(new uint8_t[size]), capacity_(size), offset_(0) {}

    ~FrameAllocator() { delete[] buffer_; }

    // Non-copiable
    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    // Allocation thread-safe, lock-free
    void* allocate(size_t size, size_t alignment = 16) {
        size_t current = offset_.load(std::memory_order_relaxed);
        size_t aligned;

        do {
            aligned = (current + alignment - 1) & ~(alignment - 1);
            if (aligned + size > capacity_) {
                return nullptr; // Out of memory
            }
        } while (!offset_.compare_exchange_weak(
            current, aligned + size,
            std::memory_order_release,
            std::memory_order_relaxed));

        return buffer_ + aligned;
    }

    // Allocation typée
    template<typename T, typename... Args>
    T* allocate(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        if (!ptr) return nullptr;
        return new (ptr) T(std::forward<Args>(args)...);
    }

    // Allocation array
    template<typename T>
    T* allocateArray(size_t count) {
        void* ptr = allocate(sizeof(T) * count, alignof(T));
        if (!ptr) return nullptr;
        for (size_t i = 0; i < count; ++i) {
            new (static_cast<T*>(ptr) + i) T();
        }
        return static_cast<T*>(ptr);
    }

    // Reset (appelé une fois par frame, single-thread)
    void reset() { offset_.store(0, std::memory_order_release); }

    // Stats
    size_t getUsed() const { return offset_.load(std::memory_order_acquire); }
    size_t getCapacity() const { return capacity_; }

private:
    uint8_t* buffer_;
    size_t capacity_;
    std::atomic<size_t> offset_;
};

} // namespace grove
```

### 1.5 Frame Packet (données immuables)

```cpp
// Frame/FramePacket.h
#pragma once
#include "FrameAllocator.h"
#include <cstdint>
#include <span>

namespace grove {

// Données sprite pour le rendu
struct SpriteInstance {
    float x, y;           // Position
    float scaleX, scaleY; // Scale
    float rotation;       // Radians
    float u0, v0, u1, v1; // UVs dans atlas
    uint32_t color;       // RGBA packed
    uint16_t textureId;   // Index dans texture array
    uint16_t layer;       // Z-order
};

// Données tilemap chunk
struct TilemapChunk {
    float x, y;           // Position du chunk
    uint16_t width, height;
    uint16_t tileWidth, tileHeight;
    uint16_t textureId;
    const uint16_t* tiles; // Indices dans tileset
};

// Données texte
struct TextCommand {
    float x, y;
    const char* text;     // Null-terminated, alloué dans FrameAllocator
    uint16_t fontId;
    uint16_t fontSize;
    uint32_t color;
    uint16_t layer;
};

// Données particule
struct ParticleInstance {
    float x, y;
    float vx, vy;
    float size;
    float life;           // 0-1, temps restant
    uint32_t color;
    uint16_t textureId;
};

// Vue caméra
struct ViewInfo {
    float viewMatrix[16];
    float projMatrix[16];
    float position[2];
    float zoom;
    uint16_t viewportX, viewportY;
    uint16_t viewportW, viewportH;
};

// Packet complet d'une frame - IMMUABLE après construction
struct FramePacket {
    uint64_t frameNumber;
    float deltaTime;

    // Données collectées (read-only pour les passes)
    std::span<const SpriteInstance> sprites;
    std::span<const TilemapChunk> tilemaps;
    std::span<const TextCommand> texts;
    std::span<const ParticleInstance> particles;

    // Vue principale
    ViewInfo mainView;

    // Clear color
    uint32_t clearColor;

    // Allocateur pour données temporaires des passes
    FrameAllocator* allocator;
};

} // namespace grove
```

---

## Phase 2 : Task Graph

### 2.1 Task Interface

```cpp
// TaskGraph/Task.h
#pragma once
#include <functional>
#include <string>
#include <cstdint>

namespace grove {

class RHICommandBuffer;
struct FramePacket;

using TaskId = uint32_t;
constexpr TaskId INVALID_TASK_ID = UINT32_MAX;

// Contexte passé à chaque task
struct TaskContext {
    const FramePacket* frame;
    rhi::RHICommandBuffer* commandBuffer; // Thread-local
    uint32_t threadIndex;
};

using TaskFunc = std::function<void(TaskContext&)>;

struct TaskDesc {
    std::string name;
    TaskFunc func;
    std::vector<TaskId> dependencies;
};

} // namespace grove
```

### 2.2 Task Graph

```cpp
// TaskGraph/TaskGraph.h
#pragma once
#include "Task.h"
#include <vector>
#include <memory>

namespace grove {

class ITaskScheduler;

class TaskGraph {
public:
    TaskGraph() = default;

    // Construction du graph
    TaskId addTask(const std::string& name, TaskFunc func);
    void addDependency(TaskId before, TaskId after);

    // Compilation (tri topologique, détection cycles)
    bool compile();

    // Exécution
    void execute(ITaskScheduler& scheduler, const FramePacket& frame);

    // Reset pour prochaine frame
    void clear();

    // Debug
    const std::vector<TaskDesc>& getTasks() const { return tasks_; }
    const std::vector<TaskId>& getExecutionOrder() const { return executionOrder_; }

private:
    std::vector<TaskDesc> tasks_;
    std::vector<TaskId> executionOrder_;
    bool compiled_ = false;
};

} // namespace grove
```

### 2.3 Task Scheduler Interface

```cpp
// TaskGraph/ITaskScheduler.h
#pragma once
#include "Task.h"
#include <vector>
#include <memory>

namespace grove {

namespace rhi { class RHICommandBuffer; }

class ITaskScheduler {
public:
    virtual ~ITaskScheduler() = default;

    // Exécute les tasks et retourne les command buffers générés
    virtual std::vector<rhi::RHICommandBuffer> execute(
        const std::vector<TaskDesc>& tasks,
        const std::vector<TaskId>& order,
        const FramePacket& frame
    ) = 0;
};

// Implémentation single-thread (Phase 1)
class SingleThreadScheduler : public ITaskScheduler {
public:
    std::vector<rhi::RHICommandBuffer> execute(
        const std::vector<TaskDesc>& tasks,
        const std::vector<TaskId>& order,
        const FramePacket& frame
    ) override;
};

// Implémentation multi-thread (Future)
// class ThreadPoolScheduler : public ITaskScheduler { ... };

} // namespace grove
```

---

## Phase 3 : Render Graph & Passes

### 3.1 Render Pass Interface

```cpp
// RenderGraph/RenderPass.h
#pragma once
#include "../RHI/RHICommandBuffer.h"
#include "../Frame/FramePacket.h"
#include <string>
#include <vector>

namespace grove {

class RenderPass {
public:
    virtual ~RenderPass() = default;

    // Identifiant unique
    virtual const char* getName() const = 0;

    // Ordre de rendu (plus petit = plus tôt)
    virtual uint32_t getSortOrder() const = 0;

    // Dépendances (noms des passes qui doivent s'exécuter avant)
    virtual std::vector<const char*> getDependencies() const { return {}; }

    // Exécution - DOIT être thread-safe
    // frame: read-only
    // cmd: write-only, thread-local
    virtual void execute(const FramePacket& frame, rhi::RHICommandBuffer& cmd) = 0;

    // Setup initial (chargement shaders, création buffers)
    virtual void setup(rhi::IRHIDevice& device) = 0;

    // Cleanup
    virtual void shutdown(rhi::IRHIDevice& device) = 0;
};

} // namespace grove
```

### 3.2 Render Graph

```cpp
// RenderGraph/RenderGraph.h
#pragma once
#include "RenderPass.h"
#include "../TaskGraph/TaskGraph.h"
#include <memory>
#include <vector>

namespace grove {

class RenderGraph {
public:
    RenderGraph() = default;

    // Enregistrement des passes
    void addPass(std::unique_ptr<RenderPass> pass);

    // Setup toutes les passes
    void setup(rhi::IRHIDevice& device);

    // Compile le graph (ordre, dépendances → TaskGraph)
    void compile();

    // Build le TaskGraph pour une frame
    void buildTasks(TaskGraph& taskGraph, const FramePacket& frame);

    // Shutdown
    void shutdown(rhi::IRHIDevice& device);

private:
    std::vector<std::unique_ptr<RenderPass>> passes_;
    std::vector<size_t> sortedIndices_;
    bool compiled_ = false;
};

} // namespace grove
```

### 3.3 Passes concrètes

#### ClearPass

```cpp
// Passes/ClearPass.h
#pragma once
#include "../RenderGraph/RenderPass.h"

namespace grove {

class ClearPass : public RenderPass {
public:
    const char* getName() const override { return "Clear"; }
    uint32_t getSortOrder() const override { return 0; } // Premier

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::RHICommandBuffer& cmd) override;
};

} // namespace grove
```

#### SpritePass

```cpp
// Passes/SpritePass.h
#pragma once
#include "../RenderGraph/RenderPass.h"

namespace grove {

class SpritePass : public RenderPass {
public:
    const char* getName() const override { return "Sprites"; }
    uint32_t getSortOrder() const override { return 100; }
    std::vector<const char*> getDependencies() const override { return {"Clear"}; }

    void setup(rhi::IRHIDevice& device) override;
    void shutdown(rhi::IRHIDevice& device) override;
    void execute(const FramePacket& frame, rhi::RHICommandBuffer& cmd) override;

private:
    rhi::ShaderHandle shader_;
    rhi::BufferHandle quadVB_;
    rhi::BufferHandle quadIB_;
    rhi::BufferHandle instanceBuffer_;
    rhi::UniformHandle textureSampler_;

    static constexpr uint32_t MAX_SPRITES_PER_BATCH = 10000;
};

} // namespace grove
```

#### TilemapPass, TextPass, ParticlePass, DebugPass

Structure similaire avec leurs spécificités.

---

## Phase 4 : Scene Collection (via IIO)

### 4.1 Scene Collector

Collecte les messages IIO pendant `process()` et construit le FramePacket.

```cpp
// Scene/SceneCollector.h
#pragma once
#include "../Frame/FramePacket.h"
#include "grove/IIO.h"
#include <vector>

namespace grove {

class SceneCollector {
public:
    SceneCollector() = default;

    // Configure les subscriptions IIO (appelé dans setConfiguration)
    void setup(IIO* io);

    // Collecte tous les messages IIO en début de frame (appelé dans process)
    // Pull-based: le module contrôle quand il lit les messages
    void collect(IIO* io, float deltaTime);

    // Génère le FramePacket immuable pour les passes de rendu
    FramePacket finalize(FrameAllocator& allocator);

    // Reset pour prochaine frame
    void clear();

private:
    std::vector<SpriteInstance> sprites_;
    std::vector<TilemapChunk> tilemaps_;
    std::vector<TextCommand> texts_;
    std::vector<ParticleInstance> particles_;
    ViewInfo mainView_;
    uint32_t clearColor_ = 0x303030FF;
    uint64_t frameNumber_ = 0;

    // Parse les messages IIO en structures internes
    void parseSprite(const IDataNode& data);
    void parseSpriteBatch(const IDataNode& data);
    void parseTilemap(const IDataNode& data);
    void parseText(const IDataNode& data);
    void parseParticle(const IDataNode& data);
    void parseCamera(const IDataNode& data);
    void parseClear(const IDataNode& data);
};

} // namespace grove
```

### 4.2 Topics IIO et Format Messages

Le renderer subscribe à `render:*` dans `setConfiguration()`.

| Topic | Format IDataNode | Description |
|-------|------------------|-------------|
| `render:sprite` | `{x, y, scaleX, scaleY, rotation, u0, v0, u1, v1, color, textureId, layer}` | Un sprite |
| `render:sprite:batch` | `{sprites: [{...}, {...}]}` | Batch de sprites |
| `render:tilemap` | `{x, y, width, height, tileW, tileH, textureId, tiles: [...]}` | Chunk tilemap |
| `render:text` | `{x, y, text, fontId, fontSize, color, layer}` | Texte à afficher |
| `render:particle` | `{x, y, vx, vy, size, life, color, textureId}` | Particule |
| `render:camera` | `{x, y, zoom, viewportX, viewportY, viewportW, viewportH}` | Caméra principale |
| `render:clear` | `{color}` | Clear color (RGBA uint32) |
| `render:debug:line` | `{x1, y1, x2, y2, color}` | Ligne debug |
| `render:debug:rect` | `{x, y, w, h, color, filled}` | Rectangle debug |

### 4.3 Exemple d'usage (autre module publiant vers renderer)

```cpp
// Dans un GameModule qui veut dessiner un sprite
void GameModule::process(const IDataNode& input) {
    // Créer le message sprite
    auto sprite = std::make_unique<JsonDataNode>("sprite");
    sprite->setDouble("x", m_playerX);
    sprite->setDouble("y", m_playerY);
    sprite->setDouble("scaleX", 1.0);
    sprite->setDouble("scaleY", 1.0);
    sprite->setDouble("rotation", m_playerRotation);
    sprite->setDouble("u0", 0.0);
    sprite->setDouble("v0", 0.0);
    sprite->setDouble("u1", 1.0);
    sprite->setDouble("v1", 1.0);
    sprite->setInt("color", 0xFFFFFFFF);
    sprite->setInt("textureId", m_playerTextureId);
    sprite->setInt("layer", 10);

    // Publier vers le renderer
    m_io->publish("render:sprite", std::move(sprite));
}
```

---

## Phase 5 : Resource Management

### 5.1 Resource Cache

```cpp
// Resources/ResourceCache.h
#pragma once
#include "../RHI/RHITypes.h"
#include <unordered_map>
#include <string>
#include <mutex>

namespace grove {

class ResourceCache {
public:
    // Thread-safe resource access
    rhi::TextureHandle getTexture(const std::string& path);
    rhi::ShaderHandle getShader(const std::string& name);

    // Chargement (appelé depuis main thread)
    void loadTexture(rhi::IRHIDevice& device, const std::string& path);
    void loadShader(rhi::IRHIDevice& device, const std::string& name,
                    const void* vsData, uint32_t vsSize,
                    const void* fsData, uint32_t fsSize);

    // Cleanup
    void clear(rhi::IRHIDevice& device);

private:
    std::unordered_map<std::string, rhi::TextureHandle> textures_;
    std::unordered_map<std::string, rhi::ShaderHandle> shaders_;
    mutable std::shared_mutex mutex_;
};

} // namespace grove
```

---

## Phase 6 : Module Principal

### 6.1 BgfxRendererModule

```cpp
// BgfxRendererModule.cpp
#include "grove/IModule.h"
#include "grove/IDataNode.h"
#include "grove/IIO.h"
#include "grove/JsonDataNode.h"
#include "RHI/RHIDevice.h"
#include "Frame/FrameAllocator.h"
#include "Frame/FramePacket.h"
#include "RenderGraph/RenderGraph.h"
#include "Scene/SceneCollector.h"
#include "Resources/ResourceCache.h"
#include <spdlog/spdlog.h>

namespace grove {

class BgfxRendererModule : public IModule {
public:
    // ========================================
    // IModule Interface
    // ========================================

    void setConfiguration(const IDataNode& config, IIO* io, ITaskScheduler* scheduler) override {
        m_io = io;
        m_logger = spdlog::get("BgfxRenderer");
        if (!m_logger) m_logger = spdlog::stdout_color_mt("BgfxRenderer");

        // Lire config statique via IDataNode
        m_width = static_cast<uint16_t>(config.getInt("windowWidth", 1280));
        m_height = static_cast<uint16_t>(config.getInt("windowHeight", 720));
        m_backend = config.getString("backend", "opengl");
        m_shaderPath = config.getString("shaderPath", "./shaders");
        m_vsync = config.getBool("vsync", true);
        m_maxSprites = config.getInt("maxSpritesPerBatch", 10000);
        size_t allocatorSize = config.getInt("frameAllocatorSizeMB", 16) * 1024 * 1024;

        // Window handle (passé via config ou 0 si WindowModule séparé)
        void* windowHandle = reinterpret_cast<void*>(
            static_cast<uintptr_t>(config.getInt("nativeWindowHandle", 0))
        );

        // Initialiser les sous-systèmes
        m_frameAllocator = std::make_unique<FrameAllocator>(allocatorSize);
        m_device = rhi::IRHIDevice::create();
        m_device->init(windowHandle, m_width, m_height);

        m_renderGraph = std::make_unique<RenderGraph>();
        m_renderGraph->setup(*m_device);

        m_sceneCollector = std::make_unique<SceneCollector>();
        m_sceneCollector->setup(io);  // Subscribe à render:*

        m_resourceCache = std::make_unique<ResourceCache>();

        m_logger->info("BgfxRenderer configured: {}x{} backend={}", m_width, m_height, m_backend);
    }

    void process(const IDataNode& input) override {
        // Lire deltaTime depuis input (fourni par le ModuleSystem)
        float deltaTime = static_cast<float>(input.getDouble("deltaTime", 0.016));

        // 1. Collecter les messages IIO (pull-based)
        m_sceneCollector->collect(m_io, deltaTime);

        // 2. Construire le FramePacket immuable
        m_frameAllocator->reset();
        FramePacket frame = m_sceneCollector->finalize(*m_frameAllocator);

        // 3. Exécuter le render graph
        m_renderGraph->execute(frame, *m_device);

        // 4. Present
        m_device->frame();

        // 5. Cleanup pour prochaine frame
        m_sceneCollector->clear();
        m_frameCount++;
    }

    void shutdown() override {
        m_logger->info("BgfxRenderer shutting down, {} frames rendered", m_frameCount);
        m_renderGraph->shutdown(*m_device);
        m_resourceCache->clear(*m_device);
        m_device->shutdown();
    }

    std::unique_ptr<IDataNode> getState() override {
        // État minimal pour hot-reload (le renderer est stateless côté gameplay)
        auto state = std::make_unique<JsonDataNode>("state");
        state->setInt("frameCount", static_cast<int>(m_frameCount));
        // Les resources GPU sont recréées au reload
        return state;
    }

    void setState(const IDataNode& state) override {
        m_frameCount = static_cast<uint64_t>(state.getInt("frameCount", 0));
        m_logger->info("State restored: frameCount={}", m_frameCount);
    }

    const IDataNode& getConfiguration() override {
        if (!m_configCache) {
            m_configCache = std::make_unique<JsonDataNode>("config");
            m_configCache->setInt("windowWidth", m_width);
            m_configCache->setInt("windowHeight", m_height);
            m_configCache->setString("backend", m_backend);
        }
        return *m_configCache;
    }

    std::unique_ptr<IDataNode> getHealthStatus() override {
        auto health = std::make_unique<JsonDataNode>("health");
        health->setString("status", "running");
        health->setInt("frameCount", static_cast<int>(m_frameCount));
        health->setInt("allocatorUsedBytes", static_cast<int>(m_frameAllocator->getUsed()));
        return health;
    }

    std::string getType() const override { return "bgfx_renderer"; }
    bool isIdle() const override { return true; }  // Toujours safe pour hot-reload

private:
    // Logger
    std::shared_ptr<spdlog::logger> m_logger;

    // Core systems
    std::unique_ptr<rhi::IRHIDevice> m_device;
    std::unique_ptr<FrameAllocator> m_frameAllocator;
    std::unique_ptr<RenderGraph> m_renderGraph;
    std::unique_ptr<SceneCollector> m_sceneCollector;
    std::unique_ptr<ResourceCache> m_resourceCache;

    // IIO (non-owning)
    IIO* m_io = nullptr;

    // Config (depuis IDataNode)
    uint16_t m_width = 1280;
    uint16_t m_height = 720;
    std::string m_backend = "opengl";
    std::string m_shaderPath = "./shaders";
    bool m_vsync = true;
    int m_maxSprites = 10000;
    std::unique_ptr<IDataNode> m_configCache;

    // Stats
    uint64_t m_frameCount = 0;
};

} // namespace grove

// ========================================
// C Export (required for dlopen)
// ========================================
extern "C" {
    grove::IModule* createModule() {
        return new grove::BgfxRendererModule();
    }
    void destroyModule(grove::IModule* module) {
        delete module;
    }
}
```

### 6.2 Exemple de configuration JSON

```json
{
    "windowWidth": 1920,
    "windowHeight": 1080,
    "backend": "vulkan",
    "shaderPath": "./assets/shaders",
    "vsync": true,
    "maxSpritesPerBatch": 20000,
    "frameAllocatorSizeMB": 32,
    "nativeWindowHandle": 0
}
```

---

## Phases d'implémentation

### Phase 1 : Squelette (1-2 jours)
- [ ] Structure fichiers/dossiers
- [ ] CMakeLists.txt avec fetch bgfx
- [ ] RHITypes.h complet
- [ ] RHIDevice interface + BgfxDevice stub
- [ ] FrameAllocator
- [ ] Module qui compile et se charge

### Phase 2 : RHI bgfx (2-3 jours)
- [ ] BgfxDevice::init/shutdown/frame
- [ ] Création textures/buffers/shaders
- [ ] RHICommandBuffer execution
- [ ] Test: triangle qui s'affiche

### Phase 3 : Task Graph (1 jour)
- [ ] TaskGraph construction
- [ ] Compilation (tri topologique)
- [ ] SingleThreadScheduler
- [ ] Tests unitaires

### Phase 4 : Render Graph + ClearPass (1 jour)
- [ ] RenderGraph
- [ ] ClearPass fonctionnel
- [ ] Intégration TaskGraph
- [ ] Test: clear color qui change

### Phase 5 : SpritePass (2-3 jours)
- [ ] Vertex layout sprite
- [ ] Shader sprite (sprite.sc)
- [ ] Batching par texture
- [ ] Instance buffer
- [ ] Test: sprites qui s'affichent

### Phase 6 : Scene Collection + IIO (1-2 jours)
- [ ] SceneCollector
- [ ] Topics IIO
- [ ] FramePacket generation
- [ ] Test: sprites via messages IIO

### Phase 7 : Passes additionnelles (3-4 jours)
- [ ] TilemapPass
- [ ] TextPass (+ font loading)
- [ ] DebugPass

### Phase 8 : Polish (2 jours)
- [ ] Resource hot-reload
- [ ] State save/restore pour module hot-reload
- [ ] Stats/profiling
- [ ] Documentation

---

## Règles strictes

| Règle | Raison |
|-------|--------|
| **Zéro `bgfx::` hors de `BgfxDevice.cpp`** | Abstraction propre, changement backend possible |
| **FramePacket const dans les passes** | Thread-safety, pas de mutation pendant render |
| **CommandBuffer par thread** | Pas de lock pendant l'encoding |
| **Handles, jamais de pointeurs raw** | Indirection = safe pour relocation |
| **Allocation via FrameAllocator** | Lock-free, reset gratuit chaque frame |
| **Dépendances passes explicites** | TaskGraph peut paralléliser |
| **State serializable** | Hot-reload du module |

---

## Dépendances externes

```cmake
# CMakeLists.txt
FetchContent_Declare(
    bgfx
    GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
    GIT_TAG v1.127.8710-464
)
FetchContent_MakeAvailable(bgfx)

# Pour le windowing (optionnel, peut être externe)
find_package(SDL2 REQUIRED)
```

---

## Décisions d'architecture

| Question | Décision | Raison |
|----------|----------|--------|
| **Windowing** | Module séparé `WindowModule` | Découplage propre, le renderer reçoit le handle via config |
| **Config vs Messages** | Config = `IDataNode`, Runtime = `IIO` | Aligné avec architecture GroveEngine |
| **Window handle** | Via `config.getInt("nativeWindowHandle")` | Fourni par WindowModule ou application |
| **Resize events** | Via IIO `window:resize` | Event dynamique = message |

## Questions ouvertes

1. **Shaders** : Pré-compilés ou compilation runtime via shaderc ?
2. **Font rendering** : stb_truetype ou bibliothèque dédiée ?
3. **Texture formats** : Support DDS/KTX ou juste PNG/JPG via stb_image ?
4. **WindowModule** : Qui le développe ? Dépendance SDL2 ou autre ?

---

## Estimation totale

| Phase | Durée estimée |
|-------|---------------|
| Phase 1-2 | 3-5 jours |
| Phase 3-4 | 2 jours |
| Phase 5-6 | 3-5 jours |
| Phase 7-8 | 5-6 jours |
| **Total** | **~2-3 semaines** |

Pour un premier sprite à l'écran : ~1 semaine.
