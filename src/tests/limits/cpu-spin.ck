# -*- perl -*-
use strict; use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(cpu-spin) begin
(cpu-spin) set CPU budget to 50 ticks
(cpu-spin) spinning; the kernel should kill us at the budget
cpu-spin: exit(-1)
EOF
pass;
