# Jogo da Memória

Jogo da memória para 1 a 4 jogadores, para Mac e Nintendo Switch (homebrew).
Os jogadores se alternam no mesmo aparelho: acertou o par, joga de novo; errou, passa a vez.

- `core/`: regras do jogo (C++17, sem dependências)
- `src/main.cpp`: janela, entrada e desenho (SDL2)
- `tests/`: testes do núcleo
- `assets/themes/`: temas de cartas (uma pasta por tema, com `01.png`…`NN.png` e `back.png`)

## Compilar e jogar no Mac

    brew install cmake sdl2 sdl2_image sdl2_ttf
    cmake -S . -B build && cmake --build build
    ./build/memoria

Linux (Debian/Ubuntu): `sudo apt install cmake libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev`.

## Controles

| Ação | Teclado | Controle |
|---|---|---|
| Mover / escolher | setas ou WASD | direcional ou analógico esquerdo |
| Confirmar / virar carta | Enter ou espaço | A |
| Voltar ao menu | Backspace | B |
| Sair (no menu) | Esc | + |
| Tela cheia | F ou F11 | – |

O mouse (ou o toque na tela do Switch) também funciona: clique na carta ou nas opções do menu.

## Um controle por jogador

Com 2 a 4 jogadores, depois de "Jogar!" aparece a tela **Controles**: cada jogador aperta
A (ou Enter, no teclado) no seu controle para ocupar uma vaga. Quem apertou "Jogar!" já fica
com a vaga do Jogador 1.

- Na partida, só o controle do jogador da vez mexe no cursor; os outros são ignorados.
- Faltou controle? Aperte **Y** (ou **Tab**) e o resto dos jogadores divide aquele controle.
- **B** (ou Backspace) libera a vaga. **Esc** ou **+** volta ao menu.
- Se um controle desconectar no meio da partida, o jogo pausa e pede para atribuir de novo,
  sem perder a partida.
- Sozinho (1 jogador), qualquer controle serve.

## Testes

    ctest --test-dir build

## Novos temas

Crie `assets/themes/<nome>/` com 6 ou mais faces (`01.png`, `02.png`, …) e `back.png`,
de preferência 256x256 com fundo transparente. O tema aparece sozinho no menu.
