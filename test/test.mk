# test/test.mk - included by the top Makefile.
#
# Dependency-free self-tests: each script under test/ exercises one slice
# of the drone (AutoIP claim, IPv6 link-local, mDNS announce, ICMP ping
# over the qeneth-style UDP-socket link, MQTT command round-trip).
# Shared helpers live in utils/drone.sh.
#
# The runner follows the GNU autotools `make check` convention: each test
# exits 0 (PASS), 77 (SKIP), or non-zero (FAIL); the summary block is the
# familiar autotools format.

TESTS = test/autoip.sh test/ipv6.sh test/mdns.sh test/pair.sh test/mqtt.sh

.PHONY: check

check: $(BIN)
	@pass=0; fail=0; skip=0; \
	for t in $(TESTS); do \
		name=$$(basename $$t .sh); \
		log=$$(mktemp); \
		$$t >$$log 2>&1; rc=$$?; \
		case $$rc in \
		0)  echo "PASS: $$name"; pass=$$((pass+1)) ;; \
		77) echo "SKIP: $$name"; skip=$$((skip+1)) ;; \
		*)  echo "FAIL: $$name"; fail=$$((fail+1)); cat $$log ;; \
		esac; \
		rm -f $$log; \
	done; \
	total=$$((pass+fail+skip)); \
	echo ""; \
	echo "============================================================================"; \
	echo "Testsuite summary for drone"; \
	echo "============================================================================"; \
	printf "# TOTAL: %d\n# PASS:  %d\n# SKIP:  %d\n# XFAIL: 0\n# FAIL:  %d\n# XPASS: 0\n# ERROR: 0\n" \
	       $$total $$pass $$skip $$fail; \
	echo "============================================================================"; \
	[ $$fail -eq 0 ]
