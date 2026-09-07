<div align="center">

# 🐾 clawdget

### **The AI agent that fits inside a router.**

Run an OpenAI-compatible AI agent on hardware that was never supposed to run AI.

![size](https://img.shields.io/badge/binary-550%20KB-blue)
![ram](https://img.shields.io/badge/RAM-%3C1%20MB-success)
![arch](https://img.shields.io/badge/arch-MIPS%20%7C%20ARM%20%7C%20x86-informational)
![license](https://img.shields.io/badge/license-MIT-green)

**Pure C · Tool calling · Streaming SSE · JSONL sessions · Zero runtime deps**

English | [中文文档（新手教程）](README.zh-CN.md)

</div>

---

## That's it running on a 580MHz MT7628 router, right now

![clawdget demo on MT7628](docs/demo.gif)

Same session as plain text:

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

*Real output, captured on an MT7628 (MIPS 24KEc, 580MHz, 59MB RAM) running OpenWrt.*
The agent decided on its own which commands to run, executed them on the router,
and wrote the report. No shell scripts involved.

## Why clawdget

Most AI agent runtimes assume a PC. clawdget targets the other end of the
spectrum — a single C binary you drop into `/usr/bin` of a router:

| | clawdget | typical Python agent |
|---|---|---|
| binary size | ~550 KB | 50 MB+ runtime |
| RAM in use | < 1 MB | 30 MB+ |
| startup | instant | seconds |
| dependencies | your firmware's own libssl/libcrypto | Python + dozens of packages |

Works with **any OpenAI-compatible endpoint**: DeepSeek, Qwen, Moonshot, OpenAI,
Ollama, vLLM, or any relay. Inspired by
[PicoClaw](https://github.com/sipeed/picoclaw) and
[NanoBot](https://github.com/HKUDS/nanobot); the core loop is an independent C
implementation, developed and verified on real MT7628 / OpenWrt hardware.

## Features

- **Agent loop** — LLM ↔ tools until the task is done (budget: 20 rounds by default)
- **7 built-in tools** — `exec` `read_file` `write_file` `edit_file` `list_dir` `http_fetch` `sysinfo`
- **Skills** — drop Markdown playbooks into `workspace/skills/<name>/SKILL.md`; the agent discovers and follows them
- **Safety defaults** — `exec` asks y/n per command; file tools are jailed to a workspace; `-y` / `"auto": true` lifts restrictions at your own risk
- **Streaming** — SSE deltas print as they arrive, with a `thinking...` indicator while waiting
- **Sessions** — append-only JSONL per session (crash-safe `fsync`), `ls` / `resume` / `rm`
- **Context control** — history sliding window (`max_history`)
- **Robust I/O** — tool output UTF-8 sanitized, capped, and timeout-killed
- **CA auto-detection** — uses `/etc/ssl/certs` (bundle or hashed dir), or set `ca_info`

## Quick start

First run writes a config template to `~/.clawdget/config.json`
(or `/data/.clawdget/config.json` on devices with a `/data` partition) —
fill in your API endpoint and key:

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

Then:

```sh
clawdget                     # interactive REPL (/new /ls /resume /quit)
clawdget "what is my load?"  # one-shot question
clawdget -y                  # auto mode: run tools without confirmation
clawdget ls                  # list sessions
clawdget -r                  # resume: pick a past session from a list
```

Paths like `/etc/config/network` typed in the REPL are sent to the agent as
normal text — only real commands (`/new` `/ls` `/resume` `/rm` `/help` `/quit`)
are interpreted.

## Skills

Teach your agent new tricks with plain Markdown. Each skill is a playbook the
agent discovers automatically and follows when a task matches:

```sh
clawdget skill add https://example.com/my-skill.md [name]  # install from URL
clawdget skill ls                                          # list installed
clawdget skill rm my-skill                                 # remove
```

A skill is just `{workspace}/skills/<name>/SKILL.md`. Its description line
(frontmatter `description:` or `# heading`) is injected into the system prompt
as a catalog; when a task matches, the agent `read_file`s the full playbook
and follows it. No execution engine, no sandbox — just instructions.

## Build

### Prebuilt binaries

Check the [Releases](https://github.com/apsara2825/clawdget/releases) page —
pushing a `v*` tag triggers CI, which builds and attaches binaries
(`linux-x86_64`, `linux-mipsel` fully static).

### Local build

```sh
# x86 build + tests (needs libcurl-dev)
make debug && make test && make e2e
```

### Cross compiling to your platform

Use **your own cross toolchain**. Two ways:

**1. build-static.sh (recommended)** — downloads and statically builds
mbedtls + curl for the target, links clawdget fully static. You only need a
cross toolchain (e.g. [musl.cc](https://musl.cc) toolchains):

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
├── main.c       CLI / REPL
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
├── http.c       libcurl wrapper (SSE stream / buffered POST / GET)
└── util.c       helpers
thirdparty/cjson.c  minimal cJSON-compatible JSON library (self-contained)
```

## Roadmap

- [ ] Hardware tools (GPIO / i2c) for embedded tinkering
- [ ] Cron / scheduled tasks
- [ ] Summarization-based context compression
- [ ] More release platforms (arm, aarch64)

Ideas and PRs welcome.

## License

MIT — see [LICENSE](LICENSE). © 2026 yang ([@apsara2825](https://github.com/apsara2825))
