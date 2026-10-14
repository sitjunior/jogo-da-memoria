# Jogo da Memória

Jogo da memória para 1 a 4 jogadores, para Mac e Nintendo Switch (homebrew).

- `core/`: regras do jogo (C++17, sem dependências)
- `tests/`: testes do núcleo
- `assets/themes/`: temas de cartas

## Testes

    cmake -S . -B build && cmake --build build && ctest --test-dir build
