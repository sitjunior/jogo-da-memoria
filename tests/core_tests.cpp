#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>

#include "game.hpp"

using namespace memory;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

// Acha o índice de outra carta com a mesma face de `index`.
static int partnerOf(const Game& g, int index) {
    for (int i = 0; i < g.cardCount(); ++i)
        if (i != index && g.card(i).face == g.card(index).face) return i;
    return -1;
}

// Acha duas cartas Hidden com faces diferentes.
static void twoDifferent(const Game& g, int& a, int& b) {
    a = b = -1;
    for (int i = 0; i < g.cardCount(); ++i) {
        if (g.card(i).state != CardState::Hidden) continue;
        if (a < 0) a = i;
        else if (g.card(i).face != g.card(a).face) { b = i; return; }
    }
}

static void testDeckHasExactPairs() {
    for (int players = 1; players <= 4; ++players) {
        Game g({6, 4, players, 42});
        std::map<int, int> counts;
        for (int i = 0; i < g.cardCount(); ++i) ++counts[g.card(i).face];
        CHECK(counts.size() == 12);
        for (auto& kv : counts) CHECK(kv.second == 2);
    }
}

static void testShuffleIsDeterministicAndVaries() {
    Game a({4, 4, 1, 7}), b({4, 4, 1, 7}), c({4, 4, 1, 8});
    bool sameAB = true, sameAC = true;
    for (int i = 0; i < 16; ++i) {
        sameAB &= a.card(i).face == b.card(i).face;
        sameAC &= a.card(i).face == c.card(i).face;
    }
    CHECK(sameAB);
    CHECK(!sameAC);
}

static void testInvalidConfig() {
    auto throws = [](Config cfg) {
        try { Game g(cfg); } catch (const std::invalid_argument&) { return true; }
        return false;
    };
    CHECK(throws({4, 4, 0, 0}));
    CHECK(throws({4, 4, 5, 0}));
    CHECK(throws({3, 3, 1, 0}));  // 9 cartas
    CHECK(throws({0, 4, 1, 0}));
}

static void testMatchKeepsTurnAndScores() {
    Game g({4, 4, 2, 1});
    CHECK(g.flip(0) == FlipResult::First);
    CHECK(g.phase() == Phase::PickSecond);
    CHECK(g.flip(partnerOf(g, 0)) == FlipResult::Match);
    CHECK(g.currentPlayer() == 0);
    CHECK(g.score(0) == 1 && g.score(1) == 0);
    CHECK(g.phase() == Phase::PickFirst);
    CHECK(g.moves() == 1);
}

static void testMismatchPassesTurn() {
    Game g({4, 4, 3, 1});
    int a, b;
    twoDifferent(g, a, b);
    CHECK(g.flip(a) == FlipResult::First);
    CHECK(g.flip(b) == FlipResult::Mismatch);
    CHECK(g.phase() == Phase::Mismatch);
    CHECK(g.flip(0) == FlipResult::Invalid);  // bloqueado até resolver
    CHECK(g.currentPlayer() == 0);
    g.resolveMismatch();
    CHECK(g.card(a).state == CardState::Hidden && g.card(b).state == CardState::Hidden);
    CHECK(g.currentPlayer() == 1);
    CHECK(g.phase() == Phase::PickFirst);
}

static void testTurnWrapsAround() {
    Game g({4, 4, 4, 1});
    for (int expected = 1; expected <= 4; ++expected) {
        int a, b;
        twoDifferent(g, a, b);
        g.flip(a);
        g.flip(b);
        g.resolveMismatch();
        CHECK(g.currentPlayer() == expected % 4);
    }
}

static void testInvalidFlips() {
    Game g({4, 4, 1, 1});
    CHECK(g.flip(-1) == FlipResult::Invalid);
    CHECK(g.flip(16) == FlipResult::Invalid);
    CHECK(g.flip(3) == FlipResult::First);
    CHECK(g.flip(3) == FlipResult::Invalid);  // mesma carta
    g.resolveMismatch();                       // fora da fase: sem efeito
    CHECK(g.phase() == Phase::PickSecond);
}

static void testFullGameAndWinners() {
    Game g({2, 2, 2, 5});  // 2 pares
    // Jogador 0 acha o 1º par, depois erra; jogador 1 acha o último.
    int first = 0;
    g.flip(first);
    CHECK(g.flip(partnerOf(g, first)) == FlipResult::Match);
    CHECK(!g.finished());
    int a, b;
    // Só restam as 2 cartas do último par: forçar um erro é impossível,
    // então o mesmo jogador fecha o jogo.
    twoDifferent(g, a, b);
    CHECK(b == -1);
    for (int i = 0; i < 4; ++i) {
        if (g.card(i).state == CardState::Hidden) { a = i; break; }
    }
    g.flip(a);
    CHECK(g.flip(partnerOf(g, a)) == FlipResult::Match);
    CHECK(g.finished());
    CHECK(g.score(0) == 2);
    auto w = g.winners();
    CHECK(w.size() == 1 && w[0] == 0);
    CHECK(g.flip(0) == FlipResult::Invalid);
}

// Joga um par correto com o jogador atual.
static void playMatch(Game& g) {
    int first = -1;
    for (int i = 0; i < g.cardCount(); ++i)
        if (g.card(i).state == CardState::Hidden) { first = i; break; }
    g.flip(first);
    CHECK(g.flip(partnerOf(g, first)) == FlipResult::Match);
}

// Joga um erro e passa a vez.
static void playMismatch(Game& g) {
    int a, b;
    twoDifferent(g, a, b);
    CHECK(b >= 0);
    g.flip(a);
    CHECK(g.flip(b) == FlipResult::Mismatch);
    g.resolveMismatch();
}

static void testTie() {
    Game g({3, 4, 2, 9});  // 6 pares
    playMatch(g); playMatch(g); playMatch(g);  // J0: 3
    playMismatch(g);                           // vez do J1
    playMatch(g); playMatch(g);                // J1: 2
    CHECK(!g.finished());
    playMatch(g);                              // J1: 3, fim
    CHECK(g.finished());
    CHECK(g.score(0) == 3 && g.score(1) == 3);
    auto w = g.winners();
    CHECK(w.size() == 2 && w[0] == 0 && w[1] == 1);
}

int main() {
    testDeckHasExactPairs();
    testShuffleIsDeterministicAndVaries();
    testInvalidConfig();
    testMatchKeepsTurnAndScores();
    testMismatchPassesTurn();
    testTurnWrapsAround();
    testInvalidFlips();
    testFullGameAndWinners();
    testTie();
    std::puts("todos os testes passaram");
    return 0;
}
