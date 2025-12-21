#!/bin/sh
set -e
CMD="$@"
for howto in how_to/*; do
    cd $howto
    echo "At $howto"
    bash -c "$CMD"
    cd - > /dev/null
done
