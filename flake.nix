{
  description = "Rook -- Multi-Modal Voice AI Agent";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          name = "rook-dev-shell";

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            gettext
            desktop-file-utils
            gcc14
            python3
            gobject-introspection
            clang-tools
            gdb
            valgrind
          ];

          buildInputs = with pkgs; [
            gtk4
            libadwaita
            libayatana-appindicator
            nlohmann_json
            curl
            libsecret
            spdlog
            ftxui
            grpc
            protobuf
            gtest
            piper-tts
            whisper-cpp
            ollama
            cmark-gfm
          ];

          env = {
            LIBCLANG_PATH = "${pkgs.libclang.lib}/lib";
          };

          shellHook = ''
            export XDG_DATA_DIRS="${pkgs.gtk4.dev}/share:${pkgs.libadwaita.dev}/share:${pkgs.gobject-introspection}/share:$XDG_DATA_DIRS"
            echo "Entering Rook development environment"
          '';
        };

        packages.default = pkgs.stdenv.mkDerivation {
          pname = "rook";
          version = "0.1.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [
            meson
            ninja
            pkg-config
            gettext
          ];

          buildInputs = with pkgs; [
            gtk4
            libadwaita
            libayatana-appindicator
            nlohmann_json
            curl
            libsecret
            spdlog
            ftxui
            grpc
            protobuf
          ];
        };
      }
    );
}
