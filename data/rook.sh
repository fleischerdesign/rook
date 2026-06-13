#!/bin/sh
set -e

case "${1:-}" in
  serve|server|d|daemon)
    exec rookd "$@"
    ;;
  tui|terminal|t)
    exec rook-tui "$@"
    ;;
  gui|desktop|""|-h|--help|help)
    if [ "${1:-}" = "help" ] || [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
      cat <<'EOF'
Usage: rook [MODE] [ARGS...]

Modes:
  (none)        Start the GTK4 desktop application (default)
  gui           Start the GTK4 desktop application
  tui, terminal Start the terminal (FTXUI) application
  serve, server Start the headless gRPC daemon

Options:
  -h, --help    Show this help
EOF
      exit 0
    fi
    exec rook-gui "$@"
    ;;
  *)
    echo "rook: unknown mode '$1' — use 'rook help' for usage"
    exit 1
    ;;
esac
