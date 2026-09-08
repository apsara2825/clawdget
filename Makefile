# clawdget Makefile
#   make debug      - local x86 build (for tests)
#   make test       - build + run unit tests
#   make e2e        - full loop against a local mock SSE server
#   make mips       - cross-compile (generic, see below)
#
# Cross compiling: point CROSS at your own toolchain prefix and CURL_INC /
# CURL_LIB at a libcurl built FOR THE TARGET:
#   make mips CROSS=mipsel-openwrt-linux- \
#        CURL_INC=/path/target-curl/include CURL_LIB=/path/target-curl/lib
# Or use build-static.sh, which builds mbedtls + curl statically and
# produces a single-file binary with zero runtime dependencies:
#   CROSS=mipsel-linux-musl- ./build-static.sh
# Personal overrides go in local.mk (git-ignored).

-include local.mk

CROSS    ?= mipsel-openwrt-linux-
CURL_INC ?=
CURL_LIB ?=

SRC      := $(wildcard src/*.c) thirdparty/cjson.c
# optional channels: make WEIXIN=1 (WeChat iLink channel, +~40KB)
WEIXIN  ?= 0
ifeq ($(WEIXIN),1)
SRC      += src/weixin/wx_api.c src/weixin/wx.c thirdparty/qrcodegen.c
endif
CFLAGS   := -std=gnu99 -Wall -Wextra -Os -Isrc -Ithirdparty
ifeq ($(WEIXIN),1)
CFLAGS   += -DPC_WEIXIN
endif
X86FLAGS := -std=gnu99 -Wall -Wextra -g -O0 -Isrc -Ithirdparty
ifeq ($(WEIXIN),1)
X86FLAGS += -DPC_WEIXIN
endif
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
ARCH_FLAGS ?= -mips32r2
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
