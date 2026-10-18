# -*- perl -*-
use strict; use warnings;
use tests::tests;
our ($test);
my @output = read_text_file ("$test.output");
# Attacker helpers exit in an unpredictable order with varying statuses.
@output = grep (!/^esc-child: exit\([-0-9]+\)\s*$/, @output);
common_checks ("run", @output);
compare_output ("run", \@output, [<<'EOF']);
(esc-procs) begin
(esc-procs) process limit set to 4
(esc-procs) launched the attacker
(esc-procs) escape contained below the cap
(esc-procs) end
esc-procs: exit(0)
EOF
pass;
