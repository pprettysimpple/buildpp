#!/bin/sh
set -e
CMD="$@"
for example in example_*; do
    cd $example
    echo "At $example"
    bash -c "$CMD"
    cd - > /dev/null
done
