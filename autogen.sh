#!/bin/sh
# autogen script for fluxbox.

dothis() {
    echo "Executing:  $*"
    echo
    $*
    if [ $? -ne 0 ]; then
        echo -e '\n ERROR: Carefully read the error message and'
        echo      '        try to figure out what went wrong.'
        exit 1
    fi
}

rm -f config.cache

# Prefer autoreconf since it runs autopoint when gettext macros are needed.
dothis autoreconf -fi

echo 'Success, now continue with ./configure'
echo 'Use configure --help for detailed help.'
