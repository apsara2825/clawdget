# clawdget Makefile
#   make debug      - local x86 build (for tests)
#   make test       - build + run unit tests
#   make e2e        - full loop against a local mock SSE server
#   make mips       - cross-compile for MT7628 (mipsel uClibc)
#
# MIPS libcurl headers/libs come from the buildroot build_dir; override via
# local.mk or environment:
#   CURL_INC - path to libcurl headers (cross)
#   CURL_LIB - path to libcurl.so (cross)

-include local.mk

CROSS    ?= /root/aiagent/mipsgcc/bin/mipsel-openwrt-linux-uclibc-
CURL_INC ?= $(HOME)/aiagent/clawdget/build-mips/include
CURL_LIB ?= $(HOME)/aiagent/clawdget/build-mips/lib

SRC      := $(wildcard src/*.c) thirdparty/cjson.c
CFLAGS   := -std=gnu99 -Wall -Wextra -Os -Isrc -Ithirdparty
X86FLAGS := -std=gnu99 -Wall -Wextra -g -O0 -Isrc -Ithirdparty
LDLIBS   := -lcurl -lpthread

.PHONY: all debug test e2e mips clean

all: debug

build-x86:
	mkdir -p build-x86

build-x86/clawdget: $(SRC) | build-x86
	$(CC) $(X86FLAGS) $(SRC) -o $@ $(LDLIBS)

debug: build-x86/clawdget

test: debug
	$(CC) $(X86FLAGS) -Isrc test/test_sse.c thirdparty/cjson.c src/provider.c src/http.c src/spinner.c -o build-x86/test_sse $(LDLIBS)
	./build-x86/test_sse && echo "test_sse OK"

e2e: debug
	@python3 test/mock_server.py 8791 2>test/mock.log & \
	pid=$$!; sleep 0.6; \
	rm -rf test/e2e_home; \
	CLAWDGET_CONFIG=test/e2e_config.json CLAWDGET_HOME=test/e2e_home \
		./build-x86/clawdget -n "list the files in the workspace"; \
	rc=$$?; kill $$pid 2>/dev/null; exit $$rc

# mips: dynamic link (-lcurl) by default; or set CURL_A=<libcurl.a path>
# to link libcurl statically (remaining NEEDED libs must exist on device,
# e.g. -lssl/-lcrypto from the firmware).
MIPS_LDFLAGS := $(if $(CURL_A),$(CURL_A),-lcurl) $(MIPS_LIBS)


mips:
	mkdir -p build-mips
	$(CROSS)gcc $(CFLAGS) -mips32r2 \
		-I$(CURL_INC) -L$(CURL_LIB) \
		$(SRC) -o build-mips/clawdget $(MIPS_LDFLAGS) $(MIPS_LIBS) -ldl -lpthread
	$(CROSS)strip build-mips/clawdget
	@echo "--- ELF check ---"
	file build-mips/clawdget
	$(CROSS)readelf -d build-mips/clawdget | grep NEEDED

clean:
	rm -rf build-x86 build-mips
