// Jogo da Memória: camada de plataforma SDL2 (Mac e Nintendo Switch).
// Toda a regra do jogo vive em core/; aqui ficam janela, entrada e desenho.
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "game.hpp"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace {

// No Switch o SDL entrega A e B (e também X e Y) trocados em relação aos rótulos do controle
// Nintendo (confirmado no aparelho), então a troca é fixa nele. No Mac não troca.
#ifdef __SWITCH__
constexpr bool kSwapAB = true;
#else
constexpr bool kSwapAB = false;
#endif

// Texto de dica para sair, conforme a plataforma.
#ifdef __SWITCH__
const char* const kExitHint = "+ ou -: sair do jogo";
#else
const char* const kExitHint = "Esc: sair";
#endif

constexpr float kPi = 3.14159265358979f;  // M_PI não existe em todo toolchain (ex.: newlib estrito)

constexpr int kW = 1280;  // resolução lógica (a do Switch portátil)
constexpr int kH = 720;

struct Color {
    Uint8 r, g, b, a = 255;
};
const Color kBg{250, 246, 236};
const Color kInk{58, 48, 72};
const Color kMuted{140, 130, 150};
const Color kWhite{255, 255, 255};
const Color kEdge{222, 214, 200};
const Color kPlayerColor[memory::kMaxPlayers] = {
    {239, 83, 80}, {66, 165, 245}, {102, 187, 106}, {255, 167, 38}};

struct GridSize {
    int cols, rows;
};
const GridSize kSizes[] = {{4, 3}, {4, 4}, {6, 4}};
constexpr int kSizeCount = 3;

// ---------------------------------------------------------------- arquivos

bool pathIs(const std::string& p, bool wantDir) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    return wantDir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode);
}

std::string findAssets() {
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("MEMORIA_ASSETS")) candidates.push_back(env);
    if (char* base = SDL_GetBasePath()) {
        candidates.push_back(std::string(base) + "assets");
        candidates.push_back(std::string(base) + "../Resources/assets");  // .app do Mac
        SDL_free(base);
    }
    candidates.push_back("assets");
    candidates.push_back("romfs:/assets");                      // Switch (romfs no .nro)
    candidates.push_back("sdmc:/switch/jogo-da-memoria/assets");  // Switch (cartão SD)
#ifdef MEMORIA_SOURCE_ASSETS
    candidates.push_back(MEMORIA_SOURCE_ASSETS);
#endif
    for (const auto& c : candidates)
        if (pathIs(c + "/themes", true)) return c;
    return "";
}

struct Theme {
    std::string name;
    std::string dir;
    int faces = 0;
};

std::vector<Theme> scanThemes(const std::string& assets) {
    std::vector<Theme> themes;
    std::vector<std::string> names;
    if (DIR* d = opendir((assets + "/themes").c_str())) {
        while (dirent* e = readdir(d)) {
            if (e->d_name[0] != '.') names.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    for (const auto& n : names) {
        Theme t{n, assets + "/themes/" + n, 0};
        if (!pathIs(t.dir + "/back.png", false)) continue;
        char buf[16];
        for (int i = 1; i < 100; ++i) {
            std::snprintf(buf, sizeof buf, "/%02d.png", i);
            if (!pathIs(t.dir + buf, false)) break;
            ++t.faces;
        }
        // Precisa de pares suficientes para a menor grade.
        if (t.faces >= kSizes[0].cols * kSizes[0].rows / 2) themes.push_back(t);
    }
    return themes;
}

// ------------------------------------------------------------------ desenho

enum class Align { Left, Center, Right };
enum Font { kSmall = 0, kMedium = 1, kBig = 2 };

class Gfx {
public:
    SDL_Renderer* r = nullptr;

    bool loadFonts(const std::string& path) {
        const int sizes[3] = {26, 38, 84};
        for (int i = 0; i < 3; ++i) {
            fonts_[i] = TTF_OpenFont(path.c_str(), sizes[i]);
            if (!fonts_[i]) return false;
        }
        // O Fredoka variável abre no peso Light; negrito sintético deixa o texto legível.
        for (auto* f : fonts_) TTF_SetFontStyle(f, TTF_STYLE_BOLD);
        return true;
    }
    // Libera texturas e fontes; precisa rodar antes de destruir o renderer e de TTF_Quit().
    void shutdown() {
        clearCache();
        for (auto*& f : fonts_) {
            if (f) TTF_CloseFont(f);
            f = nullptr;
        }
    }
    ~Gfx() { shutdown(); }

    void color(Color c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }
    void fill(SDL_Rect rc, Color c) {
        color(c);
        SDL_RenderFillRect(r, &rc);
    }

    void fillRound(SDL_Rect rc, int rad, Color c) {
        rad = std::max(0, std::min({rad, rc.w / 2, rc.h / 2}));
        color(c);
        SDL_Rect mid{rc.x, rc.y + rad, rc.w, rc.h - 2 * rad};
        SDL_RenderFillRect(r, &mid);
        for (int dy = 0; dy < rad; ++dy) {
            const float yy = rad - dy - 0.5f;
            const int dx = rad - static_cast<int>(std::sqrt(float(rad * rad) - yy * yy));
            SDL_RenderDrawLine(r, rc.x + dx, rc.y + dy, rc.x + rc.w - 1 - dx, rc.y + dy);
            SDL_RenderDrawLine(r, rc.x + dx, rc.y + rc.h - 1 - dy, rc.x + rc.w - 1 - dx,
                               rc.y + rc.h - 1 - dy);
        }
    }

    void triangle(float x1, float y1, float x2, float y2, float x3, float y3, Color c) {
        SDL_Color sc{c.r, c.g, c.b, c.a};
        SDL_Vertex v[3] = {{{x1, y1}, sc, {0, 0}}, {{x2, y2}, sc, {0, 0}}, {{x3, y3}, sc, {0, 0}}};
        SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
    }
    void arrow(int cx, int cy, int s, bool left, Color c) {
        const float d = left ? -1.f : 1.f;
        triangle(cx - d * s / 2.f, cy - s, cx - d * s / 2.f, cy + s, cx + d * s / 2.f,
                 float(cy), c);
    }

    void text(const std::string& s, Font font, Color c, int x, int y,
              Align a = Align::Center) {
        SDL_Texture* t = textTexture(s, font, c);
        if (!t) return;
        int w, h;
        SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
        const int dx = a == Align::Left ? x : a == Align::Center ? x - w / 2 : x - w;
        SDL_Rect d{dx, y - h / 2, w, h};
        SDL_RenderCopy(r, t, nullptr, &d);
    }

private:
    TTF_Font* fonts_[3] = {nullptr, nullptr, nullptr};
    std::unordered_map<std::string, SDL_Texture*> cache_;

    void clearCache() {
        for (auto& kv : cache_) SDL_DestroyTexture(kv.second);
        cache_.clear();
    }
    SDL_Texture* textTexture(const std::string& s, Font font, Color c) {
        if (s.empty()) return nullptr;
        const std::string key = std::to_string(font) + ':' + std::to_string(c.r) + ',' +
                                std::to_string(c.g) + ',' + std::to_string(c.b) + ':' + s;
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        if (cache_.size() > 300) clearCache();
        SDL_Surface* surf =
            TTF_RenderUTF8_Blended(fonts_[font], s.c_str(), SDL_Color{c.r, c.g, c.b, 255});
        if (!surf) return nullptr;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
        SDL_FreeSurface(surf);
        if (tex) cache_[key] = tex;
        return tex;
    }
};

// -------------------------------------------------------------------- áudio

enum class Sfx { Flip, Match, Wrong, Win, Click, Count };

// Mixer mínimo: carrega WAVs, converte para o formato do dispositivo e mistura música
// (em loop) com até 8 efeitos ao mesmo tempo. Se o áudio falhar, o jogo segue mudo.
class Audio {
public:
    ~Audio() { shutdown(); }

    bool init(const std::string& dir) {
        SDL_AudioSpec want{};
        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        want.callback = &Audio::callback;
        want.userdata = this;
        dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &spec_, 0);
        if (!dev_) {
            std::fprintf(stderr, "Áudio indisponível: %s\n", SDL_GetError());
            return false;
        }
        bool ok = load(dir + "/music.wav", music_);
        const char* names[int(Sfx::Count)] = {"flip", "match", "wrong", "win", "click"};
        for (int i = 0; i < int(Sfx::Count); ++i)
            ok = load(dir + "/" + names[i] + ".wav", sfx_[i]) && ok;
        if (!ok) std::fprintf(stderr, "Alguns arquivos de áudio não carregaram (%s)\n", dir.c_str());
        SDL_PauseAudioDevice(dev_, 0);
        return true;
    }

    void shutdown() {
        if (dev_) SDL_CloseAudioDevice(dev_);
        dev_ = 0;
    }

    void play(Sfx s) {
        if (!dev_ || sfx_[int(s)].pcm.empty()) return;
        SDL_LockAudioDevice(dev_);
        Voice* slot = &voices_[0];
        for (auto& v : voices_)
            if (!v.clip) { slot = &v; break; }
        slot->clip = &sfx_[int(s)];
        slot->pos = 0;
        SDL_UnlockAudioDevice(dev_);
    }

private:
    struct Clip {
        std::vector<Uint8> pcm;
    };
    struct Voice {
        const Clip* clip = nullptr;
        size_t pos = 0;
    };
    static constexpr int kMusicVolume = 48;  // de 128
    static constexpr int kSfxVolume = 60;

    SDL_AudioDeviceID dev_ = 0;
    SDL_AudioSpec spec_{};
    Clip music_;
    Clip sfx_[int(Sfx::Count)];
    Voice voices_[8];
    size_t musicPos_ = 0;

    bool load(const std::string& file, Clip& out) {
        SDL_AudioSpec ws;
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (!SDL_LoadWAV(file.c_str(), &ws, &buf, &len)) {
            std::fprintf(stderr, "WAV não carregou: %s (%s)\n", file.c_str(), SDL_GetError());
            return false;
        }
        SDL_AudioStream* st = SDL_NewAudioStream(ws.format, ws.channels, ws.freq, spec_.format,
                                                 spec_.channels, spec_.freq);
        bool ok = st && SDL_AudioStreamPut(st, buf, int(len)) == 0 && SDL_AudioStreamFlush(st) == 0;
        if (ok) {
            const int n = SDL_AudioStreamAvailable(st);
            out.pcm.resize(size_t(n));
            ok = SDL_AudioStreamGet(st, out.pcm.data(), n) == n;
        }
        if (st) SDL_FreeAudioStream(st);
        SDL_FreeWAV(buf);
        return ok;
    }

    static void callback(void* self, Uint8* stream, int len) {
        static_cast<Audio*>(self)->mix(stream, len);
    }

    void mix(Uint8* stream, int len) {
        SDL_memset(stream, 0, size_t(len));
        if (!music_.pcm.empty()) {
            int off = 0;
            while (off < len) {
                const size_t left = music_.pcm.size() - musicPos_;
                const int n = int(std::min<size_t>(size_t(len - off), left));
                SDL_MixAudioFormat(stream + off, music_.pcm.data() + musicPos_, spec_.format,
                                   Uint32(n), kMusicVolume);
                off += n;
                musicPos_ += size_t(n);
                if (musicPos_ >= music_.pcm.size()) musicPos_ = 0;
            }
        }
        for (auto& v : voices_) {
            if (!v.clip) continue;
            const size_t left = v.clip->pcm.size() - v.pos;
            const int n = int(std::min<size_t>(size_t(len), left));
            SDL_MixAudioFormat(stream, v.clip->pcm.data() + v.pos, spec_.format, Uint32(n),
                               kSfxVolume);
            v.pos += size_t(n);
            if (v.pos >= v.clip->pcm.size()) v.clip = nullptr;
        }
    }
};

// ------------------------------------------------------------------- entrada

enum class Action {
    Up, Down, Left, Right, Confirm, Back, Quit, Click,
    ExitApp,        // + ou - no Switch: sai do jogo, de qualquer tela e de qualquer controle
    Fill,           // na tela de controles: este controle fica com todas as vagas livres
    DebugMatch,     // só pelo roteiro de teste (MEMORIA_SCRIPT)
    DebugMismatch,  // idem
};
// Dispositivos de entrada. Controles usam o id de instância do SDL (>= 0).
constexpr int kDeviceKeyboard = -1;
constexpr int kDeviceMouse = -2;   // mouse ou toque na tela
constexpr int kDeviceAny = -3;     // vaga sem dono fixo (modo solo)
constexpr int kDeviceNone = -4;    // vaga ainda não atribuída
constexpr int kScriptPadBase = 1000000;  // controles falsos do roteiro de teste

struct Input {
    Action action;
    int device = kDeviceKeyboard;
    int x = 0, y = 0;  // coordenadas lógicas, para Click
};

// -------------------------------------------------------------------- sessão

struct Session {
    std::unique_ptr<memory::Game> game;
    int cols = 0, rows = 0;
    std::vector<float> flip;    // 0 = de costas, 1 = de frente
    std::vector<int> owner;     // quem fechou o par (-1 = ninguém)
    std::vector<int> image;     // face lógica -> índice da imagem no tema
    std::vector<SDL_Texture*> faceTex;
    SDL_Texture* back = nullptr;
    int cursor = 0;
    int firstPick = -1;
    float mismatchTimer = 0;
    float finishTimer = 0;
    float wrongTimer = 0;  // atraso até tocar o som de erro (depois da carta virar)

    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session() {
        for (auto* t : faceTex)
            if (t) SDL_DestroyTexture(t);
        if (back) SDL_DestroyTexture(back);
    }
};

enum class Screen { Menu, Assign, Game, Result };

struct Settings {
    int players = 2;
    int size = 1;
    int theme = 0;
};

class App {
public:
    bool init();
    void run();
    ~App();

private:
    SDL_Window* window_ = nullptr;
    Gfx gfx_;
    std::string assets_;
    std::vector<Theme> themes_;
    Settings settings_;
    Screen screen_ = Screen::Menu;
    int menuRow_ = 3;
    std::unique_ptr<Session> session_;
    bool running_ = true;
    std::vector<SDL_GameController*> pads_;
    std::vector<SDL_Joystick*> joys_;  // joysticks que o SDL não reconhece como controle
    Audio audio_;
    bool assignOnButton_ = false;  // foco no botão "Começar agora" da tela de controles
    bool debugInput_ = false;
    std::unordered_map<int, std::array<int, 2>> stick_;  // por dispositivo
    std::unordered_map<int, int> padNumber_;             // id -> "Controle N"
    std::vector<int> slots_;  // dispositivo de cada jogador (kDeviceNone = livre)
    std::vector<Input> queue_;
    float clock_ = 0;

    // roteiro de teste automatizado
    std::string script_;
    size_t scriptPos_ = 0;
    float scriptWait_ = 0;
    int scriptDevice_ = kDeviceKeyboard;
    std::string shotDir_;
    int shotCount_ = 0;
    bool shotPending_ = false;

    void pumpEvents();
    void pumpScript(float dt);
    void handle(const Input& in);
    void update(float dt);
    void render();
    void saveShot();

    void menuInput(const Input& in);
    void gameInput(const Input& in);
    void resultInput(const Input& in);
    void assignInput(const Input& in);
    void finishAssign();
    SDL_Rect assignButtonRect() const;
    bool allowed(const Input& in) const;
    void requestStart(bool reuseSlots, int starter);
    void padRemoved(int id);
    int padNumber(int id);
    void rescanPads();
    int inputDevices() const;
    void openJoystick(int index);
    void addPad(SDL_GameController* p);
    void buttonEvent(int dev, int sdlButton, bool labelSwapped);
    void stickEvent(int dev, bool horizontal, int value);
    std::string deviceName(int id);
    bool startGame();
    void toMenu();
    void flipCard(int i);
    void changeValue(int row, int dir);
    SDL_Rect menuRowRect(int row) const;
    SDL_Rect cardRect(int i) const;

    void renderMenu();
    void renderGame();
    void renderAssign();
    void renderResult();
    void renderScoreboard(int y);
    int  validSize(int size, int dir) const;
    bool sizeFits(int size) const {
        return kSizes[size].cols * kSizes[size].rows / 2 <= themes_[settings_.theme].faces;
    }
};

App::~App() {
    session_.reset();
    gfx_.shutdown();
    audio_.shutdown();
    for (auto* j : joys_) SDL_JoystickClose(j);
    for (auto* p : pads_) SDL_GameControllerClose(p);
    if (gfx_.r) SDL_DestroyRenderer(gfx_.r);
    if (window_) SDL_DestroyWindow(window_);
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

bool App::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) || TTF_Init() != 0) {
        std::fprintf(stderr, "IMG/TTF init: %s\n", SDL_GetError());
        return false;
    }
    assets_ = findAssets();
    if (assets_.empty()) {
        std::fprintf(stderr, "Pasta 'assets' não encontrada (use MEMORIA_ASSETS=...)\n");
        return false;
    }
    themes_ = scanThemes(assets_);
    if (themes_.empty()) {
        std::fprintf(stderr, "Nenhum tema válido em %s/themes\n", assets_.c_str());
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    window_ = SDL_CreateWindow("Jogo da Memória", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, kW, kH,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    gfx_.r = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!gfx_.r) gfx_.r = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!gfx_.r) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(gfx_.r, kW, kH);
    SDL_SetRenderDrawBlendMode(gfx_.r, SDL_BLENDMODE_BLEND);
    if (!gfx_.loadFonts(assets_ + "/fonts/Fredoka.ttf")) {
        std::fprintf(stderr, "Fonte não carregada: %s\n", TTF_GetError());
        return false;
    }

    if (const char* v = std::getenv("MEMORIA_VIRTUAL_PADS")) {  // só para testes
        for (int i = 0, n = std::atoi(v); i < n; ++i) {
            SDL_VirtualJoystickDesc d;
            SDL_zero(d);
            d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
            d.naxes = SDL_CONTROLLER_AXIS_MAX;
            d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
            d.name = "Controle virtual";
            SDL_JoystickAttachVirtualEx(&d);
        }
    }
    rescanPads();
    if (std::getenv("MEMORIA_DEBUG"))
        std::fprintf(stderr, "controles abertos: %d (de %d conectados)\n",
                     int(pads_.size() + joys_.size()), SDL_NumJoysticks());
    audio_.init(assets_ + "/audio");
#ifdef __SWITCH__
    debugInput_ = true;  // o log aparece no terminal com `nxlink -s`
#endif
    if (std::getenv("MEMORIA_DEBUG")) debugInput_ = true;

    if (const char* s = std::getenv("MEMORIA_SCRIPT")) script_ = s;
    if (const char* d = std::getenv("MEMORIA_SHOT_DIR")) shotDir_ = d;
    return true;
}

// ------------------------------------------------------------------ eventos

// Um joystick que o SDL não conhece como "controle" ainda é usado, pelos números dos
// botões do Switch (A=0 B=1 X=2 Y=3 +=10 -=11, direcional 12-15).
void App::addPad(SDL_GameController* p) {
    if (!p || std::find(pads_.begin(), pads_.end(), p) != pads_.end()) return;
    if (int(pads_.size() + joys_.size()) >= memory::kMaxPlayers) {  // o jogo é de até 4
        SDL_GameControllerClose(p);
        return;
    }
    pads_.push_back(p);
    padNumber(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p)));
}

void App::openJoystick(int index) {
    if (int(pads_.size() + joys_.size()) >= memory::kMaxPlayers) return;
    if (SDL_Joystick* j = SDL_JoystickOpen(index)) {
        joys_.push_back(j);
        padNumber(SDL_JoystickInstanceID(j));
    }
}

// Abre os controles conectados que ainda não estão em uso, até o limite de 4.
void App::rescanPads() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (int(pads_.size() + joys_.size()) >= memory::kMaxPlayers) return;
        const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
        if (SDL_JoystickFromInstanceID(id)) continue;  // já aberto
        if (SDL_IsGameController(i)) addPad(SDL_GameControllerOpen(i));
        else openJoystick(i);
    }
}

int App::inputDevices() const {
    if (const char* d = std::getenv("MEMORIA_DEVICES")) return std::atoi(d);  // para testes
    if (!script_.empty()) return 99;  // roteiro de teste: finge vários controles
    int n = int(pads_.size() + joys_.size());
#ifndef __SWITCH__
    n += 2;  // no computador, teclado e mouse são dois dispositivos (cada um pode ser um jogador)
#endif
    return n;
}

// `labelSwapped`: o botão veio da API de controle do SDL, que no Nintendo troca A/B e X/Y.
// Joysticks "crus" já trazem a numeração física e não sofrem a troca.
void App::buttonEvent(int dev, int btn, bool labelSwapped) {
    const bool swap = kSwapAB && labelSwapped;
    if (debugInput_) std::fprintf(stderr, "[controle %d] botão %d\n", dev, btn);
    auto push = [&](Action a) { queue_.push_back({a, dev, 0, 0}); };
    switch (btn) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: push(Action::Up); break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: push(Action::Down); break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: push(Action::Left); break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: push(Action::Right); break;
        case SDL_CONTROLLER_BUTTON_A: push(swap ? Action::Back : Action::Confirm); break;
        case SDL_CONTROLLER_BUTTON_B: push(swap ? Action::Confirm : Action::Back); break;
        case SDL_CONTROLLER_BUTTON_X: if (swap) push(Action::Fill); break;   // o Y físico
        case SDL_CONTROLLER_BUTTON_Y: if (!swap) push(Action::Fill); break;
#ifdef __SWITCH__
        // No Switch, + e - saem do jogo de qualquer tela, mesmo com os controles soltos.
        case SDL_CONTROLLER_BUTTON_START:
        case SDL_CONTROLLER_BUTTON_BACK: push(Action::ExitApp); break;
#else
        case SDL_CONTROLLER_BUTTON_START: push(Action::Quit); break;
#endif
        default: break;
    }
}

void App::stickEvent(int dev, bool horizontal, int v) {
    int& st = stick_[dev][horizontal ? 0 : 1];
    const int dir = v > 16000 ? 1 : v < -16000 ? -1 : (std::abs(v) < 8000 ? 0 : st);
    if (dir != st && dir != 0) {
        if (horizontal) queue_.push_back({dir > 0 ? Action::Right : Action::Left, dev, 0, 0});
        else queue_.push_back({dir > 0 ? Action::Down : Action::Up, dev, 0, 0});
    }
    st = dir;
}

void App::pumpEvents() {
    auto pushFrom = [&](int dev, Action act, int x = 0, int y = 0) {
        queue_.push_back({act, dev, x, y});
    };
    auto key = [&](Action act) { pushFrom(kDeviceKeyboard, act); };
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT: running_ = false; break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                    case SDLK_UP: case SDLK_w: key(Action::Up); break;
                    case SDLK_DOWN: case SDLK_s: key(Action::Down); break;
                    case SDLK_LEFT: case SDLK_a: key(Action::Left); break;
                    case SDLK_RIGHT: case SDLK_d: key(Action::Right); break;
                    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
                        if (!e.key.repeat) key(Action::Confirm);
                        break;
                    case SDLK_TAB: key(Action::Fill); break;
                    case SDLK_BACKSPACE: key(Action::Back); break;
                    case SDLK_ESCAPE: key(Action::Quit); break;
                    case SDLK_f: case SDLK_F11: {
                        const bool fs = SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                        SDL_SetWindowFullscreen(window_, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                        break;
                    }
                    default: break;
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                // Cliques sintéticos do toque são ignorados: o toque é tratado abaixo.
                if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) {
                    float lx, ly;
                    SDL_RenderWindowToLogical(gfx_.r, e.button.x, e.button.y, &lx, &ly);
                    pushFrom(kDeviceMouse, Action::Click, int(lx), int(ly));
                }
                break;
            case SDL_FINGERDOWN:  // tela de toque (não o trackpad do Mac)
                if (SDL_GetTouchDeviceType(e.tfinger.touchId) == SDL_TOUCH_DEVICE_DIRECT) {
                    if (debugInput_) std::fprintf(stderr, "[toque] %.2f %.2f\n", e.tfinger.x, e.tfinger.y);
                    pushFrom(kDeviceMouse, Action::Click, int(e.tfinger.x * kW), int(e.tfinger.y * kH));
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                addPad(SDL_GameControllerOpen(e.cdevice.which));
                break;
            case SDL_JOYDEVICEADDED:
                if (!SDL_IsGameController(e.jdevice.which)) openJoystick(e.jdevice.which);
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (auto* p = SDL_GameControllerFromInstanceID(e.cdevice.which)) {
                    pads_.erase(std::remove(pads_.begin(), pads_.end(), p), pads_.end());
                    SDL_GameControllerClose(p);
                }
                stick_.erase(e.cdevice.which);
                padRemoved(e.cdevice.which);
                rescanPads();
                break;
            case SDL_JOYDEVICEREMOVED:
                if (auto* j = SDL_JoystickFromInstanceID(e.jdevice.which)) {
                    const auto it = std::find(joys_.begin(), joys_.end(), j);
                    if (it != joys_.end()) {
                        joys_.erase(it);
                        SDL_JoystickClose(j);
                        stick_.erase(e.jdevice.which);
                        padRemoved(e.jdevice.which);
                        rescanPads();
                    }
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                buttonEvent(e.cbutton.which, e.cbutton.button, true);
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) stickEvent(e.caxis.which, true, e.caxis.value);
                else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) stickEvent(e.caxis.which, false, e.caxis.value);
                break;
            // Joysticks "crus": os eventos de quem já é controle são ignorados (já tratados acima).
            case SDL_JOYBUTTONDOWN:
                if (!SDL_GameControllerFromInstanceID(e.jbutton.which)) {
                    static const int kNx[16] = {
                        SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X,
                        SDL_CONTROLLER_BUTTON_Y, -1, -1, -1, -1, -1, -1,
                        SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_BACK,
                        SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_UP,
                        SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_DOWN};
                    if (debugInput_) std::fprintf(stderr, "[joystick %d] botão cru %d\n", int(e.jbutton.which), int(e.jbutton.button));
                    if (e.jbutton.button < 16 && kNx[e.jbutton.button] >= 0)
                        buttonEvent(e.jbutton.which, kNx[e.jbutton.button], false);
                }
                break;
            case SDL_JOYAXISMOTION:
                if (!SDL_GameControllerFromInstanceID(e.jaxis.which) && e.jaxis.axis < 2)
                    stickEvent(e.jaxis.which, e.jaxis.axis == 0, e.jaxis.value);
                break;
            case SDL_JOYHATMOTION:
                if (!SDL_GameControllerFromInstanceID(e.jhat.which)) {
                    if (e.jhat.value & SDL_HAT_UP) buttonEvent(e.jhat.which, SDL_CONTROLLER_BUTTON_DPAD_UP, false);
                    if (e.jhat.value & SDL_HAT_DOWN) buttonEvent(e.jhat.which, SDL_CONTROLLER_BUTTON_DPAD_DOWN, false);
                    if (e.jhat.value & SDL_HAT_LEFT) buttonEvent(e.jhat.which, SDL_CONTROLLER_BUTTON_DPAD_LEFT, false);
                    if (e.jhat.value & SDL_HAT_RIGHT) buttonEvent(e.jhat.which, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, false);
                }
                break;
            default: break;
        }
    }
}

// Roteiro de teste (só ativo com MEMORIA_SCRIPT): u d l r c b y = ações do dispositivo
// atual; 1-4 escolhem um "controle falso" e k/o o teclado/mouse; z desconecta o controle
// atual; w espera; s tira foto; m/e jogam um acerto/erro; q sai.
void App::pumpScript(float dt) {
    if (scriptPos_ >= script_.size()) return;
    scriptWait_ -= dt;
    if (scriptWait_ > 0) return;
    scriptWait_ = 0.35f;
    auto act = [&](Action a) { queue_.push_back({a, scriptDevice_, 0, 0}); };
    const char ch = script_[scriptPos_++];
    switch (ch) {
        case 'u': act(Action::Up); break;
        case 'd': act(Action::Down); break;
        case 'l': act(Action::Left); break;
        case 'r': act(Action::Right); break;
        case 'c': act(Action::Confirm); break;
        case 'b': act(Action::Back); break;
        case 'y': act(Action::Fill); break;
        case 'm': act(Action::DebugMatch); break;
        case 'e': act(Action::DebugMismatch); break;
        case '1': case '2': case '3': case '4': scriptDevice_ = kScriptPadBase + (ch - '0'); break;
        case 'k': scriptDevice_ = kDeviceKeyboard; break;
        case 'o': scriptDevice_ = kDeviceMouse; break;
        case 'z': padRemoved(scriptDevice_); break;
        case '+': case '-': {
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = SDL_CONTROLLERBUTTONDOWN;
            ev.cbutton.which = scriptDevice_;
            ev.cbutton.button = ch == '+' ? SDL_CONTROLLER_BUTTON_START : SDL_CONTROLLER_BUTTON_BACK;
            SDL_PushEvent(&ev);
            break;
        }
        case 'w': scriptWait_ = 1.6f; break;
        case 's': shotPending_ = true; break;
        case 'q': running_ = false; break;
        default: break;
    }
}

void App::saveShot() {
    shotPending_ = false;
    if (shotDir_.empty()) return;
    int w, h;
    SDL_GetRendererOutputSize(gfx_.r, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    SDL_RenderReadPixels(gfx_.r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    char name[512];
    std::snprintf(name, sizeof name, "%s/shot%02d.bmp", shotDir_.c_str(), ++shotCount_);
    SDL_SaveBMP(s, name);
    SDL_FreeSurface(s);
}

// --------------------------------------------------------------------- menu

SDL_Rect App::menuRowRect(int row) const {
    if (row == 3) return {kW / 2 - 200, 560, 400, 84};
    return {kW / 2 - 330, 230 + row * 100, 660, 80};
}

int App::validSize(int size, int dir) const {
    for (int i = 0; i < kSizeCount; ++i) {
        size = (size + dir + kSizeCount) % kSizeCount;
        if (sizeFits(size)) return size;
    }
    return settings_.size;
}

void App::changeValue(int row, int dir) {
    const int n = int(themes_.size());
    switch (row) {
        case 0: settings_.players = (settings_.players - 1 + dir + memory::kMaxPlayers) % memory::kMaxPlayers + 1; break;
        case 1: settings_.size = validSize(settings_.size, dir); break;
        case 2:
            settings_.theme = (settings_.theme + dir + n) % n;
            if (!sizeFits(settings_.size)) settings_.size = validSize(settings_.size, -1);
            break;
        default: break;
    }
}

void App::menuInput(const Input& in) {
    switch (in.action) {
        case Action::Up: menuRow_ = (menuRow_ + 3) % 4; break;
        case Action::Down: menuRow_ = (menuRow_ + 1) % 4; break;
        case Action::Left: changeValue(menuRow_, -1); break;
        case Action::Right: changeValue(menuRow_, +1); break;
        case Action::Confirm:
            if (menuRow_ == 3) requestStart(false, in.device);
            else changeValue(menuRow_, +1);
            break;
        case Action::Quit: running_ = false; break;
        case Action::Click:
            for (int row = 0; row < 4; ++row) {
                const SDL_Rect rc = menuRowRect(row);
                const SDL_Point p{in.x, in.y};
                if (!SDL_PointInRect(&p, &rc)) continue;
                menuRow_ = row;
                if (row == 3) requestStart(false, in.device);
                else changeValue(row, in.x < rc.x + rc.w - 170 ? -1 : +1);
            }
            break;
        default: break;
    }
}

bool App::startGame() {
    const Theme& th = themes_[settings_.theme];
    const GridSize gs = kSizes[settings_.size];
    auto s = std::make_unique<Session>();

    // std::random_device não é confiável no toolchain do Switch; o relógio de alta
    // resolução dá semente suficiente para embaralhar cartas.
    const Uint64 tick = SDL_GetPerformanceCounter();
    const std::uint32_t seed =
        std::uint32_t(tick ^ (tick >> 32) ^ Uint64(std::time(nullptr)));
    memory::Config cfg{gs.rows, gs.cols, settings_.players, seed};
    s->game = std::make_unique<memory::Game>(cfg);
    s->cols = gs.cols;
    s->rows = gs.rows;
    const int cards = gs.cols * gs.rows;
    s->flip.assign(cards, 0.f);
    s->owner.assign(cards, -1);

    // Sorteia quais imagens do tema entram nesta partida.
    std::vector<int> all(th.faces);
    for (int i = 0; i < th.faces; ++i) all[i] = i + 1;
    std::mt19937 rng(cfg.seed + 1);
    std::shuffle(all.begin(), all.end(), rng);
    s->image.assign(all.begin(), all.begin() + cards / 2);

    auto load = [&](const std::string& file) {
        SDL_Texture* t = IMG_LoadTexture(gfx_.r, file.c_str());
        if (!t) std::fprintf(stderr, "Falha ao carregar %s: %s\n", file.c_str(), IMG_GetError());
        return t;
    };
    s->back = load(th.dir + "/back.png");
    char buf[16];
    for (int img : s->image) {
        std::snprintf(buf, sizeof buf, "/%02d.png", img);
        s->faceTex.push_back(load(th.dir + buf));
    }
    if (!s->back || std::find(s->faceTex.begin(), s->faceTex.end(), nullptr) != s->faceTex.end())
        return false;

    session_ = std::move(s);
    screen_ = Screen::Game;
    return true;
}

void App::toMenu() {
    session_.reset();
    screen_ = Screen::Menu;
}

// --------------------------------------------------------------------- jogo

SDL_Rect App::cardRect(int i) const {
    const int top = 120, bottom = 50, side = 40;
    const int availW = kW - 2 * side, availH = kH - top - bottom;
    const int cell = std::min(availW / session_->cols, availH / session_->rows);
    const int gap = cell / 12;
    const int x0 = (kW - cell * session_->cols) / 2;
    const int y0 = top + (availH - cell * session_->rows) / 2;
    const int c = i % session_->cols, r = i / session_->cols;
    return {x0 + c * cell + gap / 2, y0 + r * cell + gap / 2, cell - gap, cell - gap};
}

void App::flipCard(int i) {
    Session& s = *session_;
    const int player = s.game->currentPlayer();
    const memory::FlipResult result = s.game->flip(i);
    if (result != memory::FlipResult::Invalid) audio_.play(Sfx::Flip);
    switch (result) {
        case memory::FlipResult::First: s.firstPick = i; break;
        case memory::FlipResult::Match:
            audio_.play(Sfx::Match);
            s.owner[s.firstPick] = s.owner[i] = player;
            s.firstPick = -1;
            if (s.game->finished()) s.finishTimer = 1.4f;
            break;
        case memory::FlipResult::Mismatch:
            s.firstPick = -1;
            s.mismatchTimer = 1.1f;
            s.wrongTimer = 0.3f;
            break;
        case memory::FlipResult::Invalid: break;
    }
}

void App::gameInput(const Input& in) {
    Session& s = *session_;
    const int n = s.game->cardCount();
    auto move = [&](int dc, int dr) {
        const int c = (s.cursor % s.cols + dc + s.cols) % s.cols;
        const int r = (s.cursor / s.cols + dr + s.rows) % s.rows;
        s.cursor = r * s.cols + c;
    };
    auto settle = [&] {  // dá a vez ao próximo sem esperar o tempo de memorizar
        if (s.game->phase() == memory::Phase::Mismatch) {
            s.game->resolveMismatch();
            s.mismatchTimer = 0;
        }
    };
    switch (in.action) {
        case Action::Up: move(0, -1); break;
        case Action::Down: move(0, +1); break;
        case Action::Left: move(-1, 0); break;
        case Action::Right: move(+1, 0); break;
        case Action::Confirm:
            settle();
            flipCard(s.cursor);
            break;
        case Action::Click:
            for (int i = 0; i < n; ++i) {
                const SDL_Rect rc = cardRect(i);
                const SDL_Point p{in.x, in.y};
                if (!SDL_PointInRect(&p, &rc)) continue;
                s.cursor = i;
                settle();
                flipCard(i);
            }
            break;
        case Action::Back: case Action::Quit: toMenu(); break;
        case Action::Fill: case Action::ExitApp: break;
        case Action::DebugMatch:
        case Action::DebugMismatch: {
            if (s.game->phase() != memory::Phase::PickFirst) break;
            int a = -1, b = -1;
            for (int i = 0; i < n && b < 0; ++i) {
                if (s.game->card(i).state != memory::CardState::Hidden) continue;
                if (a < 0) { a = i; continue; }
                const bool same = s.game->card(i).face == s.game->card(a).face;
                if (same == (in.action == Action::DebugMatch)) b = i;
            }
            if (b >= 0) { flipCard(a); flipCard(b); }
            break;
        }
    }
}

void App::resultInput(const Input& in) {
    switch (in.action) {
        case Action::Confirm: case Action::Click:
            requestStart(true, in.device);
            break;
        case Action::Back: case Action::Quit: toMenu(); break;
        default: break;
    }
}

// ---------------------------------------------------------- controles (1 por jogador)

// "Controle N" usa o menor número livre entre os controles conectados (1 a 4), em vez de
// crescer a cada reconexão. Assim a tela nunca sugere mais de 4 controles.
int App::padNumber(int id) {
    auto it = padNumber_.find(id);
    if (it != padNumber_.end()) return it->second;
    int n = 1;
    for (;; ++n) {
        bool used = false;
        for (const auto& kv : padNumber_) used = used || kv.second == n;
        if (!used) break;
    }
    return padNumber_[id] = n;
}

std::string App::deviceName(int id) {
    switch (id) {
        case kDeviceKeyboard: return "Teclado";
        case kDeviceMouse: return "Mouse / toque";
        case kDeviceAny: return "qualquer controle";
        case kDeviceNone: return "";
        default: return "Controle " + std::to_string(padNumber(id));
    }
}

// Durante a partida, só o dispositivo do jogador da vez manda. Voltar ao menu (Esc) vale
// para todos; no menu, na atribuição e no resultado qualquer dispositivo vale.
bool App::allowed(const Input& in) const {
    if (screen_ != Screen::Game || !session_) return true;
    switch (in.action) {
        case Action::Quit: case Action::DebugMatch: case Action::DebugMismatch: return true;
        default: break;
    }
    const int owner = slots_[session_->game->currentPlayer()];
    if (owner == in.device) return true;
    if (owner != kDeviceAny) return false;  // a vez é de outro dispositivo: bloqueado
    // Vaga livre: serve qualquer dispositivo que não seja dono de OUTRA vaga. Assim quem já
    // entrou como Jogador 1 não joga na vez do Jogador 2.
    return std::find(slots_.begin(), slots_.end(), in.device) == slots_.end();
}

void App::requestStart(bool reuseSlots, int starter) {
    const int n = settings_.players;
    if (n == 1) {  // sozinho: qualquer controle serve
        slots_.assign(1, kDeviceAny);
        if (!startGame()) toMenu();
        return;
    }
    if (inputDevices() < 2) {  // um controle só (ex.: Switch portátil): todos dividem
        slots_.assign(n, kDeviceAny);
        if (!startGame()) toMenu();
        return;
    }
    bool ready = reuseSlots && int(slots_.size()) == n;
    if (ready)
        for (int d : slots_) ready = ready && d != kDeviceNone;
    if (ready) {
        if (!startGame()) toMenu();
        return;
    }
    slots_.assign(n, kDeviceNone);
#ifdef __SWITCH__
    if (starter != kDeviceMouse) slots_[0] = starter;  // o toque na tela não conta como jogador
#else
    slots_[0] = starter;  // quem apertou "Jogar" já é o Jogador 1
#endif
    session_.reset();
    assignOnButton_ = false;
    screen_ = Screen::Assign;
}

// Um controle sumiu: libera as vagas dele. No meio da partida, pausa e pede para
// atribuir de novo, sem perder o jogo.
void App::padRemoved(int id) {
    padNumber_.erase(id);  // libera o número para o próximo controle
    bool used = false;
    for (int& d : slots_)
        if (d == id) { d = kDeviceNone; used = true; }
    if (used && screen_ == Screen::Game) {
        assignOnButton_ = false;
        screen_ = Screen::Assign;
    }
}

SDL_Rect App::assignButtonRect() const {
    return {kW / 2 - 190, 548, 380, 68};
}

// Vagas que ninguém assumiu ficam abertas: quem estiver na vez joga com qualquer dispositivo.
void App::finishAssign() {
    for (int& d : slots_)
        if (d == kDeviceNone) d = kDeviceAny;
    if (session_) screen_ = Screen::Game;  // voltando de uma pausa
    else if (!startGame()) toMenu();
}

void App::assignInput(const Input& in) {
    auto owns = [&](int dev) { return std::find(slots_.begin(), slots_.end(), dev) != slots_.end(); };
    switch (in.action) {
        case Action::Down: assignOnButton_ = true; return;
        case Action::Up: assignOnButton_ = false; return;
        case Action::Confirm:
            // Com o foco no botão, quem já tem vaga aperta de novo para começar;
            // quem ainda não entrou continua entrando.
            if (assignOnButton_ && owns(in.device)) { finishAssign(); return; }
            if (owns(in.device)) break;
            {
                auto it = std::find(slots_.begin(), slots_.end(), kDeviceNone);
                if (it != slots_.end()) *it = in.device;
            }
            break;
        case Action::Click: {
            const SDL_Rect b = assignButtonRect();
            const SDL_Point p{in.x, in.y};
            if (SDL_PointInRect(&p, &b)) { finishAssign(); return; }
            if (owns(in.device)) break;
            auto it = std::find(slots_.begin(), slots_.end(), kDeviceNone);
            if (it != slots_.end()) *it = in.device;
            break;
        }
        case Action::Fill:  // o resto da turma divide este controle
            for (int& d : slots_)
                if (d == kDeviceNone) d = in.device;
            break;
        case Action::Back:
            if (owns(in.device)) {
                for (int& d : slots_)
                    if (d == in.device) d = kDeviceNone;
            } else if (std::all_of(slots_.begin(), slots_.end(), [](int d) { return d == kDeviceNone; })) {
                toMenu();
            }
            return;
        case Action::Quit: toMenu(); return;
        default: return;
    }
    // Mesmo com todas as vagas ocupadas, o jogo só começa pelo botão "Começar agora":
    // aqui só deixamos o botão em foco para o próximo A.
    if (std::find(slots_.begin(), slots_.end(), kDeviceNone) == slots_.end()) assignOnButton_ = true;
}

void App::renderAssign() {
    const int n = settings_.players;
    gfx_.text("Controles", kBig, kInk, kW / 2, 90);
#ifdef __SWITCH__
    const char* rule = "Até 4 jogadores. Cada um usa o seu controle: aperte A para entrar";
#else
    const char* rule = "Até 4 jogadores. Cada um usa o seu: A no controle, Enter no teclado ou clique no mouse";
#endif
    gfx_.text(session_ ? "Um controle desconectou: quem ficou sem controle entra de novo" : rule, kSmall,
              kMuted, kW / 2, 164);
    const auto firstFree = std::find(slots_.begin(), slots_.end(), kDeviceNone);
    int freeSlots = 0;
    for (int d : slots_) freeSlots += d == kDeviceNone;
    for (int i = 0; i < n; ++i) {
        const SDL_Rect rc{kW / 2 - 350, 200 + i * 82, 700, 68};
        const bool next = !assignOnButton_ && firstFree != slots_.end() && i == int(firstFree - slots_.begin());
        if (next) gfx_.fillRound({rc.x - 5, rc.y - 5, rc.w + 10, rc.h + 10}, 26, kInk);
        gfx_.fillRound(rc, 22, kWhite);
        gfx_.fillRound({rc.x + 14, rc.y + 12, 44, 44}, 22, kPlayerColor[i]);
        gfx_.text("Jogador " + std::to_string(i + 1), kMedium, kInk, rc.x + 76, rc.y + rc.h / 2, Align::Left);
        if (slots_[i] == kDeviceNone)
            gfx_.text("A / Enter / clique", kSmall, kMuted, rc.x + rc.w - 28, rc.y + rc.h / 2, Align::Right);
        else
            gfx_.text(deviceName(slots_[i]), kMedium, kPlayerColor[i], rc.x + rc.w - 28, rc.y + rc.h / 2, Align::Right);
    }
    const SDL_Rect btn = assignButtonRect();
    if (assignOnButton_) gfx_.fillRound({btn.x - 6, btn.y - 6, btn.w + 12, btn.h + 12}, 40, kInk);
    gfx_.fillRound(btn, 34, kPlayerColor[2]);
    gfx_.text("Começar agora", kMedium, kWhite, btn.x + btn.w / 2, btn.y + btn.h / 2);
    if (freeSlots > 0)
        gfx_.text(std::to_string(freeSlots) + (freeSlots == 1 ? " vaga livre joga" : " vagas livres jogam") +
                      " com qualquer outro controle",
                  kSmall, kMuted, kW / 2, btn.y + btn.h + 24);
    else
        gfx_.text("Todos entraram! Aperte Começar agora para jogar", kSmall, kPlayerColor[2], kW / 2,
                  btn.y + btn.h + 24);
#ifdef __SWITCH__
    gfx_.text("A ou toque: entrar     Baixo e depois A (ou toque): começar agora", kSmall, kMuted, kW / 2,
              kH - 52);
    gfx_.text("Y: o resto joga neste controle     B: sair da vaga     + ou -: sair do jogo", kSmall, kMuted,
              kW / 2, kH - 24);
#else
    gfx_.text("A, Enter ou clique: entrar     Baixo e depois A (ou clique): começar agora", kSmall, kMuted,
              kW / 2, kH - 52);
    gfx_.text("Y ou Tab: o resto joga neste dispositivo     B ou Backspace: sair da vaga     Esc: menu",
              kSmall, kMuted, kW / 2, kH - 24);
#endif
}

void App::handle(const Input& in) {
    if (in.action == Action::ExitApp) {
        running_ = false;
        return;
    }
    if (!allowed(in)) return;
    if (screen_ != Screen::Game) {
        switch (in.action) {
            case Action::Up: case Action::Down: case Action::Left: case Action::Right:
            case Action::Confirm: case Action::Click: audio_.play(Sfx::Click); break;
            default: break;
        }
    }
    switch (screen_) {
        case Screen::Menu: menuInput(in); break;
        case Screen::Assign: assignInput(in); break;
        case Screen::Game: gameInput(in); break;
        case Screen::Result: resultInput(in); break;
    }
}

void App::update(float dt) {
    clock_ += dt;
    if (screen_ != Screen::Game) return;
    Session& s = *session_;
    for (int i = 0; i < s.game->cardCount(); ++i) {
        const float target = s.game->card(i).state == memory::CardState::Hidden ? 0.f : 1.f;
        const float step = 4.5f * dt;
        float& f = s.flip[i];
        f = f < target ? std::min(target, f + step) : std::max(target, f - step);
    }
    if (s.wrongTimer > 0) {
        s.wrongTimer -= dt;
        if (s.wrongTimer <= 0) audio_.play(Sfx::Wrong);
    }
    if (s.game->phase() == memory::Phase::Mismatch) {
        s.mismatchTimer -= dt;
        if (s.mismatchTimer <= 0) s.game->resolveMismatch();
    }
    if (s.game->finished()) {
        s.finishTimer -= dt;
        if (s.finishTimer <= 0) {
            screen_ = Screen::Result;
            audio_.play(Sfx::Win);
        }
    }
}

// ------------------------------------------------------------------ desenho

void App::renderScoreboard(int y) {
    const memory::Game& g = *session_->game;
    const int n = g.players(), w = 210, h = 64, gap = 22;
    const int x0 = (kW - (n * w + (n - 1) * gap)) / 2;
    for (int p = 0; p < n; ++p) {
        const bool active = screen_ == Screen::Game && !g.finished() && p == g.currentPlayer();
        SDL_Rect rc{x0 + p * (w + gap), y, w, h};
        if (active) {
            const float pulse = 0.5f + 0.5f * std::sin(clock_ * 6.f);
            gfx_.fillRound({rc.x - 7, rc.y - 7, rc.w + 14, rc.h + 14}, 24,
                           {kInk.r, kInk.g, kInk.b, Uint8(150 + 105 * pulse)});
        }
        Color c = kPlayerColor[p];
        if (!active && screen_ == Screen::Game) c.a = 150;
        gfx_.fillRound(rc, 18, c);
        gfx_.text("J" + std::to_string(p + 1), kMedium, kWhite, rc.x + 22, rc.y + h / 2, Align::Left);
        gfx_.text(std::to_string(g.score(p)), kMedium, kWhite, rc.x + w - 22, rc.y + h / 2, Align::Right);
    }
}

void App::renderMenu() {
    gfx_.text("Jogo da Memória", kBig, kInk, kW / 2, 110);
    const char* labels[3] = {"Jogadores", "Tamanho", "Tema"};
    const GridSize gs = kSizes[settings_.size];
    const std::string values[3] = {
        std::to_string(settings_.players),
        std::to_string(gs.cols) + " x " + std::to_string(gs.rows),
        themes_[settings_.theme].name};
    for (int row = 0; row < 3; ++row) {
        const SDL_Rect rc = menuRowRect(row);
        const bool sel = menuRow_ == row;
        if (sel) gfx_.fillRound({rc.x - 5, rc.y - 5, rc.w + 10, rc.h + 10}, 28, kPlayerColor[1]);
        gfx_.fillRound(rc, 24, sel ? kWhite : Color{255, 255, 255, 150});
        gfx_.text(labels[row], kMedium, kInk, rc.x + 32, rc.y + rc.h / 2, Align::Left);
        const int vx = rc.x + rc.w - 170;
        gfx_.text(values[row], kMedium, kInk, vx, rc.y + rc.h / 2);
        const Color ac = sel ? kPlayerColor[1] : kMuted;
        gfx_.arrow(vx - 110, rc.y + rc.h / 2, 13, true, ac);
        gfx_.arrow(vx + 110, rc.y + rc.h / 2, 13, false, ac);
    }
    // jogadores: bolinhas coloridas mostram quantos vão jogar
    for (int p = 0; p < settings_.players; ++p) {
        const SDL_Rect rc = menuRowRect(0);
        gfx_.fillRound({rc.x + 256 + p * 25, rc.y + 31, 18, 18}, 9, kPlayerColor[p]);
    }
    const SDL_Rect pb = menuRowRect(3);
    const bool sel = menuRow_ == 3;
    if (sel) gfx_.fillRound({pb.x - 6, pb.y - 6, pb.w + 12, pb.h + 12}, 40, kInk);
    gfx_.fillRound(pb, 36, kPlayerColor[2]);
    gfx_.text("Jogar!", kMedium, kWhite, pb.x + pb.w / 2, pb.y + pb.h / 2);
    gfx_.text(std::string("Setas: escolher e mudar     Enter ou A: confirmar     ") + kExitHint, kSmall,
              kMuted, kW / 2, kH - 28);
}

void App::renderGame() {
    Session& s = *session_;
    renderScoreboard(26);
    const int turn = s.game->currentPlayer();

    for (int i = 0; i < s.game->cardCount(); ++i) {
        const SDL_Rect rc = cardRect(i);
        if (i == s.cursor && !s.game->finished()) {
            const float pulse = 0.5f + 0.5f * std::sin(clock_ * 7.f);
            Color pc = kPlayerColor[turn];
            pc.a = Uint8(170 + 85 * pulse);
            gfx_.fillRound({rc.x - 8, rc.y - 8, rc.w + 16, rc.h + 16}, 26, pc);
            gfx_.fillRound({rc.x - 3, rc.y - 3, rc.w + 6, rc.h + 6}, 21, kBg);
        }
        const float p = s.flip[i];
        const float scale = std::fabs(std::cos(p * kPi));
        const int w = std::max(2, int(rc.w * scale));
        const SDL_Rect d{rc.x + (rc.w - w) / 2, rc.y, w, rc.h};
        if (p < 0.5f) {
            SDL_RenderCopy(gfx_.r, s.back, nullptr, &d);
        } else {
            const int owner = s.owner[i];
            gfx_.fillRound(d, 20, owner >= 0 ? kPlayerColor[owner] : kEdge);
            gfx_.fillRound({d.x + 4, d.y + 4, d.w - 8, d.h - 8}, 17, kWhite);
            const int inset = rc.w / 8;
            const SDL_Rect img{d.x + inset * w / rc.w, d.y + inset, w - 2 * inset * w / rc.w,
                               d.h - 2 * inset};
            SDL_RenderCopy(gfx_.r, s.faceTex[s.game->card(i).face], nullptr, &img);
        }
    }
    std::string who = "Vez do Jogador " + std::to_string(turn + 1);
    if (s.game->players() > 1) {
        // Vaga livre ao lado de jogadores com dispositivo próprio: só os "outros" jogam nela.
        const bool othersOwn = std::any_of(slots_.begin(), slots_.end(), [](int d) { return d >= 0 || d == kDeviceKeyboard || d == kDeviceMouse; });
        who += " (" + (slots_[turn] == kDeviceAny && othersOwn ? std::string("qualquer outro controle")
                                                              : deviceName(slots_[turn])) + ")";
    }
    gfx_.text(who, kSmall, kPlayerColor[turn], kW / 2, kH - 26);
    gfx_.text("Jogadas: " + std::to_string(s.game->moves()), kSmall, kMuted, kW - 40, kH - 26,
              Align::Right);
    gfx_.text(std::string("A: virar   B: menu") + (kExitHint[0] == '+' ? "   + ou -: sair" : ""), kSmall,
              kMuted, 40, kH - 26, Align::Left);
}

void App::renderResult() {
    const memory::Game& g = *session_->game;
    gfx_.text("Fim de jogo!", kBig, kInk, kW / 2, 120);
    const auto win = g.winners();
    std::string msg;
    if (g.players() == 1) msg = "Você achou todos os pares em " + std::to_string(g.moves()) + " jogadas!";
    else if (win.size() == 1) msg = "Parabéns, Jogador " + std::to_string(win[0] + 1) + "!";
    else {
        msg = "Empate entre ";
        for (size_t i = 0; i < win.size(); ++i)
            msg += (i ? (i + 1 == win.size() ? " e " : ", ") : "") + std::string("J") + std::to_string(win[i] + 1);
        msg += "!";
    }
    gfx_.text(msg, kMedium, kPlayerColor[win[0]], kW / 2, 240);
    renderScoreboard(330);
    gfx_.text(std::string("Enter ou A: jogar de novo     B: menu") +
                  (kExitHint[0] == '+' ? "     + ou -: sair" : ""),
              kSmall, kMuted, kW / 2, kH - 60);
}

void App::render() {
    gfx_.color(kBg);
    SDL_RenderClear(gfx_.r);
    switch (screen_) {
        case Screen::Menu: renderMenu(); break;
        case Screen::Assign: renderAssign(); break;
        case Screen::Game: renderGame(); break;
        case Screen::Result: renderResult(); break;
    }
    if (shotPending_) saveShot();
    SDL_RenderPresent(gfx_.r);
}

void App::run() {
    Uint64 last = SDL_GetPerformanceCounter();
    while (running_) {
        const Uint64 now = SDL_GetPerformanceCounter();
        const float dt = std::min(0.1f, float(now - last) / float(SDL_GetPerformanceFrequency()));
        last = now;
        pumpEvents();
        pumpScript(dt);
        std::vector<Input> batch;
        batch.swap(queue_);
        for (const auto& in : batch) {
            handle(in);
            if (!running_) break;
        }
        update(dt);
        render();
    }
}

}  // namespace

int main(int, char**) {
#ifdef __SWITCH__
    romfsInit();  // monta romfs:/ (os assets embutidos no .nro)
    socketInitializeDefault();
    nxlinkStdio();  // com `nxlink -s`, os logs aparecem no terminal do computador
#endif
    int rc = 0;
    {
        App app;
        if (!app.init()) rc = 1;
        else app.run();
    }  // o App precisa ser destruído antes de desmontar o romfs
#ifdef __SWITCH__
    socketExit();
    romfsExit();
#endif
    return rc;
}
