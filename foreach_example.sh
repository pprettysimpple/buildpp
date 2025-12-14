#!/bin/sh
set -e
CMD="$@"
for example in example_01_simple example_02_sanitizers example_05_hairy; do
    cd $example
    echo "In $example"
    bash -c "$CMD"
    cd - > /dev/null
done
