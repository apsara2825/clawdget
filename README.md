<div align="center">

# 🐾 clawdget

### **An ultra-efficient AI agent in pure C**

**550KB binary · <1MB RAM · instant boot · AI for every Linux device**

![size](https://img.shields.io/badge/binary-550%20KB~2MB-blue)
![ram](https://img.shields.io/badge/RAM-%3C1%20MB-success)
![Arch-x86__64%20%7C%20ARM%20%7C%20MIPS%20%7C%20RISC--V-blue](https://img.shields.io/badge/Arch-x86__64%2C%20ARM%2C%20MIPS%2C%20RISC--V-blue)
![license](https://img.shields.io/badge/license-MIT-green)

**Pure C · Tool calling · Streaming SSE · Sessions · Skills · WeChat (optional)**

English | [中文文档（新手教程）](README.zh-CN.md)

</div>

---

> **clawdget** is an independent open-source project written from scratch in
> **C99** — not a fork of PicoClaw, NanoBot, or anything else, but a fresh
> implementation inspired by them.
>
> 🦴 **clawdget** has exactly one goal: **cram an AI agent into every
> resource-constrained Linux device** — routers, dev boards, NAS boxes,
> set-top sticks, aging mini PCs … with just 1MB of free memory you get an AI
> assistant that runs commands, reads and writes files, and fetches web pages
> on its own.

## Why clawdget?

Most AI agent frameworks assume a PC: a Python runtime plus a pile of
dependencies, tens of MB before you start. clawdget goes the other way —
one C binary you drop into `/usr/bin`:

| | clawdget | typical Python agent |
|---|---|---|
| binary size | 550 KB (slim) – 2 MB (fully static) | 50 MB+ runtime |
| RAM in use | < 1 MB | 30 MB+ |
| startup | milliseconds | seconds |
| dependencies | none / your system's libcurl | Python + dozens of packages |
| backends | any OpenAI-compatible endpoint | vendor SDKs |

Works with **DeepSeek, Qwen, Moonshot, OpenAI, Ollama, vLLM and everything
OpenAI-compatible**.

## See it running right now

Captured live on a 580MHz MT7628 router with 59MB of RAM:

![clawdget demo on MT7628](docs/demo.gif)

```text
root@OpenWrt:~# clawdget -n "帮我检查一下路由器现在有没有异常"
[tool] sysinfo
[tool] exec  uptime
[tool] exec  free
[tool] exec  df -h
[tool] exec  dmesg | tail
[tool] exec  netstat -tlnp
[tool] exec  ps w

检查完毕，以下是路由器的整体健康报告：
  运行时间   ~76 小时（稳定运行中）
  系统负载   0.07 / 0.05 / 0.05（非常低）
  内存       58MB 总量，可用 33MB（46%）
  ...
**总结：路由器运行稳定，各项服务正常，没有发现异常。** 👍
```

*Real output.* The agent decided on its own which commands to run, executed
them, and wrote the report — no preset scripts. The same experience works on
any Linux device: x86 mini PCs, ARM boards, NAS, set-top boxes …

## Features

- **Agent loop** — LLM ↔ tools cooperate until the task is done (budget: 20 rounds by default)
- **7 built-in tools** — `exec` `read_file` `write_file` `edit_file` `list_dir` `http_fetch` `sysinfo`
- **Skills** — drop Markdown playbooks into `workspace/skills/<name>/SKILL.md`; the agent discovers and follows them
- **WeChat channel (optional)** — chat with the agent over a personal WeChat account via Tencent's official iLink API (`make WEIXIN=1`)
- **Telegram channel (optional)** — Bot API long-polling with native proxy support (`make TELEGRAM=1`)
- **Safety defaults** — `exec` asks y/n per command; file tools are jailed to a workspace; `-y` / `"auto": true` lifts restrictions at your own risk
- **Streaming** — SSE deltas print as they arrive, with a `thinking...` indicator while waiting
- **Sessions** — append-only JSONL per session (crash-safe `fsync`), `ls` / `resume` / `rm`
- **Context control** — history sliding window (`max_history`)
- **Robust I/O** — tool output UTF-8 sanitized, capped, and timeout-killed

## Quick start

### 1. Get the binary

Grab one from [Releases](https://github.com/apsara2825/clawdget/releases)
(`clawdget-linux-mipsel-slim` fits MIPS routers) and copy it over:

```sh
scp clawdget-linux-mipsel-slim root@192.168.x.1:/data/clawdget
ssh root@192.168.x.1 "chmod +x /data/clawdget"
```

### 2. Configure

Run `clawdget` once — it writes a config template
(`~/.clawdget/config.json`, or `/data/.clawdget/config.json` on devices with
a `/data` partition) — then fill in your API endpoint and key. Any
OpenAI-compatible platform works:

```json
{
  "model_list": [
    {
      "api_base": "https://api.deepseek.com/v1",
      "api_key": "sk-your-key",
      "model": "deepseek-chat"
    }
  ]
}
```

### 3. Chat

```sh
clawdget                     # interactive REPL (/new /ls /resume /quit)
clawdget "any question"      # one-shot question
clawdget -y                  # auto mode: run tools without confirmation
clawdget -r                  # resume a past session (shows last messages)
```

Paths like `/etc/config/network` typed in the REPL are sent to the agent as
normal text — only real commands (`/new` `/ls` `/resume` `/rm` `/help` `/quit`)
are interpreted.

## Usage

| command | what it does |
|---|---|
| `clawdget` | interactive REPL |
| `clawdget "question"` | one-shot question |
| `clawdget -y` | auto mode: tools run without confirmation |
| `clawdget -r` | resume a session (shows the last 3 messages) |
| `clawdget ls` / `rm <key>` | session management |

**Safety**: by default every shell command needs y/n and file tools stay
inside the workspace. `-y` or `"auto": true` removes the guardrails — your
call.

## Config reference

```jsonc
{
  "model_list": [                    // models; the first one is used
    {
      "api_base": "https://api.deepseek.com/v1",  // OpenAI-compatible endpoint
      "api_key": "sk-xxx",
      "model": "deepseek-chat",
      "temperature": 0.7,            // optional
      "max_tokens": 4096             // optional
    }
  ],
  "agents": {
    "defaults": {
      "max_tool_iterations": 20,     // tool rounds per task
      "max_history": 60              // history sliding window (token control)
    }
  },
  "tools": {
    "auto": false,                   // true = no confirmations
    "exec_confirm": true,            // y/n before exec
    "show_tool_calls": false,        // print [tool] lines
    "allow_paths": []                // extra paths file tools may touch
  }
}
```

Env overrides: `CLAWDGET_API_BASE` `CLAWDGET_API_KEY` `CLAWDGET_MODEL`
`CLAWDGET_HOME` `CLAWDGET_CONFIG` `CLAWDGET_CAINFO` `CLAWDGET_WX_TOKEN`.

## Skills

Teach your agent new tricks with plain Markdown. Each skill is a playbook at
`{workspace}/skills/<name>/SKILL.md`; its description is injected into the
system prompt, and when a task matches the agent `read_file`s the playbook
and follows it — no execution engine, no sandbox, just instructions.

```markdown
---
description: Health-check the router and write a report
---
# Router check

1. Run the sysinfo tool
2. Run `uptime` and `free` via exec
3. Write the summary to workspace/report.md with write_file
4. Tell the user where the report is
```

Install from a URL: `clawdget skill add https://example.com/skill.md`

## Telegram channel (optional build)

```sh
make mips TELEGRAM=1 CROSS=<toolchain-prefix> ...
```

Config:

```json
"channels": {
  "telegram": {
    "token": "bot token from BotFather",
    "proxy": "socks5://127.0.0.1:1080",
    "allow_from": [12345678, "@yourname"]
  }
}
```

Long-polling based; `proxy` accepts anything libcurl understands (socks5/http).
`allow_from` takes chat ids or @usernames (get yours from `@userinfobot`).
Replies longer than 4000 chars are split automatically; media messages are
forwarded as placeholders. Plain text replies (MarkdownV2 not yet supported).
`clawdget gateway` starts every enabled channel.

## WeChat channel (optional build)

Chat with the agent over a personal WeChat account through Tencent's official
iLink API (not a reverse-engineered protocol):

```sh
# enable at compile time (without WEIXIN=1 the binary contains none of it)
make mips WEIXIN=1 CROSS=<toolchain-prefix> ...

clawdget auth weixin    # QR login (renders right in the terminal)
clawdget gateway        # daemon: the agent answers WeChat messages
```

Per-user sessions (`wx-*`), an allowlist, and persisted state are included.
Note: tokens bind to one device, and high-frequency auto-replies can trip
WeChat's anti-spam — use the allowlist.

## Build

### Local build & development

```sh
make debug && make test && make e2e   # x86 build + unit tests + mock e2e
```

### Cross compiling

**1. build-static.sh (recommended)** — downloads and statically builds
mbedtls + curl for the target and links a fully static clawdget. You only
need a cross toolchain (e.g. [musl.cc](https://musl.cc)):

```sh
CROSS=mipsel-linux-musl- ./build-static.sh    # -> build-static/clawdget
CROSS=arm-linux-musleabi- ./build-static.sh build-arm
```

**2. make with your target libcurl** — point CURL_INC/CURL_LIB at a libcurl
built for the target:

```sh
make mips CROSS=mipsel-openwrt-linux- \
     CURL_INC=/path/target-curl/include CURL_LIB=/path/target-curl/lib
# if the target libcurl is a static archive:
make mips CROSS=... CURL_INC=... CURL_A=/path/libcurl.a MIPS_LIBS="-lssl -lcrypto"
```

Verified on MT7628 (mipsel, uClibc 0.9.33.2) with the
[OpenWrt toolchain](https://openwrt.org/docs/developer-toolchain/start).

## Tools

| tool | description |
|---|---|
| `exec` | run a shell command (`/bin/sh -c`), timeout-kill, output capped |
| `read_file` / `write_file` / `edit_file` / `list_dir` | workspace-jailed file operations |
| `http_fetch` | GET a URL, response capped |
| `sysinfo` | host, load, uptime, memory, filesystems, NIC counters |

## Architecture

```
src/
├── main.c       CLI / REPL / subcommands
├── config.c     JSON config + env overrides
├── provider.c   OpenAI-compatible client (SSE streaming, tool-call delta assembly)
├── agent.c      tool-calling loop
├── tools.c      registry + JSON schemas
├── t_shell.c    exec tool
├── t_fs.c       file tools
├── t_httpfetch.c  http_fetch + sysinfo
├── session.c    JSONL multi-session store
├── prompt.c     system prompt
├── spinner.c    "thinking..." indicator
├── gateway.c    multi-channel gateway (one thread per channel)
├── http.c       libcurl wrapper (SSE stream / buffered POST / GET)
└── util.c       helpers
thirdparty/cjson.c       minimal cJSON-compatible JSON library
thirdparty/qrcodegen.c   QR code generation (WeChat channel)
src/weixin/              WeChat channel (compiled with WEIXIN=1)
src/telegram/            Telegram channel (compiled with TELEGRAM=1)
```

## Roadmap

- [ ] More prebuilt platforms (aarch64, riscv)
- [ ] Hardware tools (GPIO / i2c)
- [ ] Scheduled tasks
- [ ] Summarization-based context compression
- [ ] More channels

Ideas and PRs welcome.

## License

MIT — see [LICENSE](LICENSE). © 2026 yang ([@apsara2825](https://github.com/apsara2825))
