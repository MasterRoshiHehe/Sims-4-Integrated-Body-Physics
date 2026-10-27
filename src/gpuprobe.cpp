// Read-only research log of how the game draws Sims (user 2026-10-16 19:14Z:
// develop the bulge and wave features; 19:16Z: the butt too). Both need the
// game's skinned vertex shaders, so this first step only looks: it changes no
// drawing.
//
// On when integratedbodyphysics.ini / integratedbodyphysics_user.ini has shader_probe = 1 at
// game start. It swaps a few entries in Direct3D 11's own vtables (found on a
// small device of our own, the same classes the game's device uses):
//   device  CreateInputLayout (11), CreateVertexShader (12), CreateDeferredContext (27),
//           CreateTexture2D (5) and CreateShaderResourceView (7) (only to meet the game's device)
//   context VSSetConstantBuffers (7), VSSetShader (11), DrawIndexed (12),
//           IASetInputLayout (17), DrawIndexedInstanced (20): these by patching
//           the functions' code (MinHook, 0.11.10), as the game's contexts carry
//           their own vtable copies
// Sims 4 VR code-patches Present, OMSetRenderTargets and ClearDepthStencilView,
// none of these.
//
// What it writes:
// - integratedbodyphysics.log: every vertex shader that is drawn with bone inputs
//   (BLENDINDICES): its id, size, inputs, outputs (from the shader's own
//   signature chunks), the constant buffer sizes bound with it, and how often
//   it is drawn; a summary every 20 s.
// - integratedbodyphysics_shaders.bin (shipped empty, so the mod manager keeps it in the
//   mod's folder): those shaders' bytecode, each after a 16-byte header
//   ("CJSH", id, size, 0), for reading offline.
#include "gpuprobe.h"
#include "wavepatch.h"

#include <windows.h>
#include <d3d11_1.h>
#include <MinHook.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gpuprobe {
namespace {

Log g_log = nullptr;
std::mutex g_capMutex;  // mode 2 capture state
std::unordered_map<const void*, unsigned> g_skinnedVs;  // vertex shaders with bone inputs -> probe id

// Mode 6, the breast wave test (wavepatch.cpp): the game's skinned shaders are created patched; when one is
// bound with a Sim's bone block (row 3 still the (0,0,0,1) padding), row 3 gets her breast bones' slots and the
// wave. Plain CPU stores into the block the game just wrote; no GPU command of ours.
// The test's fixed wave: metres (as seen, after her breast scale), cycles per second. 0.11.19 used 1.5 cm before her
// breast scale (~4 cm seen) and broke chests (user 2026-10-17 14:42Z).
constexpr float kWaveAmp = 0.03f, kWaveHz = 1.0f;  // 0.12.2: 1.2 cm not seen (user 2026-10-18 14:34Z)
// 0.11.20: 4 mm at 1.5 Hz was too small to see (user 15:22Z)
std::shared_mutex g_waveMutex;
std::unordered_map<const void*, unsigned> g_waveVs;  // patched shader -> its bone palette's first cb0 row
std::atomic<const void*> g_waveActor{nullptr};  // the Sim last tagged
std::atomic<unsigned> g_wavePatched{0}, g_waveRefused{0}, g_waveBinds{0}, g_waveTagged{0}, g_waveNoSim{0},
    g_waveNoSlot{0}, g_waveSeen{0};
struct WaveSim {
    const void* actor = nullptr;
    float pos[3] = {};
    float det[2] = {};
    float toYou = 1e9f;  // metres from your Sim: only the nearest one gets the wave (one Sim per test)
    ULONGLONG tick = 0;
};
std::mutex g_simMutex;
WaveSim g_sims[32];
thread_local const void* t_vs = nullptr;                      // the vertex shader set on this thread's context
struct Mapped {
    void* data;
    UINT size;
};
thread_local std::unordered_map<const void*, Mapped> t_mapped;  // big constant buffer -> its mapping on this thread
thread_local const unsigned char* t_okFrom = nullptr;          // last region found writable
thread_local size_t t_okSize = 0;
double g_qpcHz = 1;
std::atomic<int> g_pending{0};  // copies waiting to be read back
int g_mode = 0;  // 1: draw statistics + shader dump, 2: retired, 3: CPU palette read, 4: upload order stats,
                  // 5: can the block be found at Unmap (before its bind),
                  // 6: breast wave test (patched shaders, a fixed wave written into cb0 row 3)
std::wstring g_folder;
std::mutex g_mutex;

struct Shader {
    unsigned id = 0;
    std::vector<unsigned char> code;
    std::string outputs;
    bool skinnedSeen = false;  // drawn with bone inputs at least once
    bool dumped = false;
    unsigned long long draws = 0;
    unsigned long long indices = 0;
    std::string buffers;  // VS constant buffer sizes seen with it
};
std::unordered_map<const void*, Shader> g_shaders;  // ID3D11VertexShader* -> what we know
std::unordered_map<const void*, std::string> g_layouts;  // ID3D11InputLayout* -> semantics
std::unordered_set<const void*> g_skinnedLayouts;
unsigned g_nextId = 1;

struct ContextState {
    const void* shader = nullptr;
    const void* layout = nullptr;
    unsigned cbSizes[4] = {};
};
std::unordered_map<const void*, ContextState> g_contexts;
std::atomic<unsigned long long> g_drawsAll{0}, g_drawsSkinned{0};
ULONGLONG g_lastSummary = 0;

using CreateInputLayoutFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_INPUT_ELEMENT_DESC*, UINT,
                                                         const void*, SIZE_T, ID3D11InputLayout**);
using CreateVertexShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                                                          ID3D11VertexShader**);
using VSSetConstantBuffersFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*);
using VSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11VertexShader*, ID3D11ClassInstance* const*,
                                               UINT);
using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
using IASetInputLayoutFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11InputLayout*);
using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);

using CreateDeferredContextFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, UINT, ID3D11DeviceContext**);
CreateInputLayoutFn g_createInputLayout = nullptr;
CreateVertexShaderFn g_createVertexShader = nullptr;
CreateDeferredContextFn g_createDeferredContext = nullptr;
std::atomic<unsigned> g_shadersMade{0}, g_skinnedMade{0}, g_layoutsMade{0};
std::atomic<bool> g_gameDeviceSeen{false};
bool hookContext(ID3D11DeviceContext* c, const char* what);
void onGameDevice(ID3D11Device* d);
std::unordered_set<const void*> g_hookedCode;  // context function code already hooked
std::mutex g_fnMutex;

// The signature chunks of a DXBC container (ISGN/OSGN/OSG5): "NAME0.xyzw ...".
std::string signature(const unsigned char* code, size_t size, bool output) {
    if (size < 32 || std::memcmp(code, "DXBC", 4) != 0) return "?";
    unsigned count = *reinterpret_cast<const unsigned*>(code + 28);
    std::string out;
    for (unsigned i = 0; i < count && 32 + i * 4 + 4 <= size; ++i) {
        unsigned at = *reinterpret_cast<const unsigned*>(code + 32 + i * 4);
        if (at + 8 > size) continue;
        const char* tag = reinterpret_cast<const char*>(code + at);
        bool isOut = std::memcmp(tag, "OSGN", 4) == 0 || std::memcmp(tag, "OSG5", 4) == 0;
        bool isIn = std::memcmp(tag, "ISGN", 4) == 0 || std::memcmp(tag, "ISG1", 4) == 0;
        if (output ? !isOut : !isIn) continue;
        bool five = std::memcmp(tag, "OSG5", 4) == 0;
        bool one = std::memcmp(tag, "ISG1", 4) == 0;
        const unsigned char* data = code + at + 8;
        unsigned len = *reinterpret_cast<const unsigned*>(code + at + 4);
        if (at + 8 + len > size || len < 8) continue;
        unsigned n = *reinterpret_cast<const unsigned*>(data);
        unsigned stride = one ? 32 : five ? 28 : 24;
        unsigned first = one ? 0 : five ? 4 : 0;  // OSG5 elements start with a stream index
        for (unsigned e = 0; e < n && 8 + (e + 1) * stride <= len; ++e) {
            const unsigned char* el = data + 8 + e * stride + first;
            unsigned nameAt = *reinterpret_cast<const unsigned*>(el);
            unsigned index = *reinterpret_cast<const unsigned*>(el + 4);
            unsigned reg = *reinterpret_cast<const unsigned*>(el + 16);
            unsigned char mask = el[20];
            std::string name = nameAt < len ? std::string(reinterpret_cast<const char*>(data + nameAt)) : "?";
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s%s%u(r%u,%s%s%s%s)", out.empty() ? "" : " ", name.c_str(), index, reg,
                          mask & 1 ? "x" : "", mask & 2 ? "y" : "", mask & 4 ? "z" : "", mask & 8 ? "w" : "");
            out += buf;
        }
    }
    return out.empty() ? "-" : out;
}

void dump(Shader& s) {
    if (s.dumped || s.code.empty()) return;
    s.dumped = true;
    FILE* f = _wfopen((g_folder + L"integratedbodyphysics_shaders.bin").c_str(), L"ab");
    if (!f) return;
    unsigned header[4] = {0x48534a43u /* "CJSH" */, s.id, static_cast<unsigned>(s.code.size()), 0};
    std::fwrite(header, sizeof header, 1, f);
    std::fwrite(s.code.data(), 1, s.code.size(), f);
    std::fclose(f);
}

void summary() {
    ULONGLONG now = GetTickCount64();
    if (now - g_lastSummary < 20000) return;
    g_lastSummary = now;
    unsigned skinned = 0;
    for (auto& [ptr, s] : g_shaders)
        if (s.skinnedSeen) ++skinned;
    g_log("GPU probe: %llu draws, %llu with bone inputs, %u skinned shaders drawn; made since start: %u vertex "
          "shaders (%u skinned), %u layouts",
          g_drawsAll.load(), g_drawsSkinned.load(), skinned, g_shadersMade.load(), g_skinnedMade.load(),
          g_layoutsMade.load());
    for (auto& [ptr, s] : g_shaders)
        if (s.skinnedSeen)
            g_log("GPU probe:   shader %u: %llu draws, %llu indices, buffers %s", s.id, s.draws, s.indices,
                  s.buffers.empty() ? "-" : s.buffers.c_str());
}

void noteDraw(ID3D11DeviceContext* c, UINT indices) {
    g_drawsAll.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_mutex);
    auto& st = g_contexts[c];
    if (!st.layout || !g_skinnedLayouts.count(st.layout)) {
        summary();
        return;
    }
    g_drawsSkinned.fetch_add(1, std::memory_order_relaxed);
    auto it = g_shaders.find(st.shader);
    if (it == g_shaders.end()) {
        Shader& s = g_shaders[st.shader];
        s.id = g_nextId++;
        it = g_shaders.find(st.shader);
        g_log("GPU probe: shader %u drawn with bones but made before the probe (no bytecode)", s.id);
    }
    Shader& s = it->second;
    s.draws++;
    s.indices += indices;
    char sizes[64];
    std::snprintf(sizes, sizeof sizes, "[%u %u %u %u]", st.cbSizes[0], st.cbSizes[1], st.cbSizes[2], st.cbSizes[3]);
    if (s.buffers.find(sizes) == std::string::npos && s.buffers.size() < 200) s.buffers += sizes;
    if (!s.skinnedSeen) {
        s.skinnedSeen = true;
        g_log("GPU probe: skinned shader %u (%zu bytes): layout %s", s.id, s.code.size(),
              g_layouts.count(st.layout) ? g_layouts[st.layout].c_str() : "?");
        if (!s.code.empty()) {
            g_log("GPU probe:   inputs %s", signature(s.code.data(), s.code.size(), false).c_str());
            g_log("GPU probe:   outputs %s", s.outputs.c_str());
        }
        dump(s);
    }
    summary();
}

HRESULT STDMETHODCALLTYPE createInputLayout(ID3D11Device* d, const D3D11_INPUT_ELEMENT_DESC* el, UINT n,
                                            const void* code, SIZE_T size, ID3D11InputLayout** out) {
    onGameDevice(d);
    HRESULT hr = g_createInputLayout(d, el, n, code, size, out);
    g_layoutsMade.fetch_add(1);
    if (SUCCEEDED(hr) && out && *out && el) {
        std::string text;
        bool bones = false;
        for (UINT i = 0; i < n; ++i) {
            char buf[80];
            std::snprintf(buf, sizeof buf, "%s%s%u(fmt %d,slot %u,+%u)", text.empty() ? "" : " ",
                          el[i].SemanticName ? el[i].SemanticName : "?", el[i].SemanticIndex,
                          static_cast<int>(el[i].Format), el[i].InputSlot, el[i].AlignedByteOffset);
            text += buf;
            if (el[i].SemanticName && _stricmp(el[i].SemanticName, "BLENDINDICES") == 0) bones = true;
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_layouts[*out] = text;
        if (bones) g_skinnedLayouts.insert(*out);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE createVertexShader(ID3D11Device* d, const void* code, SIZE_T size, ID3D11ClassLinkage* l,
                                             ID3D11VertexShader** out) {
    onGameDevice(d);
    if (g_mode == 6 && code && out && size > 0 && size < (4u << 20)) {
        std::vector<uint8_t> patched;
        unsigned base = 0;
        if (wavepatch::patch(static_cast<const uint8_t*>(code), size, patched, &base)) {
            HRESULT hr = g_createVertexShader(d, patched.data(), patched.size(), l, out);
            if (SUCCEEDED(hr) && *out) {
                std::unique_lock<std::shared_mutex> lock(g_waveMutex);
                g_waveVs[*out] = base;
                g_wavePatched.fetch_add(1);
                return hr;
            }
            g_waveRefused.fetch_add(1);  // the device said no: the game gets its own shader
        }
    }
    HRESULT hr = g_createVertexShader(d, code, size, l, out);
    if (g_mode == 6 && SUCCEEDED(hr) && out && *out) {  // an address of a released patched shader reused
        std::unique_lock<std::shared_mutex> lock(g_waveMutex);
        g_waveVs.erase(*out);
    }
    g_shadersMade.fetch_add(1);
    if (SUCCEEDED(hr) && out && *out && code && size > 0 && size < (4u << 20)) {
        std::string inputs = signature(static_cast<const unsigned char*>(code), size, false);
        if (inputs.find("BLENDINDICES") != std::string::npos) {  // only the skinned ones are kept
            g_skinnedMade.fetch_add(1);
            std::lock_guard<std::mutex> lock(g_mutex);
            Shader& s = g_shaders[*out];
            s = Shader();
            s.id = g_nextId++;
            {
                std::lock_guard<std::mutex> capLock(g_capMutex);
                g_skinnedVs[*out] = s.id | (inputs.find("POSITION1") != std::string::npos ? 0x80000000u : 0);  // morphs: Sim bodies
            }
            s.code.assign(static_cast<const unsigned char*>(code), static_cast<const unsigned char*>(code) + size);
            s.outputs = signature(s.code.data(), size, true);
            dump(s);  // ids match the palette capture's
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        summary();
    }
    return hr;
}

void noteConstantBuffers(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11Buffer* const* b) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto& st = g_contexts[c];
    for (UINT i = 0; i < n; ++i) {
        UINT slot = start + i;
        if (slot >= 4) break;
        D3D11_BUFFER_DESC desc{};
        if (b && b[i]) b[i]->GetDesc(&desc);
        st.cbSizes[slot] = b && b[i] ? desc.ByteWidth : 0;
    }
}

// One set of detours per distinct function code (immediate and deferred contexts can differ). 0.11.8/0.11.9
// swapped vtable entries and saw no draws, so these patch the functions' code instead (MinHook).
template <int N>
struct Detours {
    static inline VSSetConstantBuffersFn setCb = nullptr;
    static inline VSSetShaderFn setShader = nullptr;
    static inline DrawIndexedFn draw = nullptr;
    static inline IASetInputLayoutFn setLayout = nullptr;
    static inline DrawIndexedInstancedFn drawInstanced = nullptr;

    static void STDMETHODCALLTYPE cb(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11Buffer* const* b) {
        setCb(c, start, n, b);
        noteConstantBuffers(c, start, n, b);
    }
    static void STDMETHODCALLTYPE shader(ID3D11DeviceContext* c, ID3D11VertexShader* s, ID3D11ClassInstance* const* ci,
                                         UINT n) {
        setShader(c, s, ci, n);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_contexts[c].shader = s;
    }
    static void STDMETHODCALLTYPE layout(ID3D11DeviceContext* c, ID3D11InputLayout* l) {
        setLayout(c, l);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_contexts[c].layout = l;
    }
    static void STDMETHODCALLTYPE indexed(ID3D11DeviceContext* c, UINT count, UINT start, INT base) {
        noteDraw(c, count);
        draw(c, count, start, base);
    }
    static void STDMETHODCALLTYPE instanced(ID3D11DeviceContext* c, UINT count, UINT instances, UINT start, INT base,
                                            UINT startInstance) {
        noteDraw(c, count * instances);
        drawInstanced(c, count, instances, start, base, startInstance);
    }
};

// Hooks one function's code unless an earlier set already did. Returns 1 hooked, 0 already, -1 failed.
template <typename Fn>
int codeHook(void* target, Fn detour, Fn* original) {
    if (!target) return -1;
    if (!g_hookedCode.insert(target).second) return 0;
    if (MH_CreateHook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original)) != MH_OK) return -1;
    return MH_EnableHook(target) == MH_OK ? 1 : -1;
}

template <int N>
void hookSet(void** vt, int result[5]) {
    using D = Detours<N>;
    result[0] = codeHook(vt[7], &D::cb, &D::setCb);
    result[1] = codeHook(vt[11], &D::shader, &D::setShader);
    result[2] = codeHook(vt[12], &D::indexed, &D::draw);
    result[3] = codeHook(vt[17], &D::layout, &D::setLayout);
    result[4] = codeHook(vt[20], &D::instanced, &D::drawInstanced);
}
void (*const kSets[])(void**, int*) = {hookSet<0>, hookSet<1>, hookSet<2>, hookSet<3>};
int g_setsUsed = 0;

// ---------------------------------------------------------------------------
// Mode 2: which bone palette slot is the breast? Capture windows of ~20 frames of the bone constant buffers
// and, from jiggle.cpp, one Sim's world transform and all her joints' model transforms. Read offline
// (jiggle/research): the slot whose matrix stays Model(breast) x constant is the breast.
// 0.11.11 watched Map/UpdateSubresource of whole 2976..3088-byte buffers and caught none, so 0.11.12 copies
// what is bound at VS slot 0 while a skinned shader is set (VSSetConstantBuffers and VSSetConstantBuffers1,
// any buffer size) into staging buffers, read back later on the immediate context without waiting, and logs
// per window how the game uploads constant buffers (Map types, UpdateSubresource, sizes).
FILE* g_capFile = nullptr;
const void* g_capActor = nullptr;
int g_capFrames = 0, g_capWindows = 0;
ULONGLONG g_capNext = 0, g_capStart = 0;
std::atomic<bool> g_capOn{false};
unsigned g_capBuffers = 0, g_capCopies = 0;
std::unordered_map<std::string, unsigned> g_capStats;  // "what" -> count, this window
std::unordered_map<const void*, const void*> g_vsNow;   // context -> vertex shader (while capturing)

struct Pending {
    ID3D11Buffer* staging = nullptr;
    unsigned meta[4] = {};  // shader id, first constant, constants, 1 = immediate context
    long long when = 0;     // QueryPerformanceCounter at the copy (0.11.13: pairs it with the Sim's frame)
    unsigned bytes = 0;
    ULONGLONG made = 0;  // a deferred context's copy runs only when its command list does: read 200+ ms later
    bool busy = false;
};
constexpr int kPool = 1024;
Pending g_pool[kPool];

void capWrite(unsigned kind, const void* data, unsigned size) {  // under g_capMutex
    if (!g_capFile) return;
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    unsigned header[2] = {0x42504a43u /* "CJPB" */, kind};
    std::fwrite(header, sizeof header, 1, g_capFile);
    std::fwrite(&t.QuadPart, 8, 1, g_capFile);
    std::fwrite(&size, 4, 1, g_capFile);
    std::fwrite(data, 1, size, g_capFile);
}

void stat(const char* fmt, unsigned a, unsigned b, unsigned c) {  // under g_capMutex
    char key[96];
    std::snprintf(key, sizeof key, fmt, a, b, c);
    g_capStats[key]++;
}

// Copies bytes [first*16, +count*16) of a bound constant buffer into a free staging buffer (under g_capMutex).
void capCopy(ID3D11DeviceContext* c, ID3D11Buffer* b, UINT first, UINT count) {
    if (g_capCopies >= kPool * 2) return;
    D3D11_BUFFER_DESC desc{};
    b->GetDesc(&desc);
    UINT from = first * 16, bytes = count ? count * 16 : desc.ByteWidth;
    if (from >= desc.ByteWidth) return;
    if (from + bytes > desc.ByteWidth) bytes = desc.ByteWidth - from;
    if (bytes > 4096) bytes = 4096;
    Pending* free = nullptr;
    for (auto& p : g_pool)
        if (!p.busy) { free = &p; break; }
    if (!free) return;
    if (!free->staging) {
        ID3D11Device* d = nullptr;
        c->GetDevice(&d);
        if (!d) return;
        D3D11_BUFFER_DESC sd{};
        sd.ByteWidth = 4096;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d->CreateBuffer(&sd, nullptr, &free->staging);
        d->Release();
        if (!free->staging) return;
    }
    D3D11_BOX box{from, 0, 0, from + bytes, 1, 1};
    c->CopySubresourceRegion(free->staging, 0, 0, 0, 0, b, 0, &box);
    auto it = g_skinnedVs.find(g_vsNow[c]);
    free->meta[0] = it == g_skinnedVs.end() ? 0 : it->second & 0x7fffffffu;
    free->made = GetTickCount64();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    free->when = t.QuadPart;
    free->meta[1] = first;
    free->meta[2] = count;
    free->meta[3] = c->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE ? 1 : 0;
    free->bytes = bytes;
    free->busy = true;
    ++g_pending;
    ++g_capCopies;
}

// On the immediate context (the render thread): reads back finished copies without waiting.
template <typename MapF, typename UnmapF>
void capDrain(ID3D11DeviceContext* c, MapF map, UnmapF unmap) {  // under g_capMutex
    if (c->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return;
    static unsigned char buf[24 + 4096];
    ULONGLONG now = GetTickCount64();
    for (auto& p : g_pool) {
        if (p.busy && now - p.made > 5000) {  // never came back (0.11.12 kept 256 forever): give it up
            p.busy = false;
            --g_pending;
        }
        if (!p.busy || now - p.made < 200) continue;
        D3D11_MAPPED_SUBRESOURCE m{};
        if (map(c, p.staging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &m) != S_OK) continue;
        std::memcpy(buf, p.meta, 16);
        std::memcpy(buf + 16, &p.when, 8);
        std::memcpy(buf + 24, m.pData, p.bytes);
        unmap(c, p.staging, 0);
        capWrite(4, buf, 24 + p.bytes);
        ++g_capBuffers;
        p.busy = false;
        --g_pending;
    }
}

// Mode 3: the last mapped address of each big constant buffer, per deferred context (a deferred context's
// mapped memory is the runtime's own copy, kept for its command list).
std::unordered_map<const void*, std::unordered_map<const void*, void*>> g_lastMap;
// Mode 4: is the game's buffer still mapped when a Sim body draw binds it? (can a CPU write there still land)
std::unordered_map<const void*, std::unordered_map<const void*, bool>> g_mappedNow;
std::unordered_map<unsigned long long, unsigned> g_bindsOf;  // (buffer offset ^ context) -> binds this window
// Mode 5: the ring's next block guessed from the binds so far (end of the furthest bound block, 16-constant
// aligned), and the guess made at each Unmap, checked against the bind that follows.
std::unordered_map<const void*, std::unordered_map<const void*, UINT>> g_nextFirst;
std::unordered_map<const void*, std::unordered_map<const void*, UINT>> g_guessAtUnmap;
std::unordered_map<const void*, std::unordered_map<const void*, int>> g_mapType;

bool readable(const void* from, size_t bytes) {
    MEMORY_BASIC_INFORMATION mi{};
    return VirtualQuery(from, &mi, sizeof mi) && mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
           (mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
           static_cast<const unsigned char*>(mi.BaseAddress) + mi.RegionSize >= static_cast<const unsigned char*>(from) + bytes;
}

void capRead(unsigned shader, UINT first, UINT count, const unsigned char* base) {  // under g_capMutex
    if (g_capCopies >= 4000) return;
    UINT bytes = count * 16;
    if (bytes > 4096) bytes = 4096;
    const unsigned char* from = base + size_t(first) * 16;
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(from, &mi, sizeof mi) || mi.State != MEM_COMMIT ||
        (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        !(mi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) ||
        static_cast<const unsigned char*>(mi.BaseAddress) + mi.RegionSize < from + bytes)
        return;
    static unsigned char buf[24 + 4096];
    unsigned meta[4] = {shader, first, count, 0};
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    std::memcpy(buf, meta, 16);
    std::memcpy(buf + 16, &t.QuadPart, 8);
    std::memcpy(buf + 24, from, bytes);
    capWrite(4, buf, 24 + bytes);
    ++g_capCopies;
    ++g_capBuffers;
}

using MapFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT,
                                          D3D11_MAPPED_SUBRESOURCE*);
using UnmapFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT);
using UpdateSubresourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*,
                                                     const void*, UINT, UINT);
using UpdateSubresource1Fn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*,
                                                      const void*, UINT, UINT, UINT);
using VSSetConstantBuffers1Fn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*,
                                                         const UINT*, const UINT*);

bool cbInfo(ID3D11Resource* r, D3D11_BUFFER_DESC* desc) {
    D3D11_RESOURCE_DIMENSION dim;
    r->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return false;
    static_cast<ID3D11Buffer*>(r)->GetDesc(desc);
    return (desc->BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0;
}

bool writable(void* at, size_t bytes) {
    const unsigned char* p = static_cast<const unsigned char*>(at);
    if (p >= t_okFrom && p + bytes <= t_okFrom + t_okSize) return true;
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT || (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
        !(mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
        return false;
    t_okFrom = static_cast<const unsigned char*>(mi.BaseAddress);
    t_okSize = mi.RegionSize;
    return p >= t_okFrom && p + bytes <= t_okFrom + t_okSize;
}

// Mode 6: a patched shader binds a block of the game's bone buffer.
// 0.12.3: a patched shader binds a block: count whether our tag is in it. (0.11.19-0.12.2 wrote the tag here,
// after the game's Unmap: a deferred context's runtime has already copied the data by then, so it never reached
// the GPU and no wave showed, user 2026-10-18 15:15Z.)
void waveBind(UINT start, UINT n, ID3D11Buffer* const* b, const UINT* first, const UINT* count) {
    if (start != 0 || !n || !b || !b[0] || !first || !count || !t_vs) return;
    unsigned base;
    {
        std::shared_lock<std::shared_mutex> lock(g_waveMutex);
        auto it = g_waveVs.find(t_vs);
        if (it == g_waveVs.end()) return;
        base = it->second;
    }
    g_waveBinds.fetch_add(1, std::memory_order_relaxed);
    if (count[0] < base + 180) return;
    auto at = t_mapped.find(b[0]);
    if (at == t_mapped.end() || !at->second.data) return;
    const float* blk =
        reinterpret_cast<const float*>(static_cast<unsigned char*>(at->second.data) + size_t(first[0]) * 16);
    if (!readable(blk, 64)) return;
    uint32_t x;
    std::memcpy(&x, blk + 12, 4);
    if (x >= wavepatch::kSlotTag && x < wavepatch::kSlotTag + 60) g_waveSeen.fetch_add(1, std::memory_order_relaxed);
}

// The Sim the wave goes to, looked up once per Unmap (0.12.4: 0.12.3 locked and searched every Sim for each of
// ~250k blocks a second: the game's frame went from ~8 to ~32 ms, user 2026-10-18 20:09Z).
struct WaveTarget {
    const void* actor = nullptr;
    float pos[3] = {};
    float det[2] = {};
};
bool waveTarget(WaveTarget& t) {
    ULONGLONG now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_simMutex);
    const WaveSim* she = nullptr;  // the jiggling Sim nearest your Sim
    for (const WaveSim& s : g_sims)
        if (s.actor && now - s.tick <= 1000 && (!she || s.toYou < she->toYou)) she = &s;
    if (!she || (she->det[0] <= 0 && she->det[1] <= 0)) return false;
    auto closeBy = [&](const WaveSim& s) {
        float dx = she->pos[0] - s.pos[0], dy = she->pos[1] - s.pos[1], dz = she->pos[2] - s.pos[2];
        return dx * dx + dy * dy + dz * dz < 0.6f * 0.6f;
    };
    for (const WaveSim& s : g_sims)  // another Sim right by her with a breast size close to hers: no wave
        if (&s != she && s.actor && now - s.tick <= 1000 && closeBy(s))
            for (float x : s.det)
                for (float y : she->det)
                    if (x > 0 && y > 0 && std::fabs(x - y) < 0.03f * y) return false;
    t.actor = she->actor;
    std::memcpy(t.pos, she->pos, sizeof t.pos);
    std::memcpy(t.det, she->det, sizeof t.det);
    return true;
}

// Her bone block (bones from cb0 row `base`) gets the wave tag in row 3. False: no breast bone of hers in it.
bool waveTag(float* blk, unsigned base, const WaveTarget& she) {
    float* row3 = blk + 12;
    // Her breast bones' slots: their skin matrices carry her CAS breast scale (det = its product).
    int slots[2] = {-1, -1}, found = 0;
    for (int k = 0; k < 60 && found < 2; ++k) {
        const float* m = blk + size_t(base + 3 * k) * 4;
        float d = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
                  m[2] * (m[4] * m[9] - m[5] * m[8]);
        for (float want : she.det)
            if (want > 0 && std::fabs(d - want) < 0.005f * want) {
                slots[found++] = k;
                break;
            }
    }
    if (!found) return false;
    if (found == 1) slots[1] = slots[0];
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    float phase = static_cast<float>(std::fmod(t.QuadPart / g_qpcHz * kWaveHz, 1.0) * 6.283185307179586);
    // The offset is added before skinning, so her breast bone's scale multiplies it: divide that out.
    const float* m = blk + size_t(base + 3 * slots[0]) * 4;
    float d = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
              m[2] * (m[4] * m[9] - m[5] * m[8]);
    float amp = kWaveAmp / std::cbrt(d);
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 30)
        g_log("GPU probe: wave tag: palette base %u, slots %d/%d, det %.3f, amplitude before skinning %.2f mm", base,
              slots[0], slots[1], d, amp * 1000);
    uint32_t v[4];
    v[0] = static_cast<uint32_t>(slots[0]) + wavepatch::kSlotTag;
    v[1] = static_cast<uint32_t>(slots[1]) + wavepatch::kSlotTag;
    std::memcpy(&v[2], &amp, 4);
    std::memcpy(&v[3], &phase, 4);
    std::memcpy(row3, v, 16);
    g_waveTagged.fetch_add(1, std::memory_order_relaxed);
    g_waveActor.store(she.actor, std::memory_order_relaxed);
    return true;
}

// Before the game's Unmap of a big constant buffer on a deferred context (its writes are done, the runtime
// copies it next): each 256-byte block that looks like a Sim's bone block (row 3 = 0 0 0 1) placed within
// 0.3 m of her is tried with both bone bases the patched shaders use. Blocks already tried and unchanged are
// skipped; the key holds her bone rows too (0.12.3 keyed on the world matrix only, so her hair and clothes
// blocks, which never have her breast bones, blocked the slots her body block came to later: 126 tags).
struct WaveSeen {
    float key[256][4];
};
thread_local std::unordered_map<const void*, WaveSeen> t_waveSeen;
std::atomic<bool> g_waveOff{false};        // the safety switch: the scan cost too much frame time
std::atomic<long long> g_waveScanTicks{0};  // QPC ticks spent scanning since the last log line
void waveScan(const void* r, void* data, UINT size) {
    if (g_waveOff.load(std::memory_order_relaxed)) return;
    if (!data || size < 4096 || size > 65536 || !writable(data, size)) return;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    WaveTarget she;
    if (waveTarget(she)) {
        WaveSeen& seen = t_waveSeen[r];
        for (UINT at = 0; at + 64 <= size; at += 256) {
            float* blk = reinterpret_cast<float*>(static_cast<unsigned char*>(data) + at);
            const float* row3 = blk + 12;
            if (!(row3[0] == 0.f && row3[1] == 0.f && row3[2] == 0.f && row3[3] == 1.f)) continue;
            float dx = blk[3] - she.pos[0], dy = blk[7] - she.pos[1], dz = blk[11] - she.pos[2];
            if (dx * dx + dy * dy + dz * dz >= 0.3f * 0.3f) continue;
            g_waveNoSim.fetch_add(1, std::memory_order_relaxed);  // blocks of hers looked at
            float* key = seen.key[at / 256];
            float now[4] = {blk[3], blk[0], at + 128 <= size ? blk[16] : 0.f, at + 256 <= size ? blk[52] : 0.f};
            if (std::memcmp(key, now, sizeof now) == 0) continue;  // the same block as last time: already tried
            std::memcpy(key, now, sizeof now);
            bool tagged = false;
            for (unsigned base : {4u, 8u})
                if (!tagged && at + size_t(base + 180) * 16 <= size) tagged = waveTag(blk, base, she);
            if (tagged) key[0] = std::nanf("");  // the game rewriting it (row 3 back to 0 0 0 1) is looked at again
        }
    }
    QueryPerformanceCounter(&t1);
    g_waveScanTicks.fetch_add(t1.QuadPart - t0.QuadPart);
    // Safety switch: over 3% of the time (30 ms a second) in this scan, averaged over 2 s: the wave goes off.
    static std::atomic<long long> windowStart{0}, windowTicks{0};
    long long start = windowStart.load();
    if (!start) windowStart.compare_exchange_strong(start, t1.QuadPart);
    windowTicks.fetch_add(t1.QuadPart - t0.QuadPart);
    if (t1.QuadPart - windowStart.load() > static_cast<long long>(2 * g_qpcHz)) {
        double ms = windowTicks.exchange(0) * 1000.0 / g_qpcHz;
        windowStart.store(t1.QuadPart);
        if (ms > 60 && !g_waveOff.exchange(true))
            g_log("GPU probe: wave OFF (safety): its check took %.0f ms in 2 s, too much of the game's time", ms);
    }
}

// Mode 5, under g_capMutex, right after the game's Unmap (its writes are done; nothing bound yet).
void unmapGuess(const void* c, const void* r) {
    UINT guess = g_mapType[c][r] == D3D11_MAP_WRITE_DISCARD ? 0 : g_nextFirst[c][r];
    g_guessAtUnmap[c][r] = guess;
    const unsigned char* base = static_cast<const unsigned char*>(g_lastMap[c][r]);
    if (!base || guess * 16 + 64 > 65536) {
        g_capStats["unmap: guess out of the buffer"]++;
        return;
    }
    const float* row3 = reinterpret_cast<const float*>(base + size_t(guess) * 16 + 48);
    if (!readable(row3, 16)) {
        g_capStats["unmap: guess unreadable"]++;
        return;
    }
    bool sim = row3[0] == 0.f && row3[1] == 0.f && row3[2] == 0.f && row3[3] == 1.f;
    g_capStats[sim ? "unmap: Sim-like block (row 3 = 0001) at the guess" : "unmap: no Sim-like block at the guess"]++;
}

template <int N>
struct UploadDetours {
    static inline MapFn map = nullptr;
    static inline UnmapFn unmap = nullptr;
    static inline UpdateSubresourceFn update = nullptr;
    static inline UpdateSubresource1Fn update1 = nullptr;
    static inline VSSetConstantBuffersFn setCb = nullptr;
    static inline VSSetConstantBuffers1Fn setCb1 = nullptr;
    static inline VSSetShaderFn setShader = nullptr;

    static HRESULT STDMETHODCALLTYPE onMap(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, D3D11_MAP type,
                                           UINT flags, D3D11_MAPPED_SUBRESOURCE* out) {
        HRESULT hr = map(c, r, sub, type, flags, out);
        D3D11_BUFFER_DESC desc{};
        if (g_mode == 6) {
            if (r && SUCCEEDED(hr) && out &&
                (type == D3D11_MAP_WRITE_DISCARD || type == D3D11_MAP_WRITE_NO_OVERWRITE) &&
                c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED && cbInfo(r, &desc) && desc.ByteWidth >= 4096)
                t_mapped[r] = Mapped{out->pData, desc.ByteWidth};
            return hr;
        }
        if (!g_capOn.load(std::memory_order_relaxed) || !r || FAILED(hr) || !out) return hr;
        if (g_mode >= 3) {  // remember where the game writes this buffer on this (deferred) context
            if ((type == D3D11_MAP_WRITE_DISCARD || type == D3D11_MAP_WRITE_NO_OVERWRITE) &&
                c->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED && cbInfo(r, &desc) && desc.ByteWidth >= 4096) {
                std::lock_guard<std::mutex> lock(g_capMutex);
                g_lastMap[c][r] = out->pData;
                if (g_mode >= 4) {
                    g_mappedNow[c][r] = true;
                    g_mapType[c][r] = type;
                    stat("map type %u width %u", type, desc.ByteWidth, 0);
                }
            }
            return hr;
        }
        if (cbInfo(r, &desc)) {
            std::lock_guard<std::mutex> lock(g_capMutex);
            stat("map cb type %u width %u usage %u", type, desc.ByteWidth, desc.Usage);
        }
        return hr;
    }
    static void STDMETHODCALLTYPE onUnmap(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub) {
        if (g_mode == 6 && r) {
            auto at = t_mapped.find(r);
            if (at != t_mapped.end()) waveScan(r, at->second.data, at->second.size);
        }
        unmap(c, r, sub);
        if (g_mode >= 4 && g_capOn.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(g_capMutex);
            auto ctx = g_mappedNow.find(c);
            if (ctx != g_mappedNow.end()) {
                auto at = ctx->second.find(r);
                if (at != ctx->second.end()) {
                    at->second = false;
                    if (g_mode == 5) unmapGuess(c, r);
                }
            }
        }
    }
    static void STDMETHODCALLTYPE onUpdate(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, const D3D11_BOX* box,
                                           const void* data, UINT pitch, UINT depth) {
        D3D11_BUFFER_DESC desc{};
        if (g_capOn.load(std::memory_order_relaxed) && r && cbInfo(r, &desc)) {
            std::lock_guard<std::mutex> lock(g_capMutex);
            stat("update cb width %u box %u..%u", desc.ByteWidth, box ? box->left : 0, box ? box->right : 0);
        }
        update(c, r, sub, box, data, pitch, depth);
    }
    static void STDMETHODCALLTYPE onUpdate1(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, const D3D11_BOX* box,
                                            const void* data, UINT pitch, UINT depth, UINT flags) {
        D3D11_BUFFER_DESC desc{};
        if (g_capOn.load(std::memory_order_relaxed) && r && cbInfo(r, &desc)) {
            std::lock_guard<std::mutex> lock(g_capMutex);
            stat("update1 cb width %u flags %u box %u", desc.ByteWidth, flags, box ? box->right - box->left : 0);
        }
        update1(c, r, sub, box, data, pitch, depth, flags);
    }
    static void STDMETHODCALLTYPE onSetShader(ID3D11DeviceContext* c, ID3D11VertexShader* s,
                                              ID3D11ClassInstance* const* ci, UINT n) {
        setShader(c, s, ci, n);
        if (g_mode == 6) t_vs = s;
        if (g_capOn.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(g_capMutex);
            g_vsNow[c] = s;
        }
    }
    static void note(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11Buffer* const* b, const UINT* first,
                     const UINT* count, bool one) {
        std::lock_guard<std::mutex> lock(g_capMutex);
        if (g_mode == 3) {  // 0.11.15: read what the game wrote, on the CPU; no GPU command of ours at all
            if (!g_capOn.load(std::memory_order_relaxed) || start != 0 || !n || !b || !b[0] || !one || !first ||
                !count)
                return;
            auto sk = g_skinnedVs.find(g_vsNow[c]);
            if (sk == g_skinnedVs.end() || !(sk->second & 0x80000000u)) return;  // Sim body shaders only
            auto ctx = g_lastMap.find(c);
            if (ctx == g_lastMap.end()) return;
            auto at = ctx->second.find(b[0]);
            if (at == ctx->second.end() || !at->second) return;
            capRead(sk->second & 0x7fffffffu, first[0], count[0], static_cast<const unsigned char*>(at->second));
            return;
        }
        if (g_mode == 5 && g_capOn.load(std::memory_order_relaxed) && one && b && first && count)
            for (UINT i = 0; i < n; ++i) {
                if (!b[i]) continue;
                auto ctx = g_lastMap.find(c);
                if (ctx == g_lastMap.end() || !ctx->second.count(b[i])) continue;
                UINT end = (first[i] + count[i] + 15) & ~15u;
                UINT& next = g_nextFirst[c][b[i]];
                if (end > next || first[i] + 4096 < next) next = end;  // the ring wraps to the start
            }
        if (g_mode >= 4) {  // 0.11.16: only counts; reads 16 bytes on the CPU, writes nothing
            if (!g_capOn.load(std::memory_order_relaxed) || start != 0 || !n || !b || !b[0] || !one || !first ||
                !count)
                return;
            auto sk = g_skinnedVs.find(g_vsNow[c]);
            if (sk == g_skinnedVs.end() || !(sk->second & 0x80000000u)) return;
            auto ctx = g_lastMap.find(c);
            if (ctx == g_lastMap.end()) {
                g_capStats["body bind: no map seen on this context"]++;
                return;
            }
            auto at = ctx->second.find(b[0]);
            if (at == ctx->second.end() || !at->second) {
                g_capStats["body bind: buffer not mapped this window"]++;
                return;
            }
            bool mapped = g_mappedNow[c][b[0]];
            const float* row3 = reinterpret_cast<const float*>(static_cast<const unsigned char*>(at->second) +
                                                               size_t(first[0]) * 16 + 48);
            const char* pad = "unreadable";
            if (readable(row3, 16))
                pad = row3[0] == 0.f && row3[1] == 0.f && row3[2] == 0.f && row3[3] == 1.f ? "0001" : "other";
            unsigned binds = ++g_bindsOf[(reinterpret_cast<uintptr_t>(c) << 20) ^ (reinterpret_cast<uintptr_t>(b[0]) << 4) ^
                                         first[0]];
            char key[128];
            std::snprintf(key, sizeof key, "body bind: %s, row 3 %s, %u constants, bind #%u of that block",
                          mapped ? "STILL MAPPED" : "after unmap", pad, count[0], binds > 3 ? 4 : binds);
            g_capStats[key]++;
            ++g_capBuffers;
            if (g_mode == 5) {
                auto& guess = g_guessAtUnmap[c];
                auto g = guess.find(b[0]);
                const char* what = g == guess.end() ? "body bind: no unmap guess yet"
                                   : first[0] == g->second ? "body bind: AT the unmap guess"
                                   : first[0] < g->second  ? "body bind: before the guess (earlier block again)"
                                                           : "body bind: past the guess";
                g_capStats[what]++;
            }
            return;
        }
        capDrain(c, map, unmap);
        if (!g_capOn.load(std::memory_order_relaxed) || start != 0 || !n || !b || !b[0]) return;
        auto sk = g_skinnedVs.find(g_vsNow[c]);
        bool skinned = sk != g_skinnedVs.end();
        bool body = skinned && (sk->second & 0x80000000u);
        D3D11_BUFFER_DESC desc{};
        b[0]->GetDesc(&desc);
        stat(one ? "vs cb0 set1 width %u constants %u skinned %u" : "vs cb0 set width %u constants %u skinned %u",
             desc.ByteWidth, one && count ? count[0] : 0, skinned);
        if (body) capCopy(c, b[0], one && first ? first[0] : 0, one && count ? count[0] : 0);
    }
    static void STDMETHODCALLTYPE onSetCb(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11Buffer* const* b) {
        setCb(c, start, n, b);
        if (g_capOn.load(std::memory_order_relaxed) || g_pending.load(std::memory_order_relaxed) > 0)
            note(c, start, n, b, nullptr, nullptr, false);
    }
    static void STDMETHODCALLTYPE onSetCb1(ID3D11DeviceContext* c, UINT start, UINT n, ID3D11Buffer* const* b,
                                           const UINT* first, const UINT* count) {
        setCb1(c, start, n, b, first, count);
        if (g_mode == 6) {
            waveBind(start, n, b, first, count);
            return;
        }
        if (g_capOn.load(std::memory_order_relaxed) || g_pending.load(std::memory_order_relaxed) > 0)
            note(c, start, n, b, first, count, true);
    }
};

// ID3D11DeviceContext1 (the game's contexts are 11.1+: checked with QueryInterface before hooking 116/119).
template <int N>
void hookUploads(void** vt, bool one, int result[7]) {
    using D = UploadDetours<N>;
    result[0] = codeHook(vt[14], &D::onMap, &D::map);
    result[1] = codeHook(vt[15], &D::onUnmap, &D::unmap);
    result[2] = codeHook(vt[48], &D::onUpdate, &D::update);
    result[3] = codeHook(vt[7], &D::onSetCb, &D::setCb);
    result[4] = codeHook(vt[11], &D::onSetShader, &D::setShader);
    result[5] = one ? codeHook(vt[116], &D::onUpdate1, &D::update1) : -2;
    result[6] = one ? codeHook(vt[119], &D::onSetCb1, &D::setCb1) : -2;
}
void (*const kUploadSets[])(void**, bool, int*) = {hookUploads<0>, hookUploads<1>, hookUploads<2>, hookUploads<3>};

// Texture streaming calls these all the time: the first one shows us the game's device.
using CreateTexture2DFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const D3D11_TEXTURE2D_DESC*,
                                                       const D3D11_SUBRESOURCE_DATA*, ID3D11Texture2D**);
using CreateSrvFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, ID3D11Resource*,
                                                 const D3D11_SHADER_RESOURCE_VIEW_DESC*, ID3D11ShaderResourceView**);
CreateTexture2DFn g_createTexture2D = nullptr;
CreateSrvFn g_createSrv = nullptr;

HRESULT STDMETHODCALLTYPE createTexture2D(ID3D11Device* d, const D3D11_TEXTURE2D_DESC* desc,
                                          const D3D11_SUBRESOURCE_DATA* data, ID3D11Texture2D** out) {
    onGameDevice(d);
    return g_createTexture2D(d, desc, data, out);
}

HRESULT STDMETHODCALLTYPE createSrv(ID3D11Device* d, ID3D11Resource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc,
                                    ID3D11ShaderResourceView** out) {
    onGameDevice(d);
    return g_createSrv(d, r, desc, out);
}

HRESULT STDMETHODCALLTYPE createDeferredContext(ID3D11Device* d, UINT flags, ID3D11DeviceContext** out) {
    onGameDevice(d);
    HRESULT hr = g_createDeferredContext(d, flags, out);
    if (SUCCEEDED(hr) && out && *out) hookContext(*out, "game's new deferred");
    return hr;
}

template <typename Fn>
bool swap(void** vtable, int slot, void* mine, Fn* original) {
    DWORD old;
    if (!VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old)) return false;
    *original = reinterpret_cast<Fn>(vtable[slot]);
    vtable[slot] = mine;
    VirtualProtect(&vtable[slot], sizeof(void*), old, &old);
    return true;
}

bool hookContext(ID3D11DeviceContext* c, const char* what) {
    void** vt = *reinterpret_cast<void***>(c);
    std::lock_guard<std::mutex> lock(g_fnMutex);
    if (g_mode >= 2 && g_mode <= 6) {
        if (g_hookedCode.count(vt[14]) && g_hookedCode.count(vt[7]) && g_hookedCode.count(vt[11])) return true;
        if (g_setsUsed >= 4) return false;
        ID3D11DeviceContext1* c1 = nullptr;
        bool one = SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&c1))) && c1;
        if (c1) c1->Release();
        int r[7];
        kUploadSets[g_setsUsed++](vt, one, r);
        g_log("GPU probe: %s context: capture hooks %d %d %d %d %d %d %d (1 new, 0 shared, -1 failed, -2 no 11.1)", what,
              r[0], r[1], r[2], r[3], r[4], r[5], r[6]);
        return r[3] >= 0;
    }
    bool fresh = false;
    for (int slot : {7, 11, 12, 17, 20})
        if (!g_hookedCode.count(vt[slot])) fresh = true;
    if (!fresh) return true;
    if (g_setsUsed >= int(sizeof kSets / sizeof kSets[0])) {
        g_log("GPU probe: %s context: out of hook sets", what);
        return false;
    }
    int r[5];
    kSets[g_setsUsed++](vt, r);
    g_log("GPU probe: %s context (vtable %p, draw code %p): hooks %d %d %d %d %d (1 new, 0 shared, -1 failed)", what,
          static_cast<void*>(vt), vt[12], r[0], r[1], r[2], r[3], r[4]);
    return r[2] >= 0 && r[4] >= 0;
}

// The first call from the game's own device: its immediate context and a deferred one of ours on it (the
// 0.11.8 log: a context's vtable is its own copy on the heap, not d3d11.dll's, so the small device's
// contexts told nothing about the game's).
void onGameDevice(ID3D11Device* d) {
    if (!d || g_gameDeviceSeen.exchange(true)) return;
    ID3D11DeviceContext* immediate = nullptr;
    d->GetImmediateContext(&immediate);
    if (immediate) {
        hookContext(immediate, "game's immediate");
        immediate->Release();
    }
    ID3D11DeviceContext* deferred = nullptr;
    if (SUCCEEDED(g_createDeferredContext(d, 0, &deferred)) && deferred) {
        hookContext(deferred, "a deferred one on the game's device");
        deferred->Release();
    }
}

}  // namespace

bool wantSim(const void* actor) {
    if ((g_mode < 2 || g_mode > 5) || !actor) return false;
    std::lock_guard<std::mutex> lock(g_capMutex);
    ULONGLONG now = GetTickCount64();
    if (!g_capActor) {
        if (g_capWindows >= 5 || now < g_capNext || (g_mode == 3 && !g_capFile)) return false;
        g_capActor = actor;
        g_capFrames = 0;
        g_capBuffers = 0;
        g_capCopies = 0;
        g_capStats.clear();
        g_capStart = now;
        g_capOn = true;
    }
    bool late = now - g_capStart > 5000;  // she left: the window still ends
    if (actor != g_capActor && !late) return false;
    if (late || ++g_capFrames > 20) {
        g_capOn = false;
        g_vsNow.clear();
        g_lastMap.clear();
        g_mappedNow.clear();
        g_bindsOf.clear();
        g_nextFirst.clear();
        g_guessAtUnmap.clear();
        g_mapType.clear();
        g_capActor = nullptr;
        g_capNext = now + 30000;
        ++g_capWindows;
        if (g_capFile) std::fflush(g_capFile);
        g_log("GPU probe: palette window %d done: %d frames, %u copies made, %u read", g_capWindows,
              g_capFrames, g_capCopies, g_capBuffers);
        for (auto& [what, n] : g_capStats) g_log("GPU probe:   %s: %u", what.c_str(), n);
        return false;
    }
    return true;
}

bool waveOn() { return g_mode == 6; }

void waveSim(const void* actor, const float pos[3], float detL, float detR, float toYou) {
    if (g_mode != 6 || !actor) return;
    ULONGLONG now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_simMutex);
        WaveSim* slot = nullptr;
        for (WaveSim& s : g_sims)
            if (s.actor == actor) slot = &s;
        if (!slot)
            for (WaveSim& s : g_sims)
                if (!slot || s.tick < slot->tick) slot = &s;
        slot->actor = actor;
        std::memcpy(slot->pos, pos, sizeof slot->pos);
        // A bone of scale ~1 can't be told from the others by its determinant: no wave for her (yet).
        slot->det[0] = std::fabs(detL - 1) > 0.08f ? detL : 0;
        slot->det[1] = std::fabs(detR - 1) > 0.08f ? detR : 0;
        slot->toYou = toYou;
        slot->tick = now;
    }
    static std::atomic<ULONGLONG> next{0};
    ULONGLONG due = next.load();
    if (now >= due && next.compare_exchange_strong(due, now + 30000))
        g_log("GPU probe: wave: %u shaders patched, %u refused by the device; %u binds, %u blocks tagged, %u "
              "(blocks of hers looked at), %u binds carried the tag, %.1f ms scanning; last waved Sim %p",
              g_wavePatched.load(), g_waveRefused.load(), g_waveBinds.load(), g_waveTagged.load(), g_waveNoSim.load(),
              g_waveSeen.load(), g_waveScanTicks.exchange(0) * 1000.0 / g_qpcHz, g_waveActor.load());
}

void simFrame(const void* data, unsigned size) {
    std::lock_guard<std::mutex> lock(g_capMutex);
    if (g_capOn) capWrite(3, data, size);
}

void start(const std::wstring& folder, Log log, int mode) {
    g_folder = folder;
    g_log = log;
    if (mode == 2) {  // 0.11.12's copies on the game's contexts made whole Sim parts vanish (user 2026-10-17)
        g_log("GPU probe: mode 2 is retired (its copies broke Sim drawing); nothing hooked");
        return;
    }
    g_mode = mode;
    if (mode == 3) {
        g_capFile = _wfopen((g_folder + L"integratedbodyphysics_palette.bin").c_str(), L"wb");
        g_capNext = GetTickCount64() + 60000;  // the first window a minute in
    }
    if (mode == 4 || mode == 5) g_capNext = GetTickCount64() + 60000;
    LARGE_INTEGER hz;
    QueryPerformanceFrequency(&hz);
    g_qpcHz = static_cast<double>(hz.QuadPart);
    if (FILE* f = _wfopen((g_folder + L"integratedbodyphysics_shaders.bin").c_str(), L"wb")) std::fclose(f);  // this session's only
    MH_STATUS mh = MH_Initialize();
    if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
        g_log("GPU probe: MinHook failed (%d)", static_cast<int>(mh));
        return;
    }
    HMODULE d3d = LoadLibraryW(L"d3d11.dll");
    auto create = d3d ? reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(GetProcAddress(d3d, "D3D11CreateDevice")) : nullptr;
    if (!create) {
        g_log("GPU probe: no d3d11.dll");
        return;
    }
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level;
    HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &level,
                        &context);
    if (FAILED(hr) || !device || !context) {
        g_log("GPU probe: our own small device failed (0x%08lx)", static_cast<unsigned long>(hr));
        return;
    }
    void** dvt = *reinterpret_cast<void***>(device);
    bool ok = swap(dvt, 11, reinterpret_cast<void*>(&createInputLayout), &g_createInputLayout) &&
              swap(dvt, 12, reinterpret_cast<void*>(&createVertexShader), &g_createVertexShader) &&
              swap(dvt, 27, reinterpret_cast<void*>(&createDeferredContext), &g_createDeferredContext) &&
              swap(dvt, 5, reinterpret_cast<void*>(&createTexture2D), &g_createTexture2D) &&
              swap(dvt, 7, reinterpret_cast<void*>(&createSrv), &g_createSrv);
    g_log("GPU probe: device vtable %p %s", static_cast<void*>(dvt), ok ? "watched" : "FAILED");
    // Our device and context are released; the device vtable stays (it belongs to d3d11.dll). The game's
    // contexts are watched from its device's first call (onGameDevice).
    context->Release();
    device->Release();
    g_log(mode == 3   ? "GPU probe: bone palette read on the CPU (5 windows of 20 frames, Sim body shaders only)"
          : mode == 4 ? "GPU probe: upload order stats (5 windows of 20 frames, Sim body shaders only, writes nothing)"
          : mode == 5 ? "GPU probe: unmap guess stats (5 windows of 20 frames, Sim body shaders only, writes nothing)"
          : mode == 6 ? "GPU probe: BREAST WAVE TEST (patched skinned shaders, fixed 3 cm in-and-out wave, nearest woman only)"
                      : "GPU probe: on (read-only: it only logs how Sims are drawn)");
}

}  // namespace gpuprobe
