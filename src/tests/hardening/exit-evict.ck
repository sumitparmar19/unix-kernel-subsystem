# -*- perl -*-
use strict; use warnings;
use tests::tests;
our ($test);
my @output = read_text_file ("$test.output");
# The per-round children exit in an unpredictable order.
@output = grep (!/^evict-child: exit\([-0-9]+\)\s*$/, @output);
common_checks ("run", @output);
compare_output ("run", \@output, [<<'EOF']);
(exit-evict) begin
(exit-evict) survived 30 rounds of exit-during-eviction
(exit-evict) end
exit-evict: exit(0)
EOF
pass;
