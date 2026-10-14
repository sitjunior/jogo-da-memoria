#include "game.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace memory {

Game::Game(const Config& cfg)
    : rows_(cfg.rows), cols_(cfg.cols), players_(cfg.players) {
    if (players_ < 1 || players_ > kMaxPlayers)
        throw std::invalid_argument("players must be in 1..4");
    if (rows_ < 1 || cols_ < 1 || (rows_ * cols_) % 2 != 0)
        throw std::invalid_argument("grid must have an even, positive number of cards");

    const int total = rows_ * cols_;
    cards_.resize(static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) cards_[static_cast<size_t>(i)].face = i / 2;

    std::mt19937 rng(cfg.seed);
    std::shuffle(cards_.begin(), cards_.end(), rng);
    scores_.assign(static_cast<size_t>(players_), 0);
}

FlipResult Game::flip(int index) {
    if (phase_ != Phase::PickFirst && phase_ != Phase::PickSecond) return FlipResult::Invalid;
    if (index < 0 || index >= cardCount()) return FlipResult::Invalid;
    Card& c = cards_[static_cast<size_t>(index)];
    if (c.state != CardState::Hidden) return FlipResult::Invalid;

    c.state = CardState::Revealed;

    if (phase_ == Phase::PickFirst) {
        first_ = index;
        phase_ = Phase::PickSecond;
        return FlipResult::First;
    }

    second_ = index;
    ++moves_;
    Card& a = cards_[static_cast<size_t>(first_)];
    if (a.face == c.face) {
        a.state = c.state = CardState::Matched;
        ++scores_[static_cast<size_t>(current_)];
        ++matched_;
        first_ = second_ = -1;
        phase_ = matched_ == pairCount() ? Phase::Finished : Phase::PickFirst;
        return FlipResult::Match;
    }
    phase_ = Phase::Mismatch;
    return FlipResult::Mismatch;
}

void Game::resolveMismatch() {
    if (phase_ != Phase::Mismatch) return;
    cards_[static_cast<size_t>(first_)].state = CardState::Hidden;
    cards_[static_cast<size_t>(second_)].state = CardState::Hidden;
    first_ = second_ = -1;
    current_ = (current_ + 1) % players_;
    phase_ = Phase::PickFirst;
}

std::vector<int> Game::winners() const {
    const int best = *std::max_element(scores_.begin(), scores_.end());
    std::vector<int> result;
    for (int p = 0; p < players_; ++p)
        if (scores_[static_cast<size_t>(p)] == best) result.push_back(p);
    return result;
}

}  // namespace memory
