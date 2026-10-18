# -*- perl -*-
use strict; use warnings;
use tests::tests;
our ($test);
my @output = read_text_file ("$test.output");
# Attacker helpers exit in an unpredictable order; deep ones are killed
# (exit -1) when the subtree page cap bites.
@output = grep (!/^esc-pgchild: exit\([-0-9]+\)\s*$/, @output);
common_checks ("run", @output);
compare_output ("run", \@output, [<<'EOF']);
(esc-pages) begin
(esc-pages) page limit set to 80
(esc-pages) launched the attacker
(esc-pages) escape contained below the cap
(esc-pages) end
esc-pages: exit(0)
EOF
pass;
