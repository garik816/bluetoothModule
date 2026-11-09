#!/bin/sh
set -eu
cc -std=c11 -Wall -Wextra -Werror -pedantic -I main tests/test_kcx_protocol.c main/kcx_protocol.c -o /tmp/test_kcx_protocol
/tmp/test_kcx_protocol
