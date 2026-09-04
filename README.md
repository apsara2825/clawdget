<div align="center">

# 🐾 clawdget

**A pocket-sized AI agent for embedded Linux devices — in pure C.**

* Ultra-light: ~550 KB binary (static libcurl) · runs in <1 MB RAM
* OpenAI-compatible API (DeepSeek / Moonshot / vLLM / Ollama / any relay)
* Streaming (SSE) output · tool calling (function calling) up to 20 rounds
* 7 built-in tools: exec · read_file · write_file · edit_file · list_dir · http_fetch · sysinfo
* Persistent multi-session (JSONL) · interactive REPL + one-shot CLI
* Zero runtime dependencies beyond your firmware's own libs

[中文](#中文说明) | English

</div>

---

> Inspired by [PicoClaw](https://github.com/sipeed/picoclaw) (Go) and
> [NanoBot](https://github.com/HKUDS/nanobot) — clawdget is an independent,
> from-scratch C rewrite of the core agent loop, trimmed for routers and
> single-board computers with 8–16 MB flash and 32–128 MB RAM.
> Developed and verified on an MT7628 (MIPS 24KEc, 580 MHz) OpenWrt router.

## Why clawdget

Most AI agent runtimes assume a PC. clawdget targets the other end of the
spectrum: a shell binary you can drop into `/usr/bin` of a router.

| | clawdget | typical Python agent |
|---|---|---|
| binary size | ~550 KB | 50 MB+ runtime |
| RAM in use | <1 MB | 30 MB+ |
| startup | instant | seconds |
| dependencies | firmware's own libssl/libcrypto | Python + dozens of packages |

## Features

- **Tool calling loop** with per-round budget (default 20, configurable) and
  optional history sliding window (`max_history`)
- **Safety defaults**: `exec` asks y/n on the terminal for every command;
  file tools are jailed to a workspace directory (`allow_paths` to extend);
  `-y` / `"auto": true` lifts restrictions at your own risk
- **Sessions**: append-only JSONL, one file per session, crash-safe `fsync`,
  `ls` / `resume` / `rm` management
- **Streaming**: SSE deltas are printed as they arrive
- **CA handling**: auto-detects `/etc/ssl/certs` (bundle file or hashed dir)

## Build

```sh
# local x86 build + tests
make debug && make test && make e2e

# cross-compile for MIPS (OpenWrt / uClibc)
# needs: cross gcc, libcurl headers/libs (curl >= 7.60, any TLS backend)
make mips CURL_INC=/path/to/curl/include CURL_LIB=/path/to/curl/lib
```

Fully static variant (nothing but libc from the firmware):

```sh
# libcurl.a built with --disable-shared, then:
make mips CURL_INC=... CURL_LIB=... CURL_A=/path/libcurl.a MIPS_LIBS="-lssl -lcrypto"
```

## Usage

```sh
clawdget                     # interactive REPL
clawdget "what is my load?"  # one-shot question
clawdget -y                  # auto mode: run tools without confirmation
clawdget ls                  # list sessions
clawdget -n                  # force a new session
```

First run writes a config template to `~/.clawdget/config.json` (or
`/data/.clawdget/config.json` on devices with a `/data` partition):

```json
{
  "model_list": [
    {
      "api_base": "https://api.deepseek.com/v1",
      "api_key": "sk-xxx",
      "model": "deepseek-chat",
      "temperature": 0.7,
      "max_tokens": 4096
    }
  ],
  "agents": { "defaults": { "max_tool_iterations": 20, "max_history": 60 } },
  "tools": { "auto": false, "exec_confirm": true, "show_tool_calls": false, "allow_paths": [] }
}
```

Environment overrides: `CLAWDGET_API_BASE`, `CLAWDGET_API_KEY`,
`CLAWDGET_MODEL`, `CLAWDGET_HOME`, `CLAWDGET_CONFIG`, `CLAWDGET_CAINFO`.

REPL commands: `/new` `/ls` `/resume <key>` `/rm <key>` `/help` `/quit`.

## Tools

| tool | description |
|---|---|
| `exec` | run a shell command (fork + `/bin/sh -c`), timeout-kill, output capped at 32 KB |
| `read_file` / `write_file` / `edit_file` / `list_dir` | workspace-jailed file operations |
| `http_fetch` | GET a URL, response capped at 64 KB |
| `sysinfo` | host, load, uptime, memory, filesystems, NIC counters |

## Architecture

```
src/
├── main.c       CLI / REPL
├── config.c     JSON config + env overrides (+ optional baked-in defaults)
├── provider.c   OpenAI-compatible chat client (SSE streaming, tool-call delta assembly)
├── agent.c      tool-calling loop
├── tools.c      registry + JSON schemas
├── t_shell.c    exec tool
├── t_fs.c       file tools
├── t_httpfetch.c  http_fetch + sysinfo
├── session.c    JSONL multi-session store
├── prompt.c     system prompt
├── http.c       libcurl wrapper (SSE stream / buffered POST / GET)
└── util.c       helpers
thirdparty/cjson.c  minimal cJSON-compatible JSON library (self-contained)
```

## 中文说明

**clawdget** 是一个面向嵌入式 Linux 设备（路由器、开发板）的纯 C 超轻量 AI Agent：

- 单二进制 ~550KB（libcurl 静态编入），运行内存 <1MB
- OpenAI 兼容接口（DeepSeek / Moonshot / vLLM / Ollama / 各类中转均可）
- 流式输出、工具调用（默认 20 轮上限、可配历史滑窗 `max_history`）
- 7 个内置工具，文件工具默认限制在 workspace 目录内，`exec` 默认逐条 y/n 确认
- JSONL 多会话持久化，REPL + 单次提问两种用法

灵感来自 [PicoClaw](https://github.com/sipeed/picoclaw) 与
[NanoBot](https://github.com/HKUDS/nanobot)，核心循环为独立 C 实现。
已在 MT7628 (MIPS 24KEc, OpenWrt, uClibc) 真机验证。

构建与用法见上文英文部分；交叉编译示例：

```sh
make mips CURL_INC=/path/curl/include CURL_LIB=/path/curl/lib
```

## License

MIT — see [LICENSE](LICENSE).
