#!/bin/bash
set -e   # exit on error

cd "$(dirname "$0")"

# param
MODE="${1:-run}"   # default run


case "$MODE" in
    clean)
        echo "[CLEAN]"
        make TOOLCHAIN_PREFIX=x86_64-elf- clean
        ;;

    build)
        echo "[BUILD]"
        make TOOLCHAIN_PREFIX=x86_64-elf- all
        ;;

    run)
        echo "[BUILD]"
        make TOOLCHAIN_PREFIX=x86_64-elf- all
        ;;

    rebuild)
        echo "[CLEAN]"
        make TOOLCHAIN_PREFIX=x86_64-elf- clean
        echo "[BUILD]"
        make TOOLCHAIN_PREFIX=x86_64-elf- all
        ;;

    *)
        echo "Usage: $0 {clean|build|run|rebuild}"
        exit 1
        ;;
esac