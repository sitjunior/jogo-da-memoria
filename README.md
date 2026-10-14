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
- O jogo **nunca começa sozinho**: mesmo com todos já dentro, é preciso usar o botão **Começar agora**
  na parte de baixo da tela (pode ser antes de todos entrarem): clique ou toque nele, aperte **+** no controle, ou aperte **Baixo** e depois **A**/Enter.
  As vagas que ninguém assumiu jogam com qualquer dispositivo.
- Faltou controle? Aperte **Y** (ou **Tab**) e o resto dos jogadores divide aquele controle.
- **B** (ou Backspace) libera a vaga. **Esc** volta ao menu (no controle, **+** aqui é "começar agora").
- Se um controle desconectar no meio da partida, o jogo pausa e pede para atribuir de novo,
  sem perder a partida.
- Sozinho (1 jogador), qualquer controle serve.
- O jogo usa no máximo **4 controles**, mesmo que o Switch tenha mais conectados (os extras são
  ignorados), e os controles são chamados de "Controle 1" a "Controle 4".
- No computador, **teclado e mouse contam como dois dispositivos**: com 2 jogadores dá para um usar o
  teclado e o outro o mouse. No Switch só contam os controles; o toque na tela não é um jogador.

## Testes

    ctest --test-dir build

## Novos temas

Crie `assets/themes/<nome>/` com 6 ou mais faces (`01.png`, `02.png`, …) e `back.png`,
de preferência 256x256 com fundo transparente. O tema aparece sozinho no menu.

## Compilar para o Nintendo Switch (homebrew)

Gera um `jogo-da-memoria.nro` com todos os assets embutidos (RomFS).

1. Instale o devkitPro e o gerenciador `dkp-pacman` no Mac: baixe o instalador `.pkg` em
   https://github.com/devkitPro/pacman/releases e abra.
2. Instale o toolchain e as bibliotecas:

       sudo dkp-pacman -Sy
       sudo dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_image switch-sdl2_ttf switch-pkg-config

3. Compile (na pasta do projeto):

       export DEVKITPRO=/opt/devkitpro
       export DEVKITA64=$DEVKITPRO/devkitA64
       make

   Para recompilar do zero: `make clean && make`.

4. Copie para o cartão SD do Switch:

       /switch/jogo-da-memoria/jogo-da-memoria.nro

   Abra pelo Homebrew Menu (álbum, segurando R). Alternativa sem tirar o cartão: no hbmenu
   aperte Y (netloader) e rode `$DEVKITPRO/tools/bin/nxlink -a <IP do Switch> jogo-da-memoria.nro`.

Os assets ficam dentro do `.nro`. Se preferir, também são procurados em
`sdmc:/switch/jogo-da-memoria/assets` e ao lado do `.nro`.

## Música e sons

A música e os efeitos (virar carta, acerto, erro, vitória) são sintetizados por
`tools/make_audio.py` e ficam em `assets/audio/*.wav`. Para regerar: `python3 tools/make_audio.py`.
A música fica sempre ligada. Sem dispositivo de áudio, o jogo segue mudo.

## Se algo não responder no Switch

- Com um só controle (ex.: portátil), o jogo vai direto para a partida e todos dividem o controle,
  mesmo com 2 a 4 jogadores. A tela "Controles" só aparece quando há 2 ou mais controles.
- Botões A e B: no Switch o SDL os entrega trocados, então o jogo já os inverte sozinho.
- Para ver o que o jogo recebe, abra o hbmenu, aperte **Y** (netloader) e rode no Mac:

      $DEVKITPRO/tools/bin/nxlink -s -a <IP do Switch> jogo-da-memoria.nro

  Os botões apertados aparecem no terminal.
