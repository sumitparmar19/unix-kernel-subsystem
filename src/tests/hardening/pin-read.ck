# -*- perl -*-
use strict; use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pin-read) begin
(pin-read) create the small file
(pin-read) open to fill it
(pin-read) wrote 200 bytes
(pin-read) reopen to read
(pin-read) read returned the file's 200 bytes
(pin-read) contents intact; the kernel survived the huge-buffer read
(pin-read) end
pin-read: exit(0)
EOF

pass;
