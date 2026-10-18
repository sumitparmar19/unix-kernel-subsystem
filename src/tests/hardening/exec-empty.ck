# -*- perl -*-
use strict; use warnings;
use tests::tests;
our ($test);
my @output = read_text_file ("$test.output");
# Each refused load prints a kernel diagnostic, and the empty-named child
# process then exits -1 (its name is the empty program name); drop both.
@output = grep (!/^load: .*open failed\s*$/ && !/^: exit\(-1\)\s*$/, @output);
common_checks ("run", @output);
compare_output ("run", \@output, [<<'EOF']);
(exec-empty) begin
(exec-empty) exec of "" is refused
(exec-empty) exec of all-spaces is refused
(exec-empty) exec_redirect of "" is refused
(exec-empty) kernel still works afterward
(exec-empty) end
exec-empty: exit(0)
EOF
pass;
