# -*- perl -*-
use strict; use warnings;
use tests::tests;
our ($test);
my @output = read_text_file ("$test.output");
# The worker children exit in an unpredictable order.
@output = grep (!/^grind-child: exit\([-0-9]+\)\s*$/, @output);
common_checks ("run", @output);
compare_output ("run", \@output, [<<'EOF']);
(rw-pressure) begin
(rw-pressure) create the data file
(rw-pressure) open to seed
(rw-pressure) seed 65536 bytes
(rw-pressure) all 8 readers/writers finished without a kernel fault
(rw-pressure) end
rw-pressure: exit(0)
EOF
pass;
