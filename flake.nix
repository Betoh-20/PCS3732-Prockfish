{
  description = "Prockfish — ambiente de desenvolvimento do tabuleiro de xadrez eletrônico";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # Só serve para o shell.nix: deixa `nix-shell` usar este flake (e o
    # flake.lock) sem exigir os experimental-features de flakes.
    flake-compat = {
      url = "github:edolstra/flake-compat";
      flake = false;
    };
  };

  outputs = { self, nixpkgs, flake-compat }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system:
        f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs:
        let
          # requirements.txt: python-chess, pygame, requests.
          python = pkgs.python3.withPackages (ps: with ps; [
            chess
            pygame
            requests
          ]);

          # app/gui.py procura por fontconfig (pygame.font.match_font) uma fonte
          # com os glifos U+2654–U+265F. Sem isso as peças viram K/Q/R/B/N/P.
          fontsConf = pkgs.makeFontsConf {
            fontDirectories = [ pkgs.dejavu_fonts pkgs.freefont_ttf pkgs.liberation_ttf ];
          };

          prockfish = pkgs.mkShell {
            name = "prockfish";

            packages = [
              python
              pkgs.stockfish
              # Makefile: atalhos para os modos de execução.
              pkgs.gnumake
            ];

            FONTCONFIG_FILE = fontsConf;
            CHESS_STOCKFISH_PATH = "${pkgs.stockfish}/bin/stockfish";

            shellHook = ''
              echo "prockfish — python ${pkgs.python3.version}, stockfish em ''$CHESS_STOCKFISH_PATH"
              echo "  make        # lista os modos de execução"
            '';
          };
        in
        {
          inherit prockfish;
          default = prockfish;
        });
    };
}
