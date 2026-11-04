#!/bin/sh
# PC run of the forecast test core, using the node's own sources.
# Run from the agrinode_forecast_test folder:  sh host_test/run_host_test.sh
set -e
NODE=../agri_node_test
OUT=${TMPDIR:-/tmp}/ft_host_test
INC="-Imain -I$NODE/main -I$NODE/main/host_test/stubs -I$NODE/components/blight_forecast/include \
     -I$NODE/components/blight_forecast -I$NODE/components/blight_forecast/host_test \
     -I$NODE/components/late_blight_ir/include"

# Test core: strict warnings.
gcc -std=c99 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror $INC -c main/ft_core.c -o "$OUT.core.o"
gcc -std=c99 -O2 -Wall -Wextra -Werror $INC -c host_test/test_ft_core.c -o "$OUT.test.o"
# Node sources, unchanged (the emlearn headers in edge_ai.c have unused parameters).
gcc -std=c99 -O2 -Wall -Wno-unused-parameter $INC -c $NODE/main/edge_ai.c -o "$OUT.edge.o"
gcc -std=c99 -O2 -Wall $INC -c $NODE/components/late_blight_ir/late_blight_ir.c -o "$OUT.ir.o"
gcc -std=c99 -O2 -Wall $INC -c $NODE/components/blight_forecast/blight_forecast.c -o "$OUT.bf.o"
gcc -std=c99 -O2 -Wall $INC -c $NODE/components/blight_forecast/blight_forecast_features.c -o "$OUT.bff.o"
gcc "$OUT".*.o -lm -o "$OUT"
"$OUT"
