#!/usr/bin/env bash
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/../Modular Synth/include"
DST="$HERE/src/marty"
FILES="DummyMemory.h IStorage.h InputManager.h Looper.h ShareManager.h SynthState.h page.h"

WRITE=0
case "${1:-}" in
  --write) WRITE=1 ;;
  ""     ) ;;
  *      ) echo "usage: $0 [--write]"; exit 2 ;;
esac

echo "canonical: Modular Synth/include   copies: PocketChord/src/marty"
rc=0

for f in $FILES; do
  if   [ ! -f "$SRC/$f" ];        then echo "missing upstream   $f"; rc=1
  elif [ ! -f "$DST/$f" ];        then echo "missing in sketch  $f"; rc=1
  elif cmp -s "$SRC/$f" "$DST/$f"; then echo "ok                 $f"
  elif [ "$WRITE" = 1 ];          then cp "$SRC/$f" "$DST/$f"; echo "updated            $f"
  else echo "drifted            $f   ($0 --write to refresh)"; rc=1
  fi
done

exit $rc
