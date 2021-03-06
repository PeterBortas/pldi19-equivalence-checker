#!/bin/bash

cd /home/equivalence/equivalence-checker
touch bin/envvars

rm -rf src/ext/z3

# get links to previously compiled CVC4 / Z3
ln -s /home/equivalence/base/src/ext/z3 src/ext/z3
ln -s /home/equivalence/base/src/ext/cvc4-1.6-build src/ext/cvc4-1.6-build
ln -s /home/equivalence/base/src/ext/cvc4-1.6 src/ext/cvc4-1.6

# fix PATH
echo "export PATH=\"\$HOME/equivalence-checker/bin:\$PATH\"" >> ~/.bashrc
echo "export PATH=\"\$HOME/SageMath:\$PATH\"" >> ~/.bashrc


