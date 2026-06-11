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

        sherpa-onnx = pkgs.stdenv.mkDerivation {
          pname = "sherpa-onnx";
          version = "1.13.2";
          src = pkgs.fetchurl {
            url = "https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.2/sherpa-onnx-v1.13.2-linux-x64-shared.tar.bz2";
            hash = "sha256-HvZ0FTX3r01p45T9RAqAcQgDbSbtT1QmYBkQGdpcDao=";
          };
          espeak_data = pkgs.fetchurl {
            url = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/espeak-ng-data.tar.bz2";
            hash = "sha256-QTXM+C4fQGE0kcCHTUlFrp6ceECTPY4lpvngA9nr9TM=";
          };
          nativeBuildInputs = [ pkgs.autoPatchelfHook ];
          buildInputs = [ pkgs.onnxruntime ];
          installPhase = ''
            mkdir -p $out/lib $out/include/sherpa-onnx/c-api
            cp lib/libsherpa-onnx-c-api.so $out/lib/
            cp lib/libsherpa-onnx-cxx-api.so $out/lib/
            cp lib/libonnxruntime.so $out/lib/
            cp include/sherpa-onnx/c-api/c-api.h $out/include/sherpa-onnx/c-api/
            cp include/sherpa-onnx/c-api/cxx-api.h $out/include/sherpa-onnx/c-api/
            mkdir -p $out/share/sherpa-onnx
            tar xf $espeak_data -C $out/share/sherpa-onnx
          '';
        };
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
            gcc15
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
            onnxruntime
            alsa-lib
            libpulseaudio
            pulseaudio
            ollama
            cmark-gfm
            sherpa-onnx
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
