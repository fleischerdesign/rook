# Rook — Multi-Modal AI Agent (GUI + TUI + Web + Server)

Ein lokaler AI-Agent mit Wakeword-Sprachsteuerung + selbst hostbarem Server.
Gebaut mit C++20, hexagonaler Architektur (Ports & Adapters), gRPC, CRDT-Sync.

**App-ID:** `io.github.fleischerdesign.Rook`
**Komponenten:** `rook-gui` (GTK4), `rook-tui` (FTXUI), `rook-web` (React), `rookd` (Headless Daemon)
**Core:** `librook-core` (hexagonale Domain-Bibliothek)

---

## Alle Entscheidungen

| Frage | Entscheidung |
|---|---|
| **Name** | Rook |
| **App-ID** | io.github.fleischerdesign.Rook |
| **Komponenten** | `librook-core`, `rook-gui` (GTK4), `rook-tui` (FTXUI), `rook-web` (React), `rookd` (Server-Daemon) |
| **Architektur** | Hexagonal (Ports & Adapters) + Event Bus + Dependency Injection |
| **Client-Server** | Hybrid (Standalone + optionaler rookd-Server) |
| **Server-Protokoll** | gRPC + Protobuf + gRPC-Web + gRPC-Gateway (REST) |
| **Client-Sync** | CRDT-basiert (YATA für Chats, AWSet für Extensions, LWW+HLC für Settings) |
| **Inter-Client** | Task-Delegation via rookd oder P2P (mDNS/Avahi) |
| **Security** | Object-Capability Model (ocap) + Sandboxing (bubblewrap) |
| **Observability** | OpenTelemetry (Traces + Metrics + Logs) via OTLP/gRPC |
| **Wakeword** | "Hey Rook" (custom .ppn) |
| **Porcupine Key** | User-Eingabe in Settings |
| **TTS-Stimme** | Wählbar in Settings (piper voices) |
| **STT-Modell** | small (500MB, whisper.cpp) |
| **Distribution** | Flatpak + Nix + Source |
| **Text-Mode** | Beides parallel (Voice + Text-Chat) |
| **Chat-Historie** | Persistent (Sidebar, laden/speichern) + Server-seitig sync |
| **Tool-Permissions** | Object-Capability (Capability-Grant pro MCP-Server) + Whitelist als UI-Layer |
| **Startup-Verhalten** | Tray-Icon + Fenster (GUI), Daemon (Server) |
| **PTT-Hotkey** | Nein, nur Wakeword + Text-Eingabe |
| **Persönlichkeit** | Komplett konfigurierbarer System-Prompt |
| **UI-Sprache** | Deutsch + Englisch (gettext i18n) |
| **Modell-Auswahl** | Globales Modell (Settings, nicht pro Chat) |
| **Kontext-Länge** | Konfigurierbar (Default + User-override) |
| **First-Run** | Wizard beim ersten Start |
| **LLM Backends** | Lokal (Ollama) + Cloud (OpenAI, Anthropic) |
| **MCP** | MCP-kompatibel (STDIO + SSE) |
| **Wakeword-Engine** | Porcupine C SDK + miniaudio |
| **STT** | whisper.cpp (Subprozess) |
| **TTS** | piper (Subprozess) |
| **Lizenz** | MIT |
| **Build-System** | Meson + flake.nix |

---

## Architektur-Entscheidungen (ADR)

### 1. Thread-Kommunikation: Message Queue + Dispatcher

Worker-Threads dürfen GTK nicht berühren. Architektur:

```
┌──────────────┐  push(msg)   ┌─────────────────┐  drain()   ┌────────────┐
│ Worker Thread│─────────────►│ std::mutex       │◄──────────│ Main Thread │
│ (Wakeword,   │              │ + std::deque<T>  │           │ (GTK Event  │
│  LLM, STT)   │              │ (Thread-Safe)    │           │  Loop)      │
└──────────────┘              └────────┬─────────┘           └─────────────┘
                                       │
                              Glib::Dispatcher
                              (nur "Klingelknopf",
                               keine Payload)
```

- `ThreadSafeQueue<T>`: Template mit `push()`, `try_pop()`, `drain()`, `mutex + deque`
- Worker pusht Message → `Dispatcher.emit()` → Main-Thread drained Queue
- Kein Boost, kein Lock-Free-Overengineering
- Message-Typenum: `UserInput`, `StreamChunk`, `AgentResponse`, `AudioStateChange`, `ErrorInfo`

### 2. SSE-Streaming → GTK UI: Chunk-Queue + Timer-Poll

```
┌──────────────────┐                ┌─────────────────┐
│ libcurl Thread   │  push(chunk)   │ ChunkQueue       │
│ WRITEFUNCTION    │───────────────►│ (ThreadSafeQueue)│
└──────────────────┘                └────────┬─────────┘
                                            │
                              signal_timeout(33ms) poll
                                            │
                                            ▼
                                     ┌────────────┐
                                     │ Main Thread │
                                     │ Gtk::Label  │
                                     │ .set_markup()│
                                     └────────────┘
```

- `Glib::signal_timeout(33ms)` pollt ChunkQueue (~30fps)
- `Gtk::Label::set_markup()` aktualisiert gerenderten Text inkrementell
- Kein `g_idle_add()`-Spam pro Chunk (würde bei 100+ Chunks/sec UI einfrieren)
- Streaming-Abbruch: `CURLOPT_XFERINFOFUNCTION` prüft Atomic-Flag

### 3. Audio ↔ Conversation: sigc::signal (Observer-Pattern)

```cpp
// AudioPipeline — weiß nichts vom ConversationManager
sigc::signal<void(std::string)> on_speech_recognized;  // User hat gesprochen
sigc::signal<void(AudioState)> on_state_changed;        // IDLE→LISTENING→...

// ConversationManager — weiß nichts vom AudioPipeline
sigc::signal<void(std::string)> on_response_ready;      // LLM-Antwort zum Sprechen
sigc::signal<void()>           on_processing_started;   // "Denk-Pause" Indikator
```

- Lose Kopplung: beide Komponenten sind einzeln testbar
- `Application::on_activate()` verdrahtet die Signale
- Typen: `AudioState = { Idle, Muted, WakeDetected, Listening, Processing, Speaking, Error }`

### 4. MCP-Client: Fokus auf Tools, Transport abstrakt

```
Jetzt (Phase 3):
  ✅ initialize, tools/list, tools/call, notifications
  ✅ StdioTransport, SseTransport (später)

Später (Phase 6+):
  ○ resources/list, resources/read
  ○ prompts/list, prompts/get
  ○ sampling/createMessage
```

Transport-Layer ist abstrakt von Tag 1 (`MCPTransport` Interface), sodass Resources/Prompts/Sampling reine Add-ons sind.

### 5. Fehler-Propagation: std::expected + Error-Bus

```cpp
enum class ErrorDomain { LLM, MCP, Audio, STT, TTS, Config, Network };

struct RookError {
    ErrorDomain domain;
    std::string code;        // z.B. "openai:rate_limited", "whisper:timeout"
    std::string message;     // Human-readable, i18n-ready
    std::string detail;      // Technischer Stacktrace/Debug-Info
    bool recoverable;        // true = User kann weitermachen
};

// Worker-Threads returnen std::expected<T, RookError>
auto result = llm_backend->chat(history);
if (!result) {
    error_queue.push(result.error());  // → Main-Thread → Inline-Banner
}
```

- Kein Global-State, keine Exceptions über Thread-Grenzen
- Fehler landen per Message-Queue im Main-Thread
- UI: Inline-Banner im Chat ("Verbindung zu OpenAI fehlgeschlagen — 3s Retry...")
- Logging: alle Errors via spdlog (strukturiert, JSON-formatierbar)

### 6. Dependency Injection: Constructor Injection + Composition Root

```cpp
// Keine Singletons, kein Service Locator, kein dynamic_cast.
// Alles explizit im Composition Root verdrahtet.

class RookApplication : public Gtk::Application {
    // All Components owned here
    std::unique_ptr<rook::Config>             m_config;
    std::unique_ptr<rook::audio::DeviceManager> m_audio_device;
    std::unique_ptr<rook::audio::WakewordEngine> m_wakeword;
    std::unique_ptr<rook::audio::SpeechToText>  m_stt;
    std::unique_ptr<rook::audio::TextToSpeech>  m_tts;
    std::unique_ptr<rook::audio::Pipeline>      m_audio_pipeline;
    std::unique_ptr<rook::mcp::Client>          m_mcp_client;
    std::unique_ptr<rook::mcp::PermissionMgr>   m_permissions;
    std::unique_ptr<rook::llm::Backend>          m_llm_backend;
    std::unique_ptr<rook::ConversationManager>  m_conversation;

    void on_activate() override {
        // 1. Allocate
        m_config = std::make_unique<rook::Config>();
        m_audio_device = std::make_unique<rook::audio::DeviceManager>();

        // 2. Inject dependencies via constructor
        m_llm_backend = rook::llm::create_backend(*m_config);
        m_mcp_client = std::make_unique<rook::mcp::Client>();
        m_permissions = std::make_unique<rook::mcp::PermissionMgr>(*m_config);
        m_conversation = std::make_unique<rook::ConversationManager>(
            m_llm_backend.get(), m_mcp_client.get(), m_permissions.get()
        );

        m_wakeword = std::make_unique<rook::audio::WakewordEngine>(
            *m_config, m_audio_device.get()
        );
        m_stt = std::make_unique<rook::audio::SpeechToText>(*m_config);
        m_tts = std::make_unique<rook::audio::TextToSpeech>(*m_config);
        m_audio_pipeline = std::make_unique<rook::audio::Pipeline>(
            m_wakeword.get(), m_stt.get(), m_tts.get(), m_audio_device.get()
        );

        // 3. Wire signals
        m_audio_pipeline->on_speech_recognized.connect(
            sigc::mem_fun(*m_conversation, &ConversationManager::handle_input)
        );
        m_conversation->on_response_ready.connect(
            sigc::mem_fun(*m_audio_pipeline, &audio::Pipeline::speak)
        );
    }
};
```

- Jede Komponente kennt nur abstrakte Interfaces ihrer Dependencies
- Testbar: Mock-Implementierungen in Constructor injecten
- `Config`-Objekt hält GSettings + JSON (kein verteiltes Config-Lesen)

---

## Technologie-Stack

| Komponente    | Library                       | Lizenz            | Begründung                                      |
|---------------|-------------------------------|-------------------|-------------------------------------------------|
| Wakeword      | Porcupine C SDK + miniaudio   | Apache 2.0 / PD   | C-API, native Linux, .ppn Keywords konfigurierbar |
| STT           | whisper.cpp (Subprozess)      | MIT               | Lokal, GGML-Modelle, einfach via pipe einbindbar |
| TTS           | piper (Subprozess)            | MIT / GPL (fork)  | C++, 100+ Stimmen, schnelle Inferenz            |
| LLM lokal     | Ollama (HTTP API)             | MIT               | Bewährt, OpenAI-kompatibel, Model-Management    |
| LLM Cloud     | libcurl + nlohmann/json       | MIT               | OpenAI, Anthropic API                           |
| Audio I/O     | miniaudio (vendored)          | PD / MIT          | Single-Header, cross-platform, kein Linken      |
| GUI           | gtkmm-4.0 + libadwaita        | LGPL              | Native GNOME, C++ Bindings, moderne Widgets     |
| Tray-Icon     | libayatana-appindicator       | LGPL              | System-Tray auf Linux (AppIndicator3)           |
| i18n          | gettext                       | GPL               | GNOME-Standard, meson-Integration               |
| Build         | Meson + flake.nix             | Apache 2.0        | GNOME-Ökosystem-Standard, NixOS-reproduzierbar  |
| Settings      | GSettings + JSON              | —                 | GNOME-native für UI, JSON für Advanced          |
| Secrets       | libsecret                     | LGPL              | API-Keys sicher im System-Keyring               |
| Logging       | spdlog                        | MIT               | Header-only, async, formatiert                  |

---

## Extensibility — Plugin & Extension Architecture

### Design Principles

1. **Wenige, aber starke Mechanismen** — nicht 10 verschiedene Extension-Typen mit
   überlappenden Fähigkeiten, sondern 3 klar getrennte Tiers
2. **Crash-Isolation nach Vertrauensstufe** — untrusted Code nie in-process
3. **Stabile Interfaces mit dokumentierter Semver-Garantie** — Plugins brechen nicht
   bei Minor-Updates
4. **Jede Extension ist für sich konfigurierbar und versionierbar**

### Die 3 Extension-Tiers

```
┌─────────────────────────────────────────────────────────────────┐
│ Tier 1               Tier 2                 Tier 3               │
│ Native .so Plugins   MCP Server             Skills (YAML)        │
│ (trusted, in-proc)   (untrusted, out-proc)  (declarativ, sicher) │
│                                                                  │
│ Performance-kritisch Werkzeug-Integration  Prompt-Templates      │
│ Volle Core-API       Sprache-agnostisch     Kein Code nötig      │
│ C ABI Stabilität     JSON-RPC (MCP Spec)    YAML + MCP-Chaining  │
│ Crash = App-Crash    Crash = isoliert       Kein Crash-Risiko    │
│                      Sandbox per Subprozess                      │
│                                                                  │
│ Beispiele:            Beispiele:             Beispiele:            │
│ • LLM Backends        • Browser-Automation   • Code Review         │
│ • Audio Engines       • Datenbank-Zugriff    • Daily Summary       │
│ • Performance-Hooks   • File-System Ops      • File Organizer      │
│ • System-Integration  • Web-APIs             • Meeting Prep        │
└─────────────────────────────────────────────────────────────────┘
```

---

### Tier 1: Native .so Plugins (In-Process, Trusted)

Für leistungskritische Erweiterungen, die tief in Rooks Core-Loop integriert sein müssen.
Laufen im selben Prozess — ein Crash im Plugin reißt Rook mit.
Daher nur für **vertrauenswürdige** Plugins (User-Bestätigung bei Installation).

**Plugin-Kategorien (Tier 1):**

| Kategorie     | Interface              | Einsatzzweck                                    |
|---------------|------------------------|-------------------------------------------------|
| `llm`         | `rook::llm::Backend`   | Neue LLM-Provider (OpenAI, Anthropic, Ollama)   |
| `wakeword`    | C ABI struct           | Wakeword-Engines (Porcupine, openWakeWord)      |
| `stt`         | `rook::audio::STTEngine`| Speech-to-Text (whisper.cpp, cloud STT)         |
| `tts`         | `rook::audio::TTSEngine`| Text-to-Speech (piper, Coqui, ElevenLabs)       |
| `hook`        | `rook::hook::Hook`     | Agent-Loop-Einschubpunkte                       |

#### Plugin-ABI — C ABI für Compiler-übergreifende Stabilität

Das C++-ABI ist nicht stabil (vtable-Layout, Name-Mangling — jede Compiler-Version anders).
Alle .so-Plugins kommunizieren daher über ein **reines C-Interface**.
C++-Wrapper auf beiden Seiten übersetzen zwischen C-Structs und C++-Klassen.

```c
// rook_plugin.h — Stabiler C-ABI-Header (wird sich in Minor-Versionen NICHT ändern)

#define ROOK_PLUGIN_API_VERSION_MAJOR 1
#define ROOK_PLUGIN_API_VERSION_MINOR 0
#define ROOK_PLUGIN_API_VERSION_PATCH 0

// === Plugin Metadata ===

typedef struct {
    const char* id;           // Eindeutig: "my_llm_backend"
    const char* name;         // Anzeigename: "My LLM Provider"
    const char* description;  // Kurzbeschreibung
    const char* author;
    const char* version;      // Plugin-eigene Version: "1.2.0"
    const char* category;     // "llm", "wakeword", "stt", "tts", "hook"
    int api_version_major;    // Gegen diese API-Version wurde kompiliert
    int api_version_minor;
    int api_version_patch;
} RookPluginInfo;

// === Core API (injiziert in jedes Plugin) ===

typedef struct RookCoreAPI RookCoreAPI;
struct RookCoreAPI {
    // Logging
    void (*log_trace)(const char* msg);
    void (*log_debug)(const char* msg);
    void (*log_info) (const char* msg);
    void (*log_warn) (const char* msg);
    void (*log_error)(const char* msg);

    // Plugin-eigene Config als JSON-String abrufen
    const char* (*get_config_json)(void);

    // Message-Queue: Nachrichten an den Main-Thread senden
    void (*post_message)(const char* json_message);

    // Subprozess starten (für Plugins die selbst Subprozesse brauchen)
    int  (*spawn_process)(const char* command, const char* const* argv,
                          int* stdin_fd, int* stdout_fd, int* stderr_fd);
    void (*kill_process)(int pid);

    // Dateisystem (nur in Plugin-eigenem Datenverzeichnis)
    const char* (*get_data_dir)(void);  // ~/.local/share/rook/plugins/<plugin-id>/
    int (*read_file)(const char* path, char* buf, int max_len);
    int (*write_file)(const char* path, const char* data, int len);

    // GTK (nur für Plugins die UI brauchen)
    void* gtk_application;  // GtkApplication*, null für Non-UI-Plugins
};
```

#### Versionierung — Semver mit dokumentierter API-Stabilitätsgarantie

```
┌───────────────────────────────────────────────────────┐
│  Rook API Version: MAJOR.MINOR.PATCH                  │
│                                                       │
│  MAJOR bump → BREAKING CHANGES                        │
│    • C-Struct-Layout geändert (Felder hinzu/entfernt) │
│    • Exportierte Symbol-Signatur geändert             │
│    • Semantik eines existierenden Felds geändert      │
│    → Plugin MUSS neu kompiliert werden                │
│                                                       │
│  MINOR bump → ADDITIVE CHANGES                        │
│    • Neue Felder ANS ENDE von Structs angehängt       │
│    • Neue Funktionen im RookCoreAPI hinzugefügt       │
│    • Alte Felder/Funktionen unverändert               │
│    → Plugin läuft ohne Neukompilierung weiter         │
│                                                       │
│  PATCH bump → FIXES (keine API-Änderung)              │
│    → Plugin immer kompatibel                          │
└───────────────────────────────────────────────────────┘
```

Plugin-Lader prüft beim dlopen:

```cpp
// plugin_loader.cpp — Version-Check bei jedem Plugin-Load

enum class VersionMatch {
    Compatible,           // api.major == rook.major && api.minor <= rook.minor
    NeedsRecompile,       // api.major != rook.major
    TooNew,               // api.minor > rook.minor (Plugin will neuere API)
};

VersionMatch check_version(const RookPluginInfo& plugin, int rook_major, int rook_minor) {
    if (plugin.api_version_major != rook_major)
        return VersionMatch::NeedsRecompile;
    if (plugin.api_version_minor > rook_minor)
        return VersionMatch::TooNew;
    return VersionMatch::Compatible;
}

// Inkompatible Plugins werden nicht geladen — User bekommt Notification
// mit Link zur aktualisierten Plugin-Version.
```

#### Plugin-Konfiguration — Per-Plugin Settings in config.json

```json
// ~/.config/rook/config.json
{
  "plugins": {
    "my_llm_backend": {
      "enabled": true,
      "config": {
        "api_base": "https://my-custom-llm.example.com/v1",
        "model": "my-custom-model",
        "max_tokens": 4096
      }
    },
    "porcupine_wakeword": {
      "enabled": true,
      "config": {
        "access_key": "${ENV:PORCUPINE_ACCESS_KEY}",
        "sensitivity": 0.6,
        "keyword_paths": [
          "~/.config/rook/wakewords/hey_rook_de.ppn"
        ]
      }
    }
  }
}
```

- `${ENV:VAR}` → aus Environment-Variable lesen (kein Klartext in Config)
- Plugin bekommt seine Config via `core_api->get_config_json()` als JSON-String
- User konfiguriert Plugin im Settings-Dialog (generiert aus JSON-Schema, das jedes Plugin optional bereitstellt)

#### Plugin-Lebenszyklus

```
  discover    validate   load       init            run        unload
  ──────────► ────────► ────────► ───────────► ────────────► ────────►
  scan_dir()  version   dlopen()  create()     plugin aktiv  dlclose()
              check     dlsym()   (API injiz.)              (Hook:
                                                              on_shutdown)
```

- **discover**: `PluginLoader::scan_directory("~/.config/rook/plugins/{category}/")`
- **validate**: Version-Check gegen aktuelle API-Version
- **load**: `dlopen()`, `dlsym("rook_plugin_get_info")`, `dlsym("rook_plugin_create")`
- **init**: `create(&core_api)` → registriert sich in der richtigen Kategorie-Registry
- **unload**: Bei App-Shutdown: `dlclose()` (Plugin-Cleanup via Hook `OnSystemShutdown`)

---

### Hook-System — Einschubpunkte im Agent-Loop

Hooks sind .so-Plugins der Kategorie `hook`, die an definierten Punkten in den
Agent-Event-Loop eingreifen können. Sie bekommen den Event-Kontext, können
Messages/Responses mutieren und haben eine Priorität für die Ausführungsreihenfolge.

```
┌────────────────────────── Agent Event Loop ──────────────────────────┐
│                                                                       │
│  1. UserInputReceived (Text oder STT)                                 │
│     │                                                                 │
│     ├─ PreUserInput hooks (messages vorfiltern, Kontext anreichern)   │
│     │                                                                 │
│     ▼                                                                 │
│  2. ConversationManager.send_to_llm()                                 │
│     │                                                                 │
│     ├─ PreLLM hooks (System-Prompt modifizieren, RAG-Kontext laden)   │
│     │                                                                 │
│     ▼                                                                 │
│  3. LLMStreamChunk events (streaming)                                 │
│     │                                                                 │
│     ├─ PostLLM hooks (response mutieren, filtern, formatieren)        │
│     │                                                                 │
│     ▼                                                                 │
│  4. ToolCallRequested? → ToolPort → ToolCallCompleted                 │
│     │                                                                 │
│     ├─ PreToolExecution hooks (Argumente validieren, loggen)          │
│     ├─ PostToolExecution hooks (Ergebnis transformieren)              │
│     │                                                                 │
│     ▼                                                                 │
│  5. ConversationCompleted (final response)                            │
│     │                                                                 │
│     ├─ PreResponse hooks (Antwort formatieren, Links einbetten)       │
│     │                                                                 │
│     ▼                                                                 │
│  6. UserOutputPort.display(text) + AudioPort.speak(text) (falls voice) │
│     │                                                                 │
│     ├─ PreTTS hooks (Text für Sprachausgabe optimieren: SSML,         │
│     │                Abkürzungen ausschreiben, Emojis entfernen)       │
│     │                                                                 │
│     ▼                                                                 │
│  7. IDLE (warten auf nächsten Input)                                  │
│                                                                       │
│  Zusätzliche Einschubpunkte:                                          │
│     ├─ OnSystemStartup (einmalig beim App-Start)                      │
│     └─ OnSystemShutdown (einmalig vor App-Ende)                       │
│                                                                       │
└───────────────────────────────────────────────────────────────────────┘
```

```cpp
// include/rook/ports/hook_port.hpp (Port-Interface)
namespace rook::ports {

enum class HookPoint {
    PreUserInput,        // Bevor User-Input an ConversationManager geht
    PreLLM,              // Bevor Messages an LLM gesendet werden (kann Messages mutieren)
    PostLLM,             // Nach LLM-Response (kann Response-Text mutieren)
    PreToolExecution,    // Bevor Tool-Call ausgeführt wird
    PostToolExecution,   // Nach Tool-Call (kann Ergebnis transformieren)
    PreResponse,         // Bevor finale Antwort an UI/Audio geht
    PreTTS,              // Bevor Text an TTS-Engine geht (SSML, Text-Optimierung)
    OnSystemStartup,     // Einmalig beim App-Start
    OnSystemShutdown     // Einmalig vor App-Ende (Cleanup)
};

struct HookContext {
    HookPoint point;
    std::vector<Message>* messages;   // mutable: PreLLM/PostLLM können Messages ändern
    std::string* response;            // mutable: PostLLM/PreResponse/PreTTS können Antwort ändern
    nlohmann::json* tool_args;        // mutable: PreToolExecution kann Args validieren
    nlohmann::json* tool_result;      // mutable: PostToolExecution kann Ergebnis transformieren
    const Config* config;
    void* rook_api;                   // RookCoreAPI (via C ABI für .so-Plugins)
};

class HookPort {
public:
    virtual ~HookPort() = default;
    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual std::vector<HookPoint> trigger_points() const = 0;
    virtual int priority() const { return 0; }  // Niedrigere Zahl = früher ausgeführt
    virtual void execute(HookContext& ctx) = 0;
};

} // namespace rook::ports
```

**Beispiel-Hook: Auto-Format-Responses**
```cpp
class ResponseFormatterHook : public ports::HookPort {
    std::string id() const override { return "response_formatter"; }
    std::string name() const override { return "Response Formatter"; }
    std::vector<HookPoint> trigger_points() const override {
        return {HookPoint::PreResponse};
    }

    void execute(HookContext& ctx) override {
        if (ctx.response) {
            // Stelle sicher dass Code-Blöcke Syntax-Highlighting-Language haben
            *ctx.response = std::regex_replace(*ctx.response,
                std::regex(R"(```\n)"), "```text\n");
        }
    }
};
```

---

### Tier 2: MCP Server (Out-of-Process, Untrusted)

MCP-Server laufen als **isolierte Subprozesse** über stdin/stdout oder HTTP/SSE.
Ein Crash des MCP-Servers betrifft Rook nicht. Perfekt für Drittanbieter-Tools
ohne Vertrauensgarantie.

- **Protokoll:** JSON-RPC 2.0 (MCP Specification)
- **Transporte:** STDIO (Subprozess) oder SSE+HTTP (Remote)
- **Sicherheit:** Subprozess in cgroups/namespace isolierbar (später)
- **Kein Code im Rook-Prozess:** Rust, Python, Node.js — alles erlaubt

Der MCP-Client wurde bereits im [MCP-Client-Abschnitt](#mcp-client--json-rpc-implementation) beschrieben.

#### MCP-Manifest — Erweiterte Server-Deklaration

```json
// ~/.config/rook/config.json
{
  "mcp_servers": [
    {
      "id": "filesystem",
      "name": "Filesystem",
      "description": "Dateisystem-Zugriff für Rook",
      "transport": "stdio",
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"],
      "env": {
        "HOME": "/home/user"
      },
      "enabled": true,
      "trust": "untrusted",
      "auto_restart": true,
      "restart_delay_ms": 3000,
      "max_restarts": 5,
      "has_ui": false
    },
    {
      "id": "excalidraw",
      "name": "Excalidraw",
      "transport": "stdio",
      "command": "npx",
      "args": ["-y", "@excalidraw/mcp-server"],
      "enabled": true,
      "trust": "untrusted",
      "has_ui": true,
      "ui_sandbox": "webview"
    }
  ]
}
```

#### MCP App — Server mit eigener UI

MCP-Server können ein optionales `has_ui: true`-Flag setzen. Rook rendert die vom
MCP-Server bereitgestellte HTML/JS-Ressource in einem isolierten WebView-Widget
(`WebKitGTK` in Sandbox). Kommunikation zwischen WebView und MCP-Server läuft
über den Rook-MCP-Client (nicht direkt → Audit-Log möglich).

```
┌───────────────────────────────────────────┐
│  Rook GTK Window                          │
│  ┌─────────────┐ ┌─────────────────────┐  │
│  │  Chat View  │ │  MCP App WebView    │  │
│  │             │ │  (Sandboxed HTML)   │  │
│  │             │ │                     │  │
│  │  User:      │ │  ┌───────────────┐  │  │
│  │  "Male eine │ │  │ Excalidraw   │  │  │
│  │   Skizze"   │ │  │ Canvas       │  │  │
│  └─────────────┘ │  └───────────────┘  │  │
│                   └─────────────────────┘  │
│                                            │
│  MCP Client ◄─────► MCP App Server          │
│  (JSON-RPC)        (Subprozess)            │
└───────────────────────────────────────────┘
```

---

### Tier 3: Skills — Declarative YAML-Workflows

Skills sind YAML-Dateien ohne ausführbaren Code. Sie definieren Prompt-Templates,
Tool-Zugriff und MCP-Chaining. 100% sicher, kein Sandboxing nötig.

#### Skill-Engine (optional) — Out-of-Process via Subprozess-IPC

Falls ein Skill programmierbare Logik benötigt, läuft diese NICHT in Rooks Prozess,
sondern als isolierter Subprozess mit klar definiertem IPC-Protokoll über stdin/stdout:

```
┌─────────────────────────────────────────────┐
│  Rook Main Process                          │
│                                             │
│  SkillRunner                                │
│  ├─ Skill-YAML parsen                       │
│  ├─ SkillEngine-IPC starten (Subprozess)     │
│  │   stdin  → JSON-RPC Requests             │
│  │   stdout ← JSON-RPC Responses            │
│  ├─ Timeout überwachen (SIGALRM)            │
│  └─ Ergebnis sammeln                        │
│                                             │
│  ──── process boundary ────                 │
│                                             │
│  Skill Engine (Subprozess)                  │
│  z.B. Node.js / Python / WASM-Runtime       │
│  ├─ Skill-Code laden                        │
│  ├─ Anfragen via stdin bekommen             │
│  ├─ Darf NUR:                               │
│  │   • rook.llm.call(prompt)   → extern     │
│  │   • rook.tool.call(name, args) → extern  │
│  │   • rook.files.read(path)    → extern    │
│  └─ Kein direkter FS/Netzwerk-Zugriff       │
└─────────────────────────────────────────────┘
```

```yaml
# Skill mit Engine — Code läuft im Subprozess
name: "Daily Summary"
description: "Erstellt eine tägliche Zusammenfassung"
engine:
  runtime: node        # "node", "python", "wasm"
  runtime_min_version: "20.0.0"
  timeout_ms: 30000    # max Laufzeit
  memory_mb: 128       # max RAM (rlimit)
  network: false       # kein Netzwerk-Zugriff
  filesystem: false    # kein direkter FS-Zugriff
  source: |
    async function run(ctx) {
      const emails = await ctx.rook.tool.call("gmail", "fetch_unread");
      const agenda = await ctx.rook.tool.call("calendar", "today");
      const summary = await ctx.rook.llm.call(
        `Fasse zusammen:\nE-Mails: ${JSON.stringify(emails)}\nAgenda: ${JSON.stringify(agenda)}`
      );
      return { summary, reading_time: "2 min" };
    }
```

---

### UI-Plugins — GTK-sicher via GtkBuilder (kein dlopen)

Gtk::Widget-Instanzen über dlopen-Grenzen zu teilen ist gefährlich:
vtable-Inkompatibilität zwischen Compiler-Versionen, unterschiedliche GTK-Versionen.

Stattdessen: **UI-Plugins liefern `.ui`-XML + C-Handler-Callbacks.**
Wie GNOME Shell Extensions, wie GtkBuilder designed ist.

```xml
<!-- ~/.config/rook/plugins/ui/status-panel/panel.ui -->
<interface>
  <template class="RookPluginPanel">
    <property name="title">System Status</property>
    <child>
      <object class="GtkBox">
        <property name="orientation">vertical</property>
        <child>
          <object class="GtkLabel" id="cpu_label">
            <property name="label">CPU: --</property>
          </object>
        </child>
        <child>
          <object class="GtkLabel" id="mem_label">
            <property name="label">RAM: --</property>
          </object>
        </child>
      </object>
    </child>
  </template>
</interface>
```

```c
// ~/.config/rook/plugins/ui/status-panel/plugin.so
// Exportiert C-Callbacks, kein Gtk::Widget über dlopen!

#include "rook_ui_plugin.h"

void on_panel_created(void* panel, RookCoreAPI* api) {
    // panel = GObject* vom GtkBuilder
    GtkWidget* cpu_label = gtk_builder_get_object(panel, "cpu_label");
    // Update per Timer (Main-Thread-safe)
    g_timeout_add(1000, update_stats, cpu_label);
}

const RookUIPluginDescriptor rook_ui_plugin = {
    .info = {
        .id = "status-panel",
        .name = "System Status",
        .category = "ui",
        // ...
    },
    .ui_resource_path = "panel.ui",     // Pfad zur .ui-Datei
    .on_create = on_panel_created,
    .on_destroy = NULL,
};
```

Rook lädt die `.ui`-Datei via `GtkBuilder`, instanziiert das Widget im eigenen Prozess,
und ruft die C-Callbacks des Plugins für Signal-Verbindungen auf. Keine Vtable-Grenze.

---

### Extensibility-Konfiguration — Vollständige config.json

```json
{
  "mcp_servers": [
    {
      "id": "filesystem",
      "name": "Filesystem",
      "transport": "stdio",
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"],
      "enabled": true,
      "trust": "untrusted",
      "auto_restart": true,
      "has_ui": false
    }
  ],
  "plugins": {
    "my_llm_backend": {
      "enabled": true,
      "config": {
        "api_base": "https://custom-llm.example.com/v1",
        "model": "custom-model"
      }
    },
    "system_status_ui": {
      "enabled": true,
      "config": {
        "position": "sidebar-bottom",
        "refresh_interval_ms": 2000
      }
    }
  },
  "skills": {
    "enabled": true,
    "paths": [
      "~/.config/rook/skills/",
      "/usr/share/rook/skills/"
    ]
  }
}
```

---

### Extension Store / Marketplace (Zukunft, Phase 7+)

Eine P2P-fähige Registry für Extensions, die Plugin-Entwickler unabhängig von Rook
veröffentlichen können.

```
┌──────────────────────────────────────────┐
│            Extension Registry            │
│                                          │
│  GET /plugins?category=llm               │
│  GET /plugins/{id}                       │
│  GET /plugins/{id}/versions              │
│                                          │
│  Plugin-Index (JSON):                    │
│  {                                       │
│    "id": "porcupine-wakeword",          │
│    "name": "Porcupine Wakeword",        │
│    "version": "1.2.0",                  │
│    "min_rook_api": "1.0.0",             │
│    "max_rook_api": "1.99.0",            │
│    "url": "https://plugins.rook.dev/...",│
│    "checksum": "sha256:...",            │
│    "author": "...",                     │
│    "license": "Apache-2.0"              │
│  }                                       │
└──────────────────────────────────────────┘
```

- **Offizieller Store:** `plugins.rook.dev` (von uns betrieben)
- **Drittanbieter-Registry:** User kann eigene Registry-URL eintragen
- **Manuelle Installation:** `.so` in Plugin-Ordner legen reicht
- Download verifiziert via SHA256-Checksum + optional PGP-Signatur

---

## Architektur — Hexagonale Ports & Adapters

### Gesamtsystem — Multi-Frontend + Server

```
┌──────────────────────────────────────────────────────────────────────┐
│                        FRONTENDS (Adapters)                          │
│                                                                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐               │
│  │  rook-gui    │  │  rook-tui    │  │  rook-web    │               │
│  │  (GTK4)      │  │  (FTXUI)     │  │  (React)     │               │
│  │  Desktop-App │  │  Terminal    │  │  Browser     │               │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘               │
│         │                 │                 │                        │
│         │    implementieren Port-Interfaces direkt                   │
│         │    ODER connecten via gRPC zu rookd                       │
│         │                 │                 │                        │
├─────────┼─────────────────┼─────────────────┼────────────────────────┤
│         │            PORT INTERFACES         │                        │
│         │    (reine abstrakte C++ Klassen,   │                        │
│         │     keine externen Abhängigkeiten) │                        │
│         │                                    │                        │
│  ┌──────┴──────┐ ┌──────┴──────┐ ┌──────────┴──────────┐            │
│  │UserOutputPort│ │UserInputPort│ │  AudioPort         │            │
│  │+ display()  │ │+ on_input() │ │  + play_audio()    │            │
│  │+ stream()   │ │             │ │  + capture_audio() │            │
│  └──────┬──────┘ └──────┬──────┘ └──────────┬──────────┘            │
│         │               │                   │                        │
├─────────┼───────────────┼───────────────────┼────────────────────────┤
│         │         DOMAIN (librook-core)     │                        │
│         │     Hexagon — keine I/O-Abhängigkeiten                     │
│         │                                                            │
│  ┌──────┴──────────────────────────────────────────────────────┐    │
│  │                      Event Bus (intern)                      │    │
│  │  UserInputReceived → AgentLoop → LLMResponseChunk → ...     │    │
│  │  AudioWakeDetected → PipelineStateChange → ...              │    │
│  └────────────────────────────┬─────────────────────────────────┘    │
│                               │                                      │
│  ┌────────────────────────────┼─────────────────────────────────┐   │
│  │                    Domain Services                            │   │
│  │                                                               │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐   │   │
│  │  │Conversation  │  │ AgentEngine  │  │  AudioPipeline   │   │   │
│  │  │Manager       │  │(Tool-Call    │  │  (State Machine) │   │   │
│  │  │              │  │ Loop)        │  │                  │   │   │
│  │  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘   │   │
│  │         │                 │                    │             │   │
│  │  ┌──────┴──────┐  ┌──────┴──────┐  ┌─────────┴──────────┐  │   │
│  │  │SyncEngine   │  │ExtensionMgr│  │  SecurityManager   │  │   │
│  │  │(CRDT Merge) │  │(Plugin Reg)│  │  (Ocap Grants)     │  │   │
│  │  └─────────────┘  └─────────────┘  └───────────────────┘  │   │
│  └────────────────────────────────────────────────────────────┘   │
│                                                                    │
│  ─────────────────── PORT INTERFACES ─────────────────────────────  │
│                                                                    │
│  ┌──────┴──────┐ ┌──────┴──────┐ ┌──────┴──────┐ ┌──────┴──────┐ │
│  │  LLM Port   │ │  Tool Port  │ │ Store Port  │ │ Telemetry   │ │
│  │+ complete() │ │+ call_tool()│ │+ save_chat()│ │  Port       │ │
│  │+ stream()   │ │+ list_tools│ │+ load_chat()│ │+ span()     │ │
│  └──────┬──────┘ └──────┬──────┘ └──────┬──────┘ │+ metric()   │ │
│         │               │               │         └─────────────┘ │
├─────────┼───────────────┼───────────────┼──────────────────────────┤
│         │         ADAPTERS (Infrastruktur)                         │
│         │                                                          │
│  ┌──────┴──────┐ ┌──────┴──────┐ ┌──────┴──────┐                   │
│  │OpenAIAdapter│ │MCPClient    │ │JsonStore    │                   │
│  │OllamaAdapter│ │(Stdio/SSE)  │ │SqliteStore  │                   │
│  │AnthropicAd. │ │BuiltinTools │ │RemoteStore  │                   │
│  └─────────────┘ └─────────────┘ └─────────────┘                   │
│                                                                     │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐                │
│  │OTLPExporter  │ │gRPCServer    │ │CRDTNetwork    │               │
│  │(OpenTelemetry│ │(rookd API)   │ │Layer          │               │
│  │ Collector)   │ │              │ │(WebSocket Sync)│              │
│  └──────────────┘ └──────────────┘ └──────────────┘                │
└──────────────────────────────────────────────────────────────────────┘
```

### Hexagonale Prinzipien

1. **Domain (librook-core) kennt keine I/O.** Kein HTTP, kein GTK, kein Dateisystem, kein Mikrofon. Nur Port-Interfaces.
2. **Adapter implementieren Ports.** Jeder externe Dienst (OpenAI API, MCP-Subprozess, GTK-Widget) ist ein Adapter.
3. **Dependency Inversion.** Domain definiert Ports. Adapter hängen von Ports ab. Nie umgekehrt.
4. **Testbar ohne Infrastruktur.** Domain-Tests laufen ohne Netzwerk, ohne Audio-Devices, ohne GTK. Mock-Adapter reichen.
5. **Frontends sind austauschbar.** GTK, TUI, Web — alle implementieren dieselben Ports. Kein Code-Dupliziert.

### Betriebsmodi

```
Modus 1: Standalone                Modus 2: Connected
┌─────────────────┐               ┌─────────────────┐     gRPC      ┌──────────────┐
│ rook-gui        │               │ rook-gui        │◄────────────►│   rookd      │
│ ┌─────────────┐ │               │ ┌─────────────┐ │              │ (Server)     │
│ │librook-core │ │               │ │  UI-Adapter  │ │              │ ┌──────────┐ │
│ │(Domain +    │ │               │ │  (GTK/TUI/   │ │              │ │librook-  │ │
│ │ Adapter)    │ │               │ │   Web)       │ │              │ │core      │ │
│ │             │ │               │ └──────┬──────┘ │              │ │(Domain + │ │
│ │ LLM direkt  │ │               │        │ gRPC   │              │ │ Adapter) │ │
│ │ MCP lokal   │ │               │ ┌──────┴──────┐ │              │ │          │ │
│ │ Audio lokal │ │               │ │gRPC-Client  │ │              │ │ LLM      │ │
│ └─────────────┘ │               │ │(Stub zu     │ │              │ │ MCP      │ │
│                 │               │ │ rookd)      │ │              │ │ Sync-Hub │ │
└─────────────────┘               │ └─────────────┘ │              │ └──────────┘ │
                                   └─────────────────┘              └──────────────┘

Modus 3: Multi-Server (Federation)
┌──────────────┐     CRDT Sync      ┌──────────────┐
│  rookd A     │◄──────────────────►│  rookd B     │
│  (Home)      │                    │  (Office)    │
│  Clients:    │                    │  Clients:    │
│  - Desktop   │                    │  - Laptop    │
│  - Phone(Web)│                    │  - TUI       │
└──────────────┘                    └──────────────┘
```

---

## Event Bus — Interne Kommunikation

Domain-Komponenten kommunizieren ausschließlich über Events. Kein direktes Method-Chaining.

```cpp
// include/rook/domain/events.hpp
namespace rook::domain::events {

struct UserInputReceived {
    std::string text;
    std::string source;  // "text_input", "voice_stt", "rook_delegate"
    HlcTimestamp timestamp;
};

struct LLMStreamChunk {
    std::string text;
    bool is_final;
    std::string model;
};

struct ToolCallRequested {
    std::string tool_name;
    nlohmann::json arguments;
    std::string mcp_server_id;
};

struct ToolCallCompleted {
    std::string tool_name;
    nlohmann::json result;
    std::chrono::milliseconds duration;
};

struct AudioWakewordDetected {
    std::string keyword;
    float confidence;
};

struct AudioStateChanged {
    AudioState old_state;
    AudioState new_state;
};

struct ConversationCompleted {
    std::string conversation_id;
    std::string final_response;
    std::vector<ToolCallCompleted> tool_calls;
};

} // namespace rook::domain::events
```

```cpp
// include/rook/domain/event_bus.hpp
namespace rook::domain {

template<typename Event>
using EventHandler = std::function<void(const Event&)>;

class EventBus {
public:
    template<typename Event>
    Subscription subscribe(EventHandler<Event> handler);

    template<typename Event>
    void publish(const Event& event);

    // Thread-safe: Handler werden im Thread des Publishers aufgerufen.
    // Für UI-Updates: subscribe mit Glib::Dispatcher-Queue.
};

// Singleton im Domain-Layer (einzige Ausnahme vom DI-Prinzip —
// EventBus ist Infrastruktur, kein Business-Logic-Objekt)
EventBus& event_bus();

} // namespace rook::domain
```

### Agent-Loop via Events

```
UserInputReceived
        │
        ▼
ConversationManager::on_user_input(event)
        │
        ├─► pre_llm hooks
        │
        ▼
LLMPort::stream(messages)
        │
        ▼ (chunk-by-chunk)
LLMStreamChunk { text: "Ich", is_final: false }
LLMStreamChunk { text: " erstelle", is_final: false }
LLMStreamChunk { text: " die Datei.", is_final: true }
        │
        ▼
ConversationManager::on_llm_response(chunks)
        │
        ├─ Falls Tool-Call in Response:
        │   ToolCallRequested → ToolPort → ToolCallCompleted
        │   → zurück zu LLMPort mit Tool-Result → LLMStreamChunk
        │
        ▼
ConversationCompleted { final_response: "..." }
        │
        ├─ pre_response hooks
        │
        ▼
UserOutputPort::display(text)
AudioPort::speak(text)       ← optional falls voice enabled
StorePort::save_chat(chat)   ← persistiert Chat
```

### OpenTelemetry — Observability

Jedes Event erzeugt automatisch einen Span. Der EventBus wrapped publish():

```cpp
template<typename Event>
void EventBus::publish(const Event& event) {
    auto span = telemetry::start_span(typeid(Event).name());
    span.set_attribute("event.source", event.source);
    // ... deliver to subscribers ...
    span.end();
}
```

Traces: `UserInputReceived → STT → LLM stream → Tool Calls → TTS → ConversationCompleted`

---

## gRPC — Client-Server-Protokoll

### Service-Definition (Protobuf)

```protobuf
// proto/rook/v1/service.proto
syntax = "proto3";
package rook.v1;

service RookService {
  // Bidirektionales Chat-Streaming
  rpc Chat(stream ChatRequest) returns (stream ChatResponse);

  // Task an anderen Client delegieren
  rpc DelegateTask(DelegateTaskRequest) returns (DelegateTaskResponse);

  // Sync-Engine: CRDT State Push/Pull
  rpc Sync(stream SyncEvent) returns (stream SyncEvent);

  // Extension-Management
  rpc ListExtensions(ListExtensionsRequest) returns (ListExtensionsResponse);
  rpc InstallExtension(InstallExtensionRequest) returns (InstallExtensionResponse);
  rpc RemoveExtension(RemoveExtensionRequest) returns (RemoveExtensionResponse);

  // Config
  rpc GetConfig(GetConfigRequest) returns (GetConfigResponse);
  rpc UpdateConfig(UpdateConfigRequest) returns (UpdateConfigResponse);

  // Client-Registry (welche Clients sind verbunden?)
  rpc ListClients(ListClientsRequest) returns (ListClientsResponse);
}

message ChatRequest {
  string conversation_id = 1;
  repeated Message messages = 2;

  message Message {
    string role = 1;        // "user", "assistant", "tool"
    string content = 2;
    repeated ToolCall tool_calls = 3;
  }

  message ToolCall {
    string id = 1;
    string name = 2;
    string arguments = 3;   // JSON string
  }
}

message ChatResponse {
  oneof payload {
    StreamChunk chunk = 1;
    ToolCallRequest tool_request = 2;
    ChatComplete complete = 3;
    ErrorInfo error = 4;
  }

  message StreamChunk {
    string text = 1;
    bool is_final = 2;
  }

  message ToolCallRequest {
    string tool_name = 1;
    string arguments = 2;  // JSON
    string mcp_server = 3;
  }

  message ChatComplete {
    string final_text = 1;
    repeated ToolCallResult tool_results = 2;
  }

  message ToolCallResult {
    string tool_name = 1;
    string result = 2;     // JSON
  }

  message ErrorInfo {
    string code = 1;
    string message = 2;
  }
}

message DelegateTaskRequest {
  string target_client_id = 1;
  string task = 2;
}

message DelegateTaskResponse {
  string client_id = 1;
  string result = 2;
}

message SyncEvent {
  string client_id = 1;
  HlcTimestamp timestamp = 2;
  oneof payload {
    SettingsUpdated settings = 3;
    ExtensionAdded extension_added = 4;
    ExtensionRemoved extension_removed = 5;
    ChatMessageAppended chat_message = 6;
  }
}

message HlcTimestamp {
  int64 wall_time_ms = 1;
  int32 logical_counter = 2;
  string node_id = 3;
}
```

### gRPC-Gateway → REST (automatisch generiert)

```yaml
# proto/rook/v1/service_gateway.yaml
type: google.api.Service
config_version: 3

http:
  rules:
    - selector: rook.v1.RookService.ListExtensions
      get: /api/v1/extensions
    - selector: rook.v1.RookService.InstallExtension
      post: /api/v1/extensions
    - selector: rook.v1.RookService.RemoveExtension
      delete: /api/v1/extensions/{id}
    - selector: rook.v1.RookService.GetConfig
      get: /api/v1/config
    - selector: rook.v1.RookService.UpdateConfig
      put: /api/v1/config
    - selector: rook.v1.RookService.ListClients
      get: /api/v1/clients
```

REST ist ein generated reverse-proxy. Kein handgeschriebener REST-Code. gRPC ist die Source-of-Truth.

---

## CRDT-Synchronisation

### CRDT-Typen

| Datentyp | CRDT | Algorithmus | Merge-Komplexität |
|---|---|---|---|
| **Chat-Nachrichten** | YATA (Yet Another Transformation Approach) | Yjs-ähnlich, insertion-basiert mit Tombstones | O(n) pro Merge |
| **Extensions** | AWSet (Add-Wins Observed-Remove Set) | Kleppmann 2014, Tombstone-basiert | O(1) pro Add/Remove |
| **Settings** | LWW-Register (Last-Writer-Wins) | HLC (Hybrid Logical Clock) Timestamp | O(1) pro Key |
| **Plugin-Konfiguration** | LWW-Map (Map<Key, LWW-Register>) | Map von LWW-Registern | O(k) pro k Keys |
| **Client-Liste** | 2P-Set (Two-Phase Set) | Join-Semilattice, nie Löschung | O(1) |

### HLC (Hybrid Logical Clock)

```cpp
// include/rook/sync/hlc.hpp
namespace rook::sync {

struct HlcTimestamp {
    int64_t wall_time_ms;
    int32_t logical_counter;
    std::string node_id;  // UUID des Clients/Servers

    auto operator<=>(const HlcTimestamp&) const = default;
};

class HybridLogicalClock {
public:
    HlcTimestamp now();

    // Called when receiving a remote event: advance clock past remote time.
    void observe(const HlcTimestamp& remote);

private:
    int64_t m_last_wall_time = 0;
    int32_t m_logical = 0;
    std::string m_node_id;
};

// Guarantee:
// - If event A happens-before event B, then A.timestamp < B.timestamp.
// - If A concurrent with B, timestamps may be equal → tie-break on node_id.

} // namespace rook::sync
```

### Deterministic Simulation Testing (DST)

Sync-Engine wird in einem deterministischen Simulator getestet:

```cpp
// tests/sync/test_sync_engine_dst.cpp

// Der Simulator:
// - Kontrolliert die virtuelle Uhr (kein echtes sleep/wall-clock)
// - Simuliert Netzwerk-Partitionen, Package-Drops, Duplicates, Reordering
// - Jeder Test-Run mit gleichem Seed = gleiches Ergebnis (deterministisch)
// - Testet: CRDT-Merge-Korrektheit, Konvergenz, Konflikt-Auflösung

TEST(SyncEngineDST, ConcurrentSettingsUpdate_Converges) {
    DeterministicSimulator sim(/*seed=*/42);

    auto client_a = sim.create_client("a");
    auto client_b = sim.create_client("b");
    auto server   = sim.create_server("s");

    // Client A setzt temperature=0.7
    client_a.update_setting("temperature", "0.7", sim.clock.now());

    // Simultan (gleiche virtuelle Zeit): Client B setzt temperature=0.9
    client_b.update_setting("temperature", "0.9", sim.clock.now());

    // Netzwerk-Partition: A↔Server OK, B↔Server getrennt
    sim.partition("b", "s");

    // 10 virtuelle Ticks vergehen
    sim.advance(10);

    // Partition auflösen
    sim.heal("b", "s");

    // Genug Zeit zum syncen
    sim.advance(100);

    // Beide Clients müssen konvergieren (LWW: späterer Timestamp gewinnt)
    EXPECT_EQ(client_a.get_setting("temperature"), "0.9");
    EXPECT_EQ(client_b.get_setting("temperature"), "0.9");
}

TEST(SyncEngineDST, ExtensionAddRemove_NoDoubleDelete) {
    // Testet AWSet: add→remove→add = add (nicht delete)
    // ...
}

TEST(SyncEngineDST, ChatYata_ConcurrentInserts_MaintainsOrder) {
    // Zwei Clients schreiben gleichzeitig in denselben Chat
    // YATA garantiert: beide Nachrichten erscheinen, Reihenfolge konsistent
    // ...
}
```

---

## Object-Capability Security (Ocap)

Statt: "Tool X ist whitelisted". Sondern: Jeder MCP-Server erhält präzise Capabilities.

```cpp
// include/rook/security/capability.hpp
namespace rook::security {

class Capability {
public:
    static Capability grant();

    Capability& read(const std::string& path);       // Darf Pfad lesen
    Capability& write(const std::string& path);      // Darf Pfad schreiben
    Capability& network(const std::string& host);    // Darf Host kontaktieren (oder "any")
    Capability& no_network();                         // Kein Netzwerk
    Capability& max_memory_mb(int64_t mb);
    Capability& max_cpu_time(std::chrono::seconds t);
    Capability& max_processes(int n);
    Capability& env(const std::string& key, const std::string& value);

    // Capability ist unforgeable: nur der SecurityManager kann sie erzeugen.
    // Adapter können sie nicht fälschen.
};

// Der SecurityManager ist der EINZIGE Ort, der Capabilities erzeugt.
class SecurityManager {
public:
    // Erzeugt Capabilities aus Config + User-Entscheidung
    Capability create_for_mcp_server(const McpServerConfig& config);

    // Prüft ob eine Operation erlaubt ist
    bool is_allowed(const Capability& cap, const Operation& op);

    // Revoke: Capability wird ungültig (auch für laufende Operationen)
    void revoke(const std::string& mcp_server_id);
};

} // namespace rook::security
```

### Capability-Beispiel: Filesystem-MCP-Server

```json
// In config.json:
{
  "mcp_servers": [
    {
      "id": "filesystem",
      "capabilities": {
        "read": ["/home/user/projects", "/home/user/docs"],
        "write": ["/home/user/projects/output"],
        "network": false,
        "max_memory_mb": 256,
        "max_cpu_time_sec": 60,
        "max_processes": 5,
        "env": {
          "HOME": "/home/user"
        }
      }
    }
  ]
}
```

Der Server KANN nicht außerhalb dieser Grenzen operieren — selbst wenn der MCP-Code kompromittiert ist. bwrap setzt die Capabilities als OS-Level-Sandbox durch.

### Ocap + Whitelist (zwei Ebenen)

- **Ocap (Ebene 1):** OS-Level-Garantien (bwrap, rlimits). Unumgehbar.
- **Whitelist (Ebene 2):** UX-Layer. User kann temporär mehr erlauben (Confirm-Dialog). Aber ocap-Grenzen werden nie überschritten.

---

### Architektur (ASCII-Diagramm, das alte bleibt erhalten)

```
                          mute/unmute toggle (global)
                    ┌─────────────────────────────┐
                    │                             │
                    ▼                             │
               ┌─────────┐                        │
               │  MUTED  │◄───────────────────────┤
               └─────────┘                        │
                    │ unmute                       │
                    ▼                             │
     ┌──────────────────────────────┐             │
     │                              │             │
     ▼                              │             │
  ┌──────┐   wakeword detected  ┌──────┐          │
  │ IDLE │─────────────────────►│ WAKE │          │
  └──────┘                      └──┬───┘          │
     ▲                             │              │
     │                      play acknowledgement │
     │                      sound (optional)     │
     │                             │              │
     │                    start microphone        │
     │                    recording               │
     │                             │              │
     │                             ▼              │
     │                      ┌──────────┐         │
     │                      │LISTENING │         │
     │                      │   STT    │         │
     │                      └────┬─────┘         │
     │                           │               │
     │                    stop on:                │
     │                    • silence timeout (2s)  │
     │                    • VAD end-of-speech     │
     │                    • max duration (30s)    │
     │                           │               │
     │                    run whisper.cpp         │
     │                           │               │
     │                           ▼               │
     │                     ┌──────────┐          │
     │                     │PROCESSING│          │
     │                     │ LLM +    │          │
     │                     │ Tools    │          │
     │                     └────┬─────┘          │
     │                          │                │
     │                   LLM response            │
     │                   (Tool-Calls              │
     │                    executed if needed)     │
     │                          │                │
     │                          ▼                │
     │                     ┌──────────┐          │
     │                     │ SPEAKING │──────────┘
     │                     │   TTS    │
     │                     └──────────┘
     │
     └─── Stop-Command ("stop" / "ruhe" / "leise")
```

**Zusätzliche States:**
- **ERROR**: Audio-Device disconnected, API-Error → Auto-Retry oder User-Notification
- **DOWNLOADING**: Erst-Run: whisper.cpp Modell, piper Voice, Porcupine .ppn werden heruntergeladen

---

## LLM Backend — Strategy Pattern

```
                    ┌─────────────────┐
                    │   LLM Backend   │  (abstract)
                    │                 │
                    │ + chat(history) │
                    │ + chat_stream() │
                    │ + list_models() │
                    │ + validate_key()│
                    └────────┬────────┘
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
  ┌───────┴───────┐ ┌───────┴───────┐ ┌───────┴───────┐
  │OpenAIBackend  │ │AnthropicBcknd │ │OllamaBackend  │
  │               │ │               │ │               │
  │POST /v1/chat  │ │POST /v1/      │ │POST /api/chat │
  │SSE Streaming  │ │  messages     │ │SSE Streaming  │
  │gpt-4o, etc.   │ │SSE Streaming  │ │llama3, etc.   │
  └───────────────┘ └───────────────┘ └───────────────┘
          │
  ┌───────┴───────┐
  │OpenAICompat   │  (generic: any OpenAI-compatible endpoint)
  │               │
  │Custom base_url│
  │(vLLM, LiteLLM,│
  │ LocalAI, ...) │
  └───────────────┘
```

**Konkrete Modelleinstellungen (global, pro Provider):**
- OpenAI: `gpt-4o`, `gpt-4o-mini`, `gpt-4-turbo`, ...
- Anthropic: `claude-sonnet-4-20250514`, `claude-haiku-3-5`, ...
- Ollama: `llama3`, `mistral`, `gemma`, ... (via API abfragbar)
- OpenAI-kompatibel: Beliebig (Custom URL)

---

## MCP Client — JSON-RPC Implementation

Eigener C++ MCP-Client nach [MCP Specification](https://spec.modelcontextprotocol.io/).

### Transport-Layer

```
┌─────────────────────────────────────┐
│          MCP Transport (abstract)   │
│  + send_request(method, params)     │
│  + send_notification(method, params)│
│  + on_message(callback)             │
│  + start() / stop()                │
└──────────────┬──────────────────────┘
               │
   ┌───────────┴───────────┐
   │                       │
┌──┴──────────────┐ ┌──────┴──────────┐
│ STDIO Transport │ │ SSE Transport   │
│                 │ │                 │
│ Subprozess      │ │ HTTP POST +     │
│ stdin/stdout    │ │ SSE EventSource │
│ JSON-RPC        │ │                 │
└─────────────────┘ └─────────────────┘
```

### Protocol Flow

```
Client (Rook)                          MCP Server (Subprozess)

    │──── initialize ────────────────────►│
    │◄─── capabilities + server_info ────│
    │                                     │
    │──── tools/list ────────────────────►│
    │◄─── [tool1, tool2, ...] ──────────│
    │                                     │
    │──── tools/call(name, args) ────────►│
    │◄─── result / error ────────────────│
    │                                     │
    │◄─── notifications/ (optional) ─────│
```

### Tool-Call Loop im Agent

```
User: "Erstelle eine Datei test.txt mit Hallo Welt"

1. LLM antwortet: tool_call { name: "write_file", args: {path:"test.txt", content:"Hallo Welt"} }
2. Agent prüft Tool-Whitelist:
   - write_file ist NICHT in Whitelist → Confirmation-Dialog
   - User klickt "Erlauben" (oder "Immer erlauben" → Whitelist)
3. Agent ruft MCP-Client: mcp->call_tool("write_file", args)
4. MCP-Server antwortet: { success: true }
5. Agent sendet Tool-Result an LLM
6. LLM antwortet final: "Die Datei wurde erstellt."
7. → TTS / UI-Ausgabe
```

### Tool Permission System

```
┌────────────────────────────────────────┐
│         Tool Permission Manager        │
│                                        │
│  Whitelist (auto-approve):            │
│    - read_file                         │
│    - list_directory                    │
│    - search                            │
│    - web_fetch                         │
│    - get_time                          │
│                                        │
│  Needs Confirmation (default):         │
│    - write_file / edit_file            │
│    - execute_command                   │
│    - delete_file                       │
│    - network_access                    │
│    - alles was nicht in Whitelist      │
│                                        │
│  Confirmation Dialog:                  │
│    ┌────────────────────────────┐      │
│    │  Rook möchte:              │      │
│    │  write_file(/etc/passwd)   │      │
│    │                            │      │
│    │  [Immer erlauben] [Einmal] │      │
│    │  [Ablehnen]               │      │
│    └────────────────────────────┘      │
└────────────────────────────────────────┘
```

---

## Tray-Icon & Startup

```
┌──────────────────────────────────────────┐
│            System Tray (AppIndicator)    │
│                                          │
│  🐦 Rook                                 │
│    ├─ Chat öffnen                        │
│    ├─ Voice: Aktiv / Stumm               │
│    ├─ Schnellfrage...                    │
│    ├────────────                         │
│    └─ Beenden                            │
└──────────────────────────────────────────┘
```

- **Fenster-Schließen = In Tray minimieren** (nicht beenden)
- **Beenden nur via Tray-Menü oder Ctrl+Q**
- **Tray-Icon zeigt Voice-Status** (grün = lauschend, rot = stumm, grau = inaktiv)
- Kein Autostart (User konfiguriert selbst via GNOME Tweaks / Autostart-Ordner)

---

## First-Run Wizard

```
┌──────────────────────────────────────────────┐
│           Willkommen bei Rook!               │
│                                              │
│  Schritt 1: LLM-Provider einrichten          │
│    ○ OpenAI (API-Key benötigt)               │
│    ○ Anthropic (API-Key benötigt)            │
│    ○ Ollama (lokal, http://localhost:11434)  │
│    ○ Anderer (OpenAI-kompatibel)             │
│    [Überspringen, später einrichten]         │
│                                              │
│  Schritt 2: Wakeword konfigurieren           │
│    Wakeword-Datei (.ppn): [Durchsuchen...]   │
│    Porcupine Access Key: [_______________]   │
│    Sensitivity: [========|====] 0.5          │
│    [Wakeword testen 🎤]                      │
│    [Überspringen, nur Text-Mode]             │
│                                              │
│  Schritt 3: Stimme auswählen (TTS)           │
│    ○ thorsten-medium (deutsch, männlich)     │
│    ○ eva_k-medium (deutsch, weiblich)        │
│    ○ Andere herunterladen...                 │
│    [Stimme testen 🔊]                        │
│    [Überspringen, ohne TTS]                  │
│                                              │
│  Schritt 4: Whisper-Modell herunterladen      │
│    Modell: small (~500MB)                    │
│    [Herunterladen & installieren]            │
│    [Überspringen, kein lokales STT]         │
│                                              │
│  [← Zurück]  [Fertig stellen →]             │
└──────────────────────────────────────────────┘
```

Der Wizard ist eine `Gtk::Assistant`-Seite im Window-Stack. Alle Schritte sind optional (überspringbar), Rook funktioniert auch als reiner Text-Chat ohne Voice.

---

## Chat-Historie — Persistenz

```
~/.local/share/rook/chats/
├── index.json                    # Liste aller Chats
├── chat_2026-05-30T14-30-00.json # Einzelner Chat
├── chat_2026-05-30T15-00-00.json
└── ...
```

**index.json:**
```json
{
  "chats": [
    {
      "id": "2026-05-30T14-30-00",
      "title": "C++ Frage zu Templates",
      "model": "gpt-4o",
      "provider": "openai",
      "created": "2026-05-30T14:30:00",
      "last_message": "2026-05-30T14:45:00",
      "message_count": 12
    }
  ]
}
```

**Chat-Datei (JSON):**
```json
{
  "id": "2026-05-30T14-30-00",
  "title": "C++ Frage zu Templates",
  "model": "gpt-4o",
  "provider": "openai",
  "system_prompt": "Du bist Rook, ein hilfreicher...",
  "messages": [
    {"role": "user", "content": "Was ist SFINAE?"},
    {"role": "assistant", "content": "SFINAE steht für...", "tool_calls": []},
    {"role": "tool", "tool_call_id": "call_123", "content": "..."}
  ]
}
```

- Auto-Save nach jeder Nachricht
- Titel = erste User-Nachricht (gekürzt auf 40 Zeichen)
- Sidebar: Chat-Liste mit Suche, Löschen, Umbenennen
- Export als JSON / Import von JSON

---

## i18n — gettext-Integration

Verzeichnisstruktur:
```
po/
├── de.po          # Deutsche Übersetzung
├── en.po          # Englische Übersetzung (Source)
├── LINGUAS        # Liste unterstützter Sprachen
└── POTFILES       # Quelldateien mit _()-Calls
```

Meson-Integration:
```meson
i18n = import('i18n')
subdir('po')
# → generiert po/rook.mo → installiert nach /usr/share/locale/
```

Im C++-Code:
```cpp
#include <glib/gi18n.h>
// _() Makro für literals, ngettext() für Plural
auto label = Gtk::make_managed<Gtk::Label>(_("Hello, World!"));
```

- Default-Locale = System-Sprache
- Fallback: Englisch wenn Übersetzung fehlt
- Voice-Prompts immer in User-Sprache (DE-first)

---

## Projektdetail — Ordnerstruktur

```
rook/
├── flake.nix                              # Nix Dev-Shell + Package (alle Komponenten)
├── flake.lock
├── meson.build                            # Root Build
├── proto/
│   ├── rook/
│   │   └── v1/
│   │       ├── service.proto              # gRPC Service-Definition
│   │       ├── service_gateway.yaml       # gRPC-Gateway → REST Mapping
│   │       └── types.proto                # Shared message types
│   └── buf.yaml                           # Buf-Konfiguration (Lint + Breaking Change Detection)
├── libs/
│   └── rook-core/                         # librook-core: Hexagonale Domain-Bibliothek
│       ├── meson.build
│       ├── include/rook/
│       │   ├── domain/
│       │   │   ├── events.hpp             # Alle Domain-Events
│       │   │   ├── event_bus.hpp          # EventBus (pub/sub)
│       │   │   ├── conversation.hpp       # ConversationManager
│       │   │   ├── agent.hpp              # AgentEngine (Tool-Call-Loop)
│       │   │   └── audio_pipeline.hpp     # Audio State Machine
│       │   ├── ports/                     # PORT INTERFACES (reine Abstraktionen)
│       │   │   ├── llm_port.hpp           # LLM Port
│       │   │   ├── tool_port.hpp          # Tool Port (MCP + Built-in)
│       │   │   ├── store_port.hpp         # Storage Port (Chats, Config)
│       │   │   ├── audio_port.hpp         # Audio I/O Port
│       │   │   ├── user_output_port.hpp   # User-Output Port (UI Rendering)
│       │   │   ├── user_input_port.hpp    # User-Input Port
│       │   │   └── telemetry_port.hpp     # OpenTelemetry Port
│       │   ├── sync/
│       │   │   ├── hlc.hpp                # Hybrid Logical Clock
│       │   │   ├── crdt_chat.hpp          # YATA CRDT für Chats
│       │   │   ├── crdt_extensions.hpp    # AWSet für Extensions
│       │   │   ├── crdt_settings.hpp      # LWW-Map für Settings
│       │   │   └── sync_engine.hpp        # Sync-Engine (Push/Merge)
│       │   ├── security/
│       │   │   ├── capability.hpp         # Object-Capability API
│       │   │   ├── security_manager.hpp   # Capability-Erzeuger
│       │   │   └── command_guard.hpp      # Dangerous Command Detection
│       │   └── frontend.hpp               # Abstract Frontend Interface
│       └── src/
│           ├── domain/                    # Domain-Implementierungen
│           ├── sync/                      # CRDT-Implementierungen
│           ├── security/                  # Security-Implementierungen
│           └── adapters/                  # ADAPTERS (Infrastruktur)
│               ├── llm/
│               │   ├── openai_adapter.cpp
│               │   ├── anthropic_adapter.cpp
│               │   └── ollama_adapter.cpp
│               ├── mcp/
│               │   ├── mcp_client.cpp
│               │   ├── stdio_transport.cpp
│               │   └── sse_transport.cpp
│               ├── store/
│               │   ├── json_store.cpp
│               │   └── sqlite_store.cpp   # Später für Server-Mode
│               ├── audio/
│               │   ├── wakeword_porcupine.cpp
│               │   ├── stt_whisper.cpp
│               │   ├── tts_piper.cpp
│               │   └── audio_device_miniaudio.cpp
│               ├── telemetry/
│               │   └── otlp_exporter.cpp
│               └── server/
│                   ├── grpc_service.cpp   # gRPC Service-Implementierung
│                   └── gateway.cpp        # gRPC-Gateway REST-Proxy
├── src/
│   ├── rook-gui/                           # GTK4 Desktop App
│   │   ├── main.cpp                       # Entry Point
│   │   ├── application.hpp / .cpp         # Gtk::Application + Tray
│   │   ├── window.hpp / .cpp              # Main Window
│   │   └── views/
│   │       ├── chat_view.hpp / .cpp
│   │       ├── chat_sidebar.hpp / .cpp
│   │       ├── message_widget.hpp / .cpp
│   │       ├── settings_dialog.hpp / .cpp
│   │       ├── mcp_config_dialog.hpp / .cpp
│   │       ├── voice_indicator.hpp / .cpp
│   │       └── first_run_wizard.hpp / .cpp
│   ├── rook-tui/                           # FTXUI Terminal App
│   │   ├── main.cpp
│   │   └── tui_frontend.hpp / .cpp        # Implementiert Frontend-Interface
│   └── rookd/                              # Headless Daemon
│       ├── main.cpp                       # Server Entry Point
│       ├── server.hpp / .cpp              # gRPC-Server + REST-Gateway
│       └── config.hpp / .cpp              # Server-Konfiguration
├── web/
│   └── rook-web/                           # React Web-Frontend
│       ├── package.json
│       ├── src/
│       │   ├── App.tsx
│       │   ├── hooks/useGrpcClient.ts     # gRPC-Web Client
│       │   └── components/
│       └── public/
├── vendor/
│   ├── miniaudio.h                        # Vendored Single-Header
│   └── porcupine/
│       ├── include/pv_porcupine.h
│       └── lib/linux/x86_64/libpv_porcupine.so
├── data/
│   ├── icons/hicolor/scalable/apps/
│   │   └── io.github.fleischerdesign.Rook.svg
│   ├── io.github.fleischerdesign.Rook.desktop.in
│   └── io.github.fleischerdesign.Rook.gschema.xml
├── po/
│   ├── de.po
│   ├── en.po
│   ├── LINGUAS
│   └── POTFILES
├── resources/
│   └── wakewords/                         # Default .ppn Dateien
├── tests/
│   ├── meson.build
│   ├── fixtures/
│   │   ├── configs/                       # minimal.json, full.json, broken.json
│   │   ├── audio/                         # hey_rook.wav, silence.wav, german_speech.wav
│   │   ├── chats/                         # sample_chat.json, corrupt_chat.json
│   │   └── plugins/                       # mock_llm.so, mock_wakeword.so, mock_hook.so
│   ├── unit/
│   │   ├── test_conversation.cpp
│   │   ├── test_event_bus.cpp
│   │   ├── test_mcp_client.cpp
│   │   ├── test_plugin_registry.cpp
│   │   ├── test_plugin_loader.cpp
│   │   ├── test_permission_manager.cpp
│   │   ├── test_command_guard.cpp
│   │   ├── test_history_manager.cpp
│   │   ├── test_config.cpp
│   │   ├── test_audio_pipeline.cpp
│   │   ├── test_capability.cpp
│   │   ├── test_thread_safe_queue.cpp
│   │   └── test_markdown_renderer.cpp
│   ├── integration/
│   │   ├── test_conversation_mcp.cpp
│   │   ├── test_audio_pipeline_e2e.cpp
│   │   ├── test_plugin_loading.cpp
│   │   ├── test_config_loading.cpp
│   │   ├── test_history_roundtrip.cpp
│   │   └── test_grpc_server.cpp
│   ├── sync/                              # Deterministic Simulation Tests
│   │   ├── test_sync_engine_dst.cpp
│   │   ├── test_crdt_chat.cpp
│   │   ├── test_crdt_extensions.cpp
│   │   └── test_crdt_settings.cpp
│   └── ui/
│       ├── test_chat_view.cpp
│       └── test_message_widget.cpp
├── plan.md                                # Diese Datei
└── README.md
```

---

## Build-System

### meson.build (Root)

```meson
project('rook', 'cpp',
  version: '0.1.0',
  license: 'MIT',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
    'werror=true'
  ]
)

# i18n
i18n = import('i18n')
gnome = import('gnome')

# Dependencies
gtkmm_dep      = dependency('gtkmm-4.0', version: '>=4.0')
adwaita_dep    = dependency('libadwaita-1')
curl_dep       = dependency('libcurl')
json_dep       = dependency('nlohmann_json', version: '>=3.11')
secret_dep     = dependency('libsecret-1')
spdlog_dep     = dependency('spdlog')
thread_dep     = dependency('threads')
ayatana_dep    = dependency('ayatana-appindicator3-0.1')

# GTK resources (icons, styles)
gnome.compile_resources('rook-resources',
  'data/io.github.fleischerdesign.Rook.gresource.xml',
  c_name: 'rook_resources'
)

# GSettings schema
gnome.compile_schemas()

subdir('src')
subdir('po')
subdir('tests')
```

### flake.nix

```nix
{
  description = "Rook — Voice AI Agent";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        stdenv = pkgs.gcc14Stdenv;
      in {
        devShells.default = (pkgs.mkShell.override { inherit stdenv; }) {
          buildInputs = with pkgs; [
            gtkmm4
            libadwaita
            nlohmann_json
            libcurl
            libsecret
            spdlog
            libayatana-appindicator
            meson
            ninja
            pkg-config
            gettext
            desktop-file-utils
            # Voice/Audio
            piper-tts
            whisper-cpp
            ollama
            # Dev tools
            git
            gdb
            valgrind
            clang-tools
          ];
        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "rook";
          version = "0.1.0";
          src = self;
          nativeBuildInputs = with pkgs; [
            meson ninja pkg-config gettext desktop-file-utils
          ];
          buildInputs = with pkgs; [
            gtkmm4 libadwaita nlohmann_json libcurl libsecret
            spdlog libayatana-appindicator
          ];
        };
      });
}
```

---

## Thread-Modell

| Thread          | Priorität | Aufgabe                                       | Library         |
|-----------------|-----------|-----------------------------------------------|-----------------|
| **Main / GTK**  | Normal    | GTK Event Loop, UI Updates                    | gtkmm           |
| **Wakeword**    | Hoch      | Porcupine continuous detection, 16kHz mono    | miniaudio       |
| **STT Worker**  | Normal    | whisper.cpp Subprozess starten & Ergebnis lesen | std::async     |
| **LLM Worker**  | Normal    | HTTP-Request + SSE-Streaming zu Provider      | libcurl         |
| **TTS Worker**  | Normal    | piper Subprozess, Audio-Playback              | miniaudio       |
| **MCP Worker**  | Normal    | MCP-Tool-Calls ausführen (blockierend)        | std::async      |

**Signal-Flow:**
- Wakeword-Thread → `Glib::Dispatcher` → Main-Thread (Wakeword erkannt)
- LLM-Streaming-Chunks → `Glib::signal_idle()` → Chat-View (stotterfreies UI-Update)
- Audio-Pipeline-Status → `Glib::Dispatcher` → Voice-Indicator Widget

---

## Settings & Config

### GSettings Schema (`io.github.fleischerdesign.Rook.gschema.xml`)

```xml
<schemalist>
  <schema id="io.github.fleischerdesign.Rook"
          path="/io/github/fleischerdesign/Rook/">
    <!-- LLM -->
    <key name="llm-provider" type="s">
      <default>'openai'</default>
      <summary>LLM Provider</summary>
    </key>
    <key name="llm-model" type="s">
      <default>'gpt-4o'</default>
      <summary>LLM Model</summary>
    </key>
    <key name="openai-api-base" type="s">
      <default>'https://api.openai.com/v1'</default>
    </key>
    <key name="ollama-api-base" type="s">
      <default>'http://localhost:11434'</default>
    </key>
    <key name="context-window" type="i">
      <default>8000</default>
      <summary>Max context tokens</summary>
    </key>
    <key name="max-tokens" type="i">
      <default>4096</default>
      <summary>Max response tokens</summary>
    </key>
    <!-- System Prompt -->
    <key name="system-prompt" type="s">
      <default>'Du bist Rook, ein hilfreicher, präziser AI-Assistent. Antworte auf Deutsch.'</default>
      <summary>System Prompt</summary>
    </key>
    <!-- Voice -->
    <key name="voice-enabled" type="b">
      <default>true</default>
    </key>
    <key name="porcupine-access-key" type="s">
      <default>''</default>
      <summary>Picovoice Porcupine Access Key</summary>
    </key>
    <key name="wakeword-path" type="s">
      <default>''</default>
      <summary>Path to .ppn wakeword file</summary>
    </key>
    <key name="wakeword-sensitivity" type="d">
      <default>0.5</default>
    </key>
    <key name="stt-model" type="s">
      <default>'small'</default>
    </key>
    <key name="tts-voice" type="s">
      <default>'de_DE-thorsten-medium'</default>
    </key>
    <!-- Audio -->
    <key name="audio-input-device" type="s">
      <default>''</default>
    </key>
    <key name="audio-output-device" type="s">
      <default>''</default>
    </key>
    <!-- UI -->
    <key name="window-width" type="i">
      <default>900</default>
    </key>
    <key name="window-height" type="i">
      <default>700</default>
    </key>
    <key name="dark-theme" type="b">
      <default>true</default>
      <summary>Use dark theme</summary>
    </key>
    <key name="first-run-complete" type="b">
      <default>false</default>
    </key>
  </schema>
</schemalist>
```

### Config File (`~/.config/rook/config.json`)

```json
{
  "mcp_servers": [
    {
      "name": "filesystem",
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/home/user"],
      "transport": "stdio",
      "enabled": true
    }
  ],
  "tool_whitelist": [
    "read_file",
    "list_directory",
    "web_fetch",
    "search",
    "get_time"
  ],
  "wakewords": [
    {
      "name": "hey_rook",
      "path": "~/.config/rook/wakewords/hey_rook_de.ppn",
      "sensitivity": 0.5
    }
  ]
}
```

---

## Security & Trust Model

### Object-Capability (Ocap) Security

Rook verwendet ein **Object-Capability-Modell** als fundamentale Sicherheitsarchitektur.
Siehe [Architektur — Object-Capability Security](#object-capability-security-ocap) für die API.

**Prinzipien:**
1. **Capabilities sind unforgeable.** Nur der SecurityManager kann sie erzeugen.
2. **Capabilities sind revocable.** Jederzeit widerrufbar (auch für laufende Operationen).
3. **Capabilities sind composable.** Ein MCP-Server kann seine Capabilities an Sub-Server delegieren (nur enger).
4. **Least Privilege.** Jeder MCP-Server bekommt nur das Minimum, das er braucht.
5. **OS-Level-Durchsetzung.** bwrap setzt Capabilities auf Kernel-Ebene durch.

**Zwei-Ebenen-Modell:**
- **Ebene 1 (Ocap):** Unumgehbare OS-Garantien via bwrap + rlimits. Der MCP-Code KANN physisch nicht außerhalb seiner Capabilities operieren.
- **Ebene 2 (Whitelist/UI):** UX-Layer für dynamische Freigaben. User kann temporär via Confirm-Dialog erweitern — aber nie über Ocap-Grenzen hinaus.

### Threat Model

| Bedrohung | Vector | Defense |
|---|---|---|
| Malicious MCP Server | npm/pip supply chain | Ocap (bwrap-Isolation, rlimits), Audit-Log |
| MCP Server versucht Privilege Escalation | MCP-Code kompromittiert | Ocap-Grenzen durch bwrap enforced — kann nicht überschritten werden |
| Malicious .so Plugin | manuelle Installation | User-Trust-Level (Tier-1 = explizit vertraut, keine Sandbox) |
| Prompt Injection | Web-Inhalte in Tool-Outputs | System-Prompt-Hardening, Output-Sanitization, Audit-Log |
| LLM generiert dangerous commands | LLM-Output | CommandGuard (Pattern-Prüfung), Ocap-Limits auf Shell-Server |
| API-Key Leak | Logs, Error-Messages | libsecret, Key-Never-Log-Regel, `${ENV:VAR}` in Config |
| Dauerhaft lauschendes Mikrofon | Privacy | Lokales Wakeword, Audio nie persistent, visueller Indikator |
| Supply-Chain-Angriff | Plugin-Store | SHA256 + PGP-Signatur-Verifikation |
| Server-Takeover | rookd exponiert im Netzwerk | mTLS, JWT-Auth, Rate-Limiting, gRPC-Interceptors |
| CRDT State Poisoning | Sync-Event von kompromittiertem Client | Server validiert CRDT-Ops, signierte Events, HLC-Validierung |

### Security Levels

| Level | Typ | Isolation | Netzwerk | FS-Zugriff | Ocap Enforcement |
|---|---|---|---|---|---|
| **L0** | Built-in Core | In-Process | Ja | Voll | Kein (vertrauenswürdig) |
| **L1** | User .so Plugin | In-Process (User-Trusted) | Ja | Via CoreAPI | Kein (User-Entscheidung) |
| **L2** | Untrusted MCP | Subprozess + bwrap | Nein (default) | Declared paths | Ocap via bwrap |
| **L3** | Untrusted MCP + Net | Subprozess + bwrap | Ja (allowlist) | Declared paths | Ocap via bwrap + netfilter |

### Sandboxing (bwrap via Ocap-Config)

```yaml
# Per-Server Capability-Definition in config.json
mcp_servers:
  - id: "filesystem"
    capabilities:
      read: ["/home/user/projects", "/home/user/docs"]
      write: ["/home/user/projects/output"]
      network: false
      rlimits:
        max_memory_mb: 256
        max_cpu_time_sec: 60
        max_processes: 5
```

Generiert:
```
bwrap \
  --ro-bind /usr /usr \
  --tmpfs /tmp \
  --unshare-net \
  --unshare-pid \
  --die-with-parent \
  --bind /home/user/projects /home/user/projects \
  --bind /home/user/docs /home/user/docs \
  --bind /home/user/projects/output /home/user/projects/output \
  --setenv HOME /home/user \
  npx -y @modelcontextprotocol/server-filesystem /home/user/projects
```

### Dangerous Command Detection

Bevor ein `execute_command`-Tool-Call durchgeht, wird das Command gegen eine Pattern-Liste
geprüft. Bei Match → Block + Audit-Log + User-Notification.

```cpp
// libs/rook-core/src/security/command_guard.cpp

// Bekannte gefährliche Patterns (statische Analyse, kein LLM):
// - "rm -rf /" (ohne Pfad-Beschränkung)
// - "curl ... | sh" (ungeprüfter Pipe-to-Shell)
// - "chmod 777" (world-writable)
// - "dd if=/dev/..." (raw device-Zugriff)
// - ":(){ :|:& };:" (fork bomb)
// - "> /dev/sda" (raw device write)
// - "wget ... -O /etc/..." (systemweite Überschreibung)
```

### Audit Log

`~/.local/share/rook/audit/audit_YYYY-MM-DD.jsonl` — JSON Lines, OpenTelemetry-kompatibel:

```json
{"ts":"...","event":"capability_grant","mcp":"filesystem","caps":["read:/home/user/projects"]}
{"ts":"...","event":"tool_call","tool":"write_file","mcp":"filesystem","result":"success"}
{"ts":"...","event":"tool_call","tool":"execute_command","mcp":"shell","result":"denied","reason":"ocap_limit:no_network"}
```

### API-Key-Sicherheit

- **Storage:** libsecret (GNOME-Keyring / KDE Wallet)
- **Config:** `${ENV:OPENAI_API_KEY}` — kein Klartext in config.json
- **Logging:** Logger maskiert API-Keys: `sk-****b3f2`
- **Memory:** `explicit_bzero` nach LLM-Call

### Privacy

- **Wakeword/STT/TTS:** Alle 100% lokal, kein Audio/Text verlässt das Gerät
- **LLM:** Cloud-Provider erhalten nur den Prompt (wie Text-Chat)
- **Mic-Indikator:** Widget in HeaderBar zeigt `🎤 aktiv` wenn Mikrofon offen
- **Keine Telemetrie ohne Opt-in:** Rook sendet niemals Nutzungsdaten
- **OpenTelemetry:** Nur lokal (OTLP-Collector auf localhost), kein Cloud-Export default

---

## Testing Strategy

### Test-Pyramide

```
         ┌──────┐
         │ E2E  │  Manuell + semi-automatisch (Dogfooding)
         ├──────┤
       ┌─┤ Int. │──┐
       │ ├──────┤  │
     ┌─┤ │ Unit │ ├─┐
     │ ├─┤      ├─┤ │
     │ │ └──────┘ │ │
     └─┴──────────┴─┘
```

### Test-Framework

- **GoogleTest 1.15** + **GoogleMock** — C++-Standard, Meson-integriert, Fixtures, Death-Tests
- **lcov/gcovr** — Coverage-Reports (Ziel: >80% Line Coverage für Core)
- Tests kompilieren als separates Binary (`rook_tests`) mit allen Core-Sources + Mock-Implementierungen

### Unit Tests — Abdeckung pro Modul

| Modul | Was getestet wird | Mock-Strategie |
|---|---|---|
| `ConversationManager` | Message-History-Manipulation, Context-Window-Trimming, Tool-Call-Loop-Korrektheit, Streaming-Chunk-Aggregation | Mock-LLM (Antwort als String), Mock-MCP (Echo-Tool) |
| `MCPClient` | JSON-RPC-Parsing, Tool-List-Parsing, Tool-Call-Request/Response, Transport-Fehler-Handling, Reconnect-Logik | Mock-Transport (String-Queue statt Subprozess) |
| `OllamaBackend` | Request-Building, SSE-Stream-Parsing, Error-Response-Handling | Mock-HTTP (HTTP-VCR-Pattern: einmal recorden, dann replayen) |
| `OpenAIBackend` | wie Ollama | HTTP-VCR |
| `PluginRegistry<T>` | Register/Unregister/Lookup/Einzigartigkeit, Default-Selection | Mock-Interface |
| `PluginLoader` | dlopen/dlsym-Erfolgspfade, Version-Check-Matrix (alle Kombinationen major/minor/patch), Error-Pfade (fehlende Symbole, falsche Kategorie) | Test-.so-Plugins (pre-built, kompiliert als Teil der Test-Suite) |
| `PermissionManager` | Whitelist-Matching (exakt, Prefix, Wildcard), Risk-Level-Klassifikation | Keine Mocks nötig (pure Logic) |
| `CommandGuard` | Gefährliche-Pattern-Erkennung (alle oben gelisteten + Edge-Cases wie Escaping, Unicode-Tricks) | Keine Mocks (pure Logic) |
| `Config` | GSettings-Fallback, JSON-Parsing (gültig/ungültig/leer), Missing-Key-Defaults, ENV-Variable-Expansion | Fake-GSettings via `GSETTINGS_SCHEMA_DIR` |
| `HistoryManager` | Save/Load-Roundtrip, Index-JSON-Integrität, Corrupt-File-Recovery | Temp-Verzeichnis pro Test (`mkdtemp`) |
| `AudioPipeline` | State-Machine-Transitionen (alle erlaubten + verbotene), Mute-Toggle, Error-State-Recovery | Mock-Audio-Device (Fake-Capture-Callback) |
| `WakewordEngine` | Porcupine-Init (gültiger/ungültiger Key), Process-Rückgaben (kein Wakeword, Wakeword-Index, Error) | Fake-Porcupine (Prebuilt Test-Modell mit controlled Input) |
| `ThreadSafeQueue<T>` | Push/Pop-Order, Thread-Safety (2 Producer, 1 Consumer Stress-Test), Drain-While-Pushing | Keine Mocks (concurrency test) |
| `MarkdownRenderer` | Pango-Markup-Output für: bold, italic, code, codeblock, lists, links, headers | Keine Mocks (pure String→String) |
| `MessageWidget` | Label-Content nach set_message(), Avatar-Icon pro Role, Tool-Call-Darstellung | GTK-Headless (kein Display-Server nötig in CI) |

### Integration Tests

| Test | Beschreibung |
|---|---|
| `Conversation + MCP (filesystem)` | Echter MCP filesystem-Server im Subprozess: read_file, write_file, list_directory → Roundtrip |
| `Conversation + MCP (fetch)` | Echter fetch-Server: URL aufrufen → page content zurück |
| `Multi-MCP-Server` | Zwei Server gleichzeitig, Tool-Namespace-Konflikte? |
| `AudioPipeline + WakewordEngine` | Audio-File (hey_rook.wav) replayen → Wakeword-Erkennung verifizieren |
| `STT Pipeline` | Audio-File (german_speech.wav) → whisper.cpp Subprozess → Text-Korrektheit |
| `Config Loading` | Echte config.json-Dateien laden, Schema-Validierung |
| `Plugin Loading (multi .so)` | 3 Test-Plugins gleichzeitig laden → Registry-Korrektheit |
| `History Roundtrip` | Chat mit 50 Messages erzeugen → speichern → neu laden → identisch? |
| `LLM Real API (optional)` | Nur mit gesetztem API-Key in CI-Secrets, ansonsten skipped |

### Test-Fixtures

```
tests/
├── fixtures/
│   ├── configs/
│   │   ├── minimal.json       # Nur Provider + Model
│   │   ├── full.json          # Alle Optionen
│   │   ├── broken.json        # Ungültige JSON (testet Error-Pfad)
│   │   └── env_vars.json      # Mit ${ENV:VAR}-Platzhaltern
│   ├── audio/
│   │   ├── hey_rook.wav       # Aufnahme von "Hey Rook" (16kHz mono)
│   │   ├── silence.wav        # 3 Sekunden Stille
│   │   └── german_speech.wav  # "Wie spät ist es?" (16kHz mono)
│   ├── chats/
│   │   ├── sample_chat.json   # Chat mit 10 Messages
│   │   └── corrupt_chat.json  # Kaputte JSON (testet Recovery)
│   └── plugins/
│       ├── mock_llm.so        # Mock LLM Backend
│       ├── mock_wakeword.so   # Mock Wakeword Engine
│       └── mock_hook.so       # Mock Conversation Hook
├── unit/
│   ├── test_conversation.cpp
│   ├── test_mcp_client.cpp
│   ├── test_plugin_registry.cpp
│   ├── test_plugin_loader.cpp
│   ├── test_permission_manager.cpp
│   ├── test_command_guard.cpp
│   ├── test_history_manager.cpp
│   ├── test_config.cpp
│   ├── test_audio_pipeline.cpp
│   ├── test_thread_safe_queue.cpp
│   └── test_markdown_renderer.cpp
├── integration/
│   ├── test_conversation_mcp.cpp
│   ├── test_audio_pipeline_e2e.cpp
│   ├── test_plugin_loading.cpp
│   ├── test_config_loading.cpp
│   └── test_history_roundtrip.cpp
└── ui/
    ├── test_chat_view.cpp
    └── test_message_widget.cpp
```

### Test-Plugins (pre-built für Tests)

```
tests/fixtures/plugins/
├── meson.build               # Baut alle .so Test-Plugins
├── mock_llm.c                # LLM-Backend das "Hello from mock" returned
├── mock_wakeword.c           # Wakeword das nach 100 Frames "erkannt" returned
└── mock_hook.c               # Hook der jede Message logged
```

### Test-CI-Konfiguration

```yaml
# .github/workflows/test.yml
- name: Unit + Integration Tests
  run: |
    meson setup build
    meson compile -C build
    GSETTINGS_SCHEMA_DIR=build/data \
    meson test -C build --print-errorlogs --suite unit
    meson test -C build --print-errorlogs --suite integration

- name: Coverage
  run: |
    ninja -C build coverage
    gcovr build/ --xml -o coverage.xml

- name: Upload to CodeCov
  uses: codecov/codecov-action@v4
  with:
    files: coverage.xml
```

---

## CI/CD

### GitHub Actions Workflows

**CI (pro Commit + PR):**

```yaml
name: CI
on: [push, pull_request]

jobs:
  build-and-test:
    strategy:
      matrix:
        config:
          - { os: ubuntu-24.04, cc: gcc-14,    cxx: g++-14 }
          - { os: ubuntu-24.04, cc: clang-18,   cxx: clang++-18 }
    runs-on: ${{ matrix.config.os }}
    steps:
      - uses: actions/checkout@v4
      - uses: DeterminateSystems/nix-installer-action@main
      - run: nix develop --command bash -c "
               meson setup build
               && ninja -C build
               && meson test -C build --print-errorlogs
               "

  lint:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: DeterminateSystems/nix-installer-action@main
      - run: nix develop --command bash -c "
               clang-format --dry-run --Werror src/**/*.cpp src/**/*.hpp
               && clang-tidy src/**/*.cpp -- -std=c++20
               "

  flatpak-lint:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: flatpak-builder-lint manifest io.github.fleischerdesign.Rook.yml
```

**Release (pro Tag `v*`):**

```yaml
name: Release
on:
  push:
    tags: ['v*']

jobs:
  release:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: DeterminateSystems/nix-installer-action@main

      - name: Build Flatpak
        run: flatpak-builder build-dir io.github.fleischerdesign.Rook.yml --force-clean

      - name: Build Nix Package
        run: nix build .#rook

      - name: Generate Changelog
        uses: orhun/git-cliff-action@v4
        with:
          args: --latest --strip all

      - name: Create Release
        uses: softprops/action-gh-release@v2
        with:
          body_path: CHANGELOG.md
          files: |
            result/
            rookie.flatpak
```

### Pre-commit Hooks (lokal)

`.pre-commit-config.yaml`:
```yaml
repos:
  - repo: local
    hooks:
      - id: clang-format
        name: clang-format
        entry: clang-format --dry-run --Werror
        language: system
        files: \.(cpp|hpp|c|h)$
        stages: [pre-commit]

      - id: clang-tidy
        name: clang-tidy
        entry: clang-tidy
        language: system
        files: \.cpp$
        stages: [pre-commit]

      - id: meson-test
        name: meson-test
        entry: meson test -C build
        language: system
        pass_filenames: false
        stages: [pre-push]
```

### Git-Workflow

- **Trunk:** `main` (immer deployable)
- **Branches:** `feat/beschreibung`, `fix/beschreibung`, `chore/beschreibung`
- **Commits:** [Conventional Commits](https://www.conventionalcommits.org/) — `feat:`, `fix:`, `chore:`, `docs:`, `test:`, `refactor:`
- **Merge:** Squash-Merge in `main` (ein Commit pro Feature)
- **Tags:** `v0.1.0`, `v0.2.0` (Semver, manuell gesetzt)
- **Changelog:** Generiert aus Conventional-Commits via `git-cliff`

### Code-Review-Policy

- ≥1 Approver bevor Merge (später: CODEOWNERS)
- Alle CI-Checks müssen grün sein
- Coverage darf nicht sinken (CodeCov Check)
- clang-tidy: keine neuen Warnings
- Keine Merge-Commits im `main`-Branch (nur Squash)

---

## Phasen-Plan (detailliert)

### Phase 1: Foundation — Hexagon + Skeleton

**Ziel:** `librook-core` als Bibliothek mit Port-Interfaces, EventBus, Meson + flake.nix, GTK4-Skeleton

- [ ] `flake.nix` mit Dev-Shell (alle Dependencies inkl. gRPC, protobuf)
- [ ] `meson.build`: librook-core als `shared_library`, rook-gui als `executable`
- [ ] `proto/rook/v1/service.proto`: gRPC-Service-Definition (erste Version)
- [ ] `libs/rook-core/include/rook/ports/`: Alle Port-Interfaces definiert (pure virtual)
- [ ] `libs/rook-core/include/rook/domain/events.hpp`: Alle Domain-Events
- [ ] `libs/rook-core/include/rook/domain/event_bus.hpp`: EventBus (pub/sub, thread-safe)
- [ ] `libs/rook-core/src/domain/event_bus.cpp`: Implementierung
- [ ] `src/rook-gui/main.cpp`: Gtk::Application, Fenster, HeaderBar
- [ ] `src/rook-gui/application.cpp`: Tray-Icon, EventBus-Integration
- [ ] `src/rook-gui/window.cpp`: Gtk::Stack mit "chat"- und "settings"-Seite
- [ ] `src/rook-gui/views/chat_view.cpp`: Leere Message-Liste + Input-Entry
- [ ] `po/de.po`, `po/en.po`: Deutsch + Englisch Übersetzungen
- [ ] `data/io.github.fleischerdesign.Rook.gschema.xml`: Minimales Schema
- [ ] App-Icon, Desktop-File
- [ ] `nix develop` → `meson setup build && ninja -C build` → App startet

**Erwartetes Ergebnis:** Fenster mit HeaderBar ("Rook"), EventBus läuft im Hintergrund, Port-Interfaces definiert, aber noch keine Adapter. Grundgerüst für TDD.

---

### Phase 2: Domain-Core — Conversation + LLM + Event-Loop

**Ziel:** ConversationManager, LLM-Port-Adapter, Agent-Engine via EventBus

- [ ] `libs/rook-core/src/adapters/llm/openai_adapter.cpp`: Implementiert LLMPort, SSE-Streaming
- [ ] `libs/rook-core/src/adapters/llm/ollama_adapter.cpp`: Implementiert LLMPort
- [ ] `libs/rook-core/src/adapters/llm/anthropic_adapter.cpp`: Implementiert LLMPort
- [ ] `libs/rook-core/src/domain/conversation.cpp`: Message-History, Context-Window
- [ ] `libs/rook-core/src/domain/agent.cpp`: AgentEngine — Event-basierter Tool-Call-Loop
- [ ] `libs/rook-core/src/adapters/store/json_store.cpp`: Chat-Historie speichern/laden
- [ ] `src/rook-gui/views/chat_view.cpp`: EventBus-Subscriber für LLMStreamChunk
- [ ] `src/rook-gui/views/message_widget.cpp`: Chat-Blase mit Pango-Markup
- [ ] `src/rook-gui/views/chat_sidebar.cpp`: Chat-Liste
- [ ] Settings-Dialog: Provider, Model, API-Key, System-Prompt
- [ ] First-Run Wizard: Schritt 1 (LLM-Provider)
- [ ] Unit-Tests: ConversationManager, EventBus, OpenAI-Adapter (HTTP-VCR)

**Erwartetes Ergebnis:** Text-Chat funktioniert via EventBus. User tippt → UserInputReceived → LLM-Adapter → LLMStreamChunk → UI rendert.

---

### Phase 3: MCP Tools + Ocap Security

**Ziel:** Tool-Port-Adapter (MCP-Client), Capability-System, Permission-UI

- [ ] `libs/rook-core/src/adapters/mcp/stdio_transport.cpp`: Subprozess-Transport
- [ ] `libs/rook-core/src/adapters/mcp/mcp_client.cpp`: JSON-RPC MCP Client
- [ ] `libs/rook-core/include/rook/security/capability.hpp`: Capability-Klasse
- [ ] `libs/rook-core/src/security/security_manager.cpp`: Capability-Grant + bwrap
- [ ] `libs/rook-core/src/security/command_guard.cpp`: Dangerous-Command-Detection
- [ ] Agent-Engine: Tool-Call-Loop (ToolCallRequested → ToolPort → ToolCallCompleted)
- [ ] Permission-UI: Confirm-Dialog ("Rook möchte X" → Allow/Deny/Immer)
- [ ] `src/rook-gui/views/mcp_config_dialog.cpp`: MCP-Server + Capabilities verwalten
- [ ] Test mit filesystem + fetch MCP-Servern
- [ ] Unit-Tests: MCP-Client, Capability, CommandGuard, SecurityManager

**Erwartetes Ergebnis:** MCP-Tools funktionieren mit Ocap-Security. Jeder Server hat präzise Capability-Grenzen via bwrap.

---

### Phase 4: Voice Pipeline — Audio-Port-Adapter

**Ziel:** Wakeword, STT, TTS als Audio-Port-Adapter, Audio State Machine via EventBus

- [ ] `vendor/miniaudio.h`, `vendor/porcupine/`
- [ ] `libs/rook-core/src/adapters/audio/audio_device_miniaudio.cpp`: AudioPort
- [ ] `libs/rook-core/src/adapters/audio/wakeword_porcupine.cpp`: Wakeword-Adapter
- [ ] `libs/rook-core/src/adapters/audio/stt_whisper.cpp`: STT-Adapter
- [ ] `libs/rook-core/src/adapters/audio/tts_piper.cpp`: TTS-Adapter
- [ ] `libs/rook-core/src/domain/audio_pipeline.cpp`: State Machine (events: AudioWakeDetected → AudioStateChanged)
- [ ] `src/rook-gui/views/voice_indicator.cpp`: EventBus-Subscriber
- [ ] First-Run Wizard: Schritt 2-4 (Wakeword, TTS, Whisper)
- [ ] Unit-Tests: AudioPipeline-State-Transitions (Mock-Audio-Device)
- [ ] Integrations-Tests: Audio-File-Replay

**Erwartetes Ergebnis:** "Hey Rook" → AudioWakeDetected → AudioStateChanged(LISTENING) → STT → UserInputReceived (EventBus) → ... → TTS spricht.

---

### Phase 5: gRPC-Server + Multi-Frontend

**Ziel:** `rookd` Daemon, gRPC-API, rook-tui, rook-web

- [ ] `libs/rook-core/src/adapters/server/grpc_service.cpp`: gRPC-Service (Chat, Delegate, Sync)
- [ ] `libs/rook-core/src/adapters/server/gateway.cpp`: gRPC-Gateway → REST
- [ ] `src/rookd/main.cpp`: Server Entry-Point, Config, systemd-Unit
- [ ] `src/rookd/server.cpp`: gRPC-Server + REST-Gateway starten
- [ ] `src/rook-tui/main.cpp`: FTXUI App, implementiert UserOutputPort + UserInputPort
- [ ] `src/rook-tui/tui_frontend.cpp`: EventBus → Terminal-Rendering
- [ ] `web/rook-web/`: React-App mit gRPC-Web Client
- [ ] Client-Server-Auth (JWT via gRPC Interceptor)
- [ ] Unit-Tests: gRPC-Service (Mock-LLM, Mock-MCP)
- [ ] Integrationstests: rook-gui connected to rookd

**Erwartetes Ergebnis:** rookd läuft als Daemon. rook-gui, rook-tui, rook-web connecten via gRPC. Alle drei Frontends funktionieren.

---

### Phase 6: CRDT Sync + OpenTelemetry + Rook-to-Rook

**Ziel:** Multi-Client-Sync, CRDT, OTLP-Export, Task-Delegation

- [ ] `libs/rook-core/include/rook/sync/hlc.hpp`: Hybrid Logical Clock
- [ ] `libs/rook-core/src/sync/crdt_chat.cpp`: YATA-CRDT für Chat-Nachrichten
- [ ] `libs/rook-core/src/sync/crdt_extensions.cpp`: AWSet für Extensions
- [ ] `libs/rook-core/src/sync/crdt_settings.cpp`: LWW-Map für Settings
- [ ] `libs/rook-core/src/sync/sync_engine.cpp`: Push/Merge, Offline-Queue
- [ ] `libs/rook-core/src/adapters/telemetry/otlp_exporter.cpp`: OTLP/gRPC Exporter
- [ ] EventBus-OTel-Integration: Auto-Spans pro Event-Typ
- [ ] Rook-to-Rook Task-Delegation via gRPC DelegateTask
- [ ] P2P Discovery (mDNS/Avahi) für lokales Netzwerk
- [ ] Deterministic Simulation Tests: CRDT-Korrektheit, Konvergenz, Partition-Toleranz
- [ ] Integrationstests: Multi-Client-Sync-Szenarien

**Erwartetes Ergebnis:** Zwei Rook-Instanzen syncen Chat-Historie, Extensions, Settings. DST-Tests bestehen (Konvergenz garantiert, keine Datenverluste).

---

### Phase 7: Polish — Produktreif

**Ziel:** Stabil, getestet, verteilbar, dokumentiert

- [ ] Ollama-Model-Management-UI (Liste installierter Modelle, Pull neuer Modelle, Delete)
- [ ] Wakeword-Import-Dialog (.ppn-Datei auswählen, Sensitivity testen via "Test-Button" mit Mikrofon)
- [ ] Mehrere Wakewords parallel aktivierbar ("Hey Rook" + optionales zweites Keyword)
- [ ] Chat-Management: Löschen, Umbenennen, Exportieren (JSON), Importieren (JSON)
- [ ] libsecret vollständig integriert (alle API-Keys, keine Klartext-Speicherung)
- [ ] OpenTelemetry Dashboard (lokal, Jaeger/Grafana via docker-compose)
- [ ] `rookd` Docker-Image + docker-compose.yml
- [ ] `rookd` NixOS-Modul (services.rookd)
- [ ] Flatpak-Manifest (`io.github.fleischerdesign.Rook.yml`)
- [ ] Nix-Package für alle Komponenten
- [ ] Extension Store / Registry (optional, minimal)
- [ ] README.md mit Screenshots, Architektur-Diagramm, Quickstart
- [ ] CONTRIBUTING.md (hexagonale Struktur erklärt, wie man Adapter/Ports hinzufügt)
- [ ] Error-Handling komplett:
  - Audio-Device-Wechsel (Hotplug) → automatisch neues Device erkennen
  - API-Down → "Keine Verbindung zu OpenAI" Inline-Banner mit Retry-Timer
  - Modell nicht gefunden → Download-Button im Settings-Dialog
  - Porcupine-Key ungültig → Link zur Picovoice Console mit Setup-Anleitung
  - whisper Timeout → "Spracherkennung fehlgeschlagen — bitte wiederholen"
- [ ] Accessibility: ATK-Support in GTK4-Widgets
- [ ] Performance-Benchmarks (Startup-Zeit, RAM, LLM-Latency)
- [ ] Vollständige Test-Coverage (Unit + Integration + Sync-DST > 80%)
- [ ] `.clang-format` + `.clang-tidy` Konfiguration finalisiert

---

## Dependencies — Vollständige Liste

### Nix-Pakete (Build + Runtime)

```
# Build-Toolchain
meson ninja pkg-config gettext desktop-file-utils
gcc14 clang-tools gdb valgrind

# gRPC + Protobuf
grpc protobuf grpc-gateway buf

# OpenTelemetry
cpp-otel-sdk opentelemetry-cpp

# GTK4 + GNOME + TUI
gtkmm4 libadwaita libayatana-appindicator ftxui

# Core
nlohmann_json libcurl libsecret spdlog

# Voice (Runtime, von App als Subprozess genutzt)
piper-tts whisper-cpp ollama

# Testing
gtest gcovr lcov

# Vendor (manuell zu beschaffen, nicht in nixpkgs)
# - miniaudio.h    → https://miniaud.io
# - Porcupine .so  → https://github.com/Picovoice/porcupine (Access Key nötig)
```

### Vendor-Ordner (manuell, per Script geladen)

```
vendor/
├── miniaudio.h
└── porcupine/
    ├── include/pv_porcupine.h
    └── lib/linux/x86_64/libpv_porcupine.so
```

---

## Coding-Konventionen

- **Sprache:** C++20
- **Formatierung:** `.clang-format` (WebKit-basiert, 4-space indent, 100 col)
- **Static Analysis:** `.clang-tidy` (modernize-, performance-, bugprone- checks)
- **Naming:**
  - Klassen / Enums: `PascalCase` → `ConversationManager`, `OllamaBackend`
  - Methoden / Variablen: `snake_case` → `send_message()`, `on_wakeword_detected()`
  - Member-Variablen: `m_` prefix → `m_history`, `m_http_client`
  - Namespaces: `rook::` → `rook::audio`, `rook::llm`, `rook::mcp`
  - Constants: `k` prefix → `k_default_context_window`
- **Memory:** `std::unique_ptr` für Ownership, `Glib::RefPtr` für GTK-Objekte, kein nacktes `new`/`delete`
- **Error Handling:**
  - `std::expected<T, Error>` für recoverable Errors (LLM-API, MCP-Tool)
  - Exceptions für fatale Fehler (Config-Parse-Fehler beim Start)
  - `Glib::Error` für GTK-spezifische Fehler
- **Async:** `std::async` + `Glib::Dispatcher` für Thread-übergreifende UI-Updates
- **Logging:** `spdlog` → `SPDLOG_DEBUG`, `SPDLOG_INFO`, `SPDLOG_ERROR`
- **i18n:** `_("Text")` Makro für alle UI-Strings, `ngettext()` für Plurale
- **Includes:** System-Headers mit `<>`, Projekt-Headers mit `""`

---

## Ressourcen & Referenzen

| Ressource | URL |
|---|---|
| Porcupine C SDK | https://github.com/Picovoice/porcupine |
| whisper.cpp | https://github.com/ggerganov/whisper.cpp |
| piper → piper1-gpl | https://github.com/OHF-Voice/piper1-gpl |
| Piper Voice Models | https://huggingface.co/rhasspy/piper-voices |
| miniaudio | https://miniaud.io |
| gtkmm 4.0 Docs | https://gnome.pages.gitlab.gnome.org/gtkmm/ |
| libadwaita Docs | https://gnome.pages.gitlab.gnome.org/libadwaita/ |
| FTXUI | https://github.com/ArthurSonzogni/FTXUI |
| gRPC C++ | https://grpc.io/docs/languages/cpp/ |
| Protobuf | https://protobuf.dev/ |
| gRPC-Gateway | https://github.com/grpc-ecosystem/grpc-gateway |
| Buf | https://buf.build |
| OpenTelemetry C++ | https://opentelemetry.io/docs/languages/cpp/ |
| MCP Specification | https://spec.modelcontextprotocol.io/ |
| YATA CRDT (Yjs) | https://docs.yjs.dev/ |
| Hybrid Logical Clock | https://cse.buffalo.edu/tech-reports/2014-04.pdf |
| Object Capabilities | https://en.wikipedia.org/wiki/Object-capability_model |
| Bubblewrap | https://github.com/containers/bubblewrap |
| Newelle (Referenz) | https://github.com/qwersyk/Newelle |
| Alpaca (Referenz) | https://github.com/Jeffser/Alpaca |
| Ollama API | https://github.com/ollama/ollama/blob/main/docs/api.md |
| libayatana-appindicator | https://github.com/AyatanaIndicators/libayatana-appindicator |
| FTXUI | https://github.com/ArthurSonzogni/FTXUI |
