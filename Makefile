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

SRC      := $(filter-out src/gateway.c,$(wildcard src/*.c)) thirdparty/cjson.c
# optional channels: make WEIXIN=1 (WeChat iLink channel, +~40KB)
WEIXIN   ?= 0
TELEGRAM ?= 0
ifeq ($(WEIXIN),1)
SRC      += src/weixin/wx_api.c src/weixin/wx.c thirdparty/qrcodegen.c
endif
ifeq ($(TELEGRAM),1)
SRC      += src/telegram/tg_api.c src/telegram/tg.c
endif
ifneq ($(filter 1,$(WEIXIN) $(TELEGRAM)),)
SRC      += src/gateway.c
endif
CFLAGS   := -std=gnu99 -Wall -Wextra -Os -Isrc -Ithirdparty
ifeq ($(WEIXIN),1)
CFLAGS   += -DPC_WEIXIN
endif
ifeq ($(TELEGRAM),1)
CFLAGS   += -DPC_TELEGRAM
endif
X86FLAGS := -std=gnu99 -Wall -Wextra -g -O0 -Isrc -Ithirdparty
ifeq ($(WEIXIN),1)
X86FLAGS += -DPC_WEIXIN
endif
ifeq ($(TELEGRAM),1)
X86FLAGS += -DPC_TELEGRAM
endif
LDLIBS   := -lcurl -lpthread

.PHONY: all debug test e2e mips clean FORCE

# force rebuild when compile flags change (e.g. switching WEIXIN/TELEGRAM)
BUILD_FLAGS := $(CFLAGS) $(X86FLAGS) $(LDLIBS)
flags.stamp: FORCE
	@printf '%s' '$(BUILD_FLAGS)' | cmp -s - $@ 2>/dev/null || \
		printf '%s' '$(BUILD_FLAGS)' > $@

all: debug

build-x86:
	mkdir -p build-x86

build-x86/clawdget: $(SRC) flags.stamp | build-x86
	$(CC) $(X86FLAGS) $(SRC) -o $@ $(LDLIBS)

debug: build-x86/clawdget

tg-e2e:
	@$(MAKE) --no-print-directory debug WEIXIN=1 TELEGRAM=1
	@python3 test/mock_tg.py 8793 > test/tg_mock.log 2>&1 & \
	pid=$$!; sleep 0.8; \
	rm -rf test/tg_home; \
	CLAWDGET_CONFIG=test/tg_config.json CLAWDGET_HOME=test/tg_home \
		./build-x86/clawdget gateway -y > test/tg_run.log 2>&1; \
	kill $$pid 2>/dev/null; \
	grep -E "\[tg\]" test/tg_run.log | head -5; \
	echo "--- mock sendMessage:"; grep -a "SENT" test/tg_mock.log | head -2; \
	test -f test/tg_home/telegram/offset.txt && echo "offset 持久化 OK"

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

# mips: flags.stamp dynamic link (-lcurl) by default; or set CURL_A=<libcurl.a path>
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
