// Núcleo do jogo da memória: sem dependência de SDL ou de qualquer plataforma.
#pragma once

#include <cstdint>
#include <vector>

namespace memory {

constexpr int kMaxPlayers = 4;

enum class CardState : std::uint8_t { Hidden, Revealed, Matched };

struct Card {
    int face = 0;  // índice da imagem do tema; cartas com o mesmo valor formam par
    CardState state = CardState::Hidden;
};

enum class Phase : std::uint8_t {
    PickFirst,   // esperando a 1ª carta do turno
    PickSecond,  // esperando a 2ª carta
    Mismatch,    // duas cartas diferentes à mostra; chame resolveMismatch()
    Finished,    // todos os pares foram achados
};

enum class FlipResult : std::uint8_t {
    Invalid,   // jogada ignorada (índice ruim, carta já aberta, fase errada)
    First,     // 1ª carta aberta
    Match,     // formou par; o mesmo jogador joga de novo (ou o jogo acabou)
    Mismatch,  // não formou par; chame resolveMismatch()
};

struct Config {
    int rows = 4;
    int cols = 4;
    int players = 1;  // 1..kMaxPlayers
    std::uint32_t seed = 0;
};

class Game {
public:
    // Lança std::invalid_argument se a config for inválida
    // (jogadores fora de 1..4, grade vazia ou com número ímpar de cartas).
    explicit Game(const Config& cfg);

    FlipResult flip(int index);
    // Vira de volta as duas cartas erradas e passa a vez ao próximo jogador.
    // Não faz nada fora da fase Mismatch.
    void resolveMismatch();

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    int cardCount() const { return static_cast<int>(cards_.size()); }
    int pairCount() const { return cardCount() / 2; }
    const Card& card(int index) const { return cards_.at(static_cast<size_t>(index)); }

    int players() const { return players_; }
    int currentPlayer() const { return current_; }
    int score(int player) const { return scores_.at(static_cast<size_t>(player)); }
    int moves() const { return moves_; }  // tentativas (pares de cartas viradas)
    Phase phase() const { return phase_; }
    bool finished() const { return phase_ == Phase::Finished; }

    // Jogadores com a maior pontuação (mais de um em caso de empate).
    std::vector<int> winners() const;

private:
    int rows_, cols_, players_;
    std::vector<Card> cards_;
    std::vector<int> scores_;
    int current_ = 0;
    int moves_ = 0;
    int first_ = -1;
    int second_ = -1;
    int matched_ = 0;
    Phase phase_ = Phase::PickFirst;
};

}  // namespace memory
