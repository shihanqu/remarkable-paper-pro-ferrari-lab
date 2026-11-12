# Ferrari Lab — Cross-Compilation & Deployment Makefile
# Target: reMarkable Paper Pro (NXP i.MX8M Mini, 4x Cortex-A53, aarch64-linux-gnu, glibc 2.38)

RMPP_HOST ?= 10.11.99.1
RMPP_USER ?= root
RMPP_DEST ?= /home/root/xovi/exthome/appload/ferrari_lab

SRC       := ferrari_lab/main.cpp
HEADERS   := $(wildcard ferrari_lab/*.h)
OUT       := ferrari_lab/ferrari_lab

# Auto-detect Zig C++ cross-compiler first, falling back to aarch64-linux-gnu-g++
ZIG ?= $(shell command -v zig 2>/dev/null)
ifneq ($(ZIG),)
    CXX_CMD := $(ZIG) c++ -target aarch64-linux-gnu.2.38
else ifneq ($(wildcard .toolchains/zig/zig),)
    CXX_CMD := .toolchains/zig/zig c++ -target aarch64-linux-gnu.2.38
else
    CXX_CMD := aarch64-linux-gnu-g++
endif

CXXFLAGS := -O3 -ffast-math -mcpu=cortex_a53+neon -std=c++17 -Wall -Wno-unused-function
LDFLAGS  := -lpthread -lm

.PHONY: all build deploy clean

all: build

build: $(OUT)

$(OUT): $(SRC) $(HEADERS)
	$(CXX_CMD) $(CXXFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

deploy: build
	ssh $(RMPP_USER)@$(RMPP_HOST) "mkdir -p $(RMPP_DEST) && killall ferrari_lab 2>/dev/null || true"
	scp $(OUT) ferrari_lab/external.manifest.json ferrari_lab/icon.png $(RMPP_USER)@$(RMPP_HOST):$(RMPP_DEST)/
	ssh $(RMPP_USER)@$(RMPP_HOST) "chmod +x $(RMPP_DEST)/ferrari_lab"
	@echo "Deployed Ferrari Lab to $(RMPP_USER)@$(RMPP_HOST):$(RMPP_DEST)"
	@echo "Open AppLoad on your reMarkable Paper Pro and tap 'Ferrari Lab' to launch."

clean:
	rm -f $(OUT)
