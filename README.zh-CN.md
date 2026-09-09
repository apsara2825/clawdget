<div align="center">

# 🐾 clawdget

### **C 语言超轻量级 AI Agent**

**550KB 二进制 · <1MB 内存 · 毫秒启动 · 让任何 Linux 设备拥有 AI**

![size](https://img.shields.io/badge/binary-550%20KB~2MB-blue)
![ram](https://img.shields.io/badge/RAM-%3C1%20MB-success)
![Arch-x86__64%20%7C%20ARM%20%7C%20MIPS%20%7C%20RISC--V-blue](https://img.shields.io/badge/Arch-x86__64%2C%20ARM%2C%20MIPS%2C%20RISC--V-blue)
![license](https://img.shields.io/badge/license-MIT-green)

**纯 C · 工具调用 · 流式输出 · 多会话 · Skills 技能系统 · 微信接入（可选）**

[English](README.md) | 中文（新手教程）

</div>

---

> **clawdget** 是一个独立开源项目，使用 **C99** 从零编写——不是 PicoClaw、NanoBot 或任何项目的分支，而是受它们启发的全新实现。
>
> 🦴 **clawdget** 的目标只有一个：**把 AI Agent 塞进一切资源受限的 Linux 设备**——路由器、开发板、NAS、机顶盒、老旧小主机……只要 1MB 空闲内存，就能拥有一个会自己执行命令、读写文件、查网页的 AI 助手。

## 为什么是 clawdget？

绝大多数 AI Agent 框架默认跑在 PC 上：Python 运行时 + 一堆依赖，几十 MB 起步。
clawdget 反其道而行之——一个 C 语言二进制，扔进 `/usr/bin` 就能用：

| | clawdget | 典型 Python Agent |
|---|---|---|
| 二进制体积 | 550KB（精简）~ 2MB（全静态） | 50MB+ 运行时 |
| 运行内存 | <1MB | 30MB+ |
| 启动速度 | 毫秒级 | 秒级 |
| 依赖 | 零 / 系统自带 libcurl | Python + 几十个包 |
| 接入方式 | 任意 OpenAI 兼容接口 | 各家 SDK |

支持 **DeepSeek、Qwen、Moonshot、OpenAI、Ollama、vLLM 及一切 OpenAI 兼容接口**。

## 实测演示

在 580MHz 的 MT7628 路由器（59MB 内存）上实录：

![演示动图](docs/demo.gif)

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

AI 自己决定执行哪些命令、自己总结——没有预设脚本。类似的体验可以跑在任何
Linux 设备上：x86 小主机、ARM 开发板、NAS、机顶盒……

## 核心特性

- **Agent 工具循环** — LLM ↔ 工具自动协作完成任务（默认 20 轮上限，可配置）
- **7 个内置工具** — `exec` `read_file` `write_file` `edit_file` `list_dir` `http_fetch` `sysinfo`
- **Skills 技能系统** — 往 `workspace/skills/` 里扔一份 Markdown 手册，AI 自动发现并照做
- **微信接入（可选编译）** — 基于腾讯官方 iLink API，个人微信号变成 AI 助手
- **Telegram 渠道（可选编译）** — Bot API 长轮询，原生支持代理
- **安全默认** — exec 逐条 y/n 确认；文件工具锁定 workspace；`-y` 解锁全自动
- **流式输出** — SSE 边收边显示，等待时有 `thinking...` 动态指示
- **多会话** — JSONL 持久化（掉电安全），`ls`/`resume`/`rm` 管理
- **上下文滑窗** — `max_history` 控制历史长度，费用可控
- **健壮 I/O** — 工具输出 UTF-8 清洗、截断、超时击杀

## 五分钟上手

### 第 1 步：拿到程序

到 [Releases](https://github.com/apsara2825/clawdget/releases) 下载对应平台的文件
（`clawdget-linux-mipsel-slim` 适合 MIPS 路由器），传到设备：

```sh
scp clawdget-linux-mipsel-slim root@192.168.x.1:/data/clawdget
ssh root@192.168.x.1 "chmod +x /data/clawdget"
```

### 第 2 步：填配置

在设备上运行一次 `clawdget`，自动生成配置模板，填入 API 地址和 key
（DeepSeek、Moonshot、阿里百炼、火山方舟等任意 OpenAI 兼容平台均可）：

```json
{
  "model_list": [
    {
      "api_base": "https://api.deepseek.com/v1",
      "api_key": "sk-你的key",
      "model": "deepseek-chat"
    }
  ]
}
```

### 第 3 步：开聊

```sh
clawdget                        # 对话模式（/new /ls /resume /quit）
clawdget "任意问题"              # 单次提问
clawdget -y                     # 自动模式：AI 执行命令不再询问
clawdget -r                     # 恢复旧对话：列表选序号继续聊
```

详细用法、配置逐项说明、Skills 教程、常见问题见本页下方各章节。

## 日常使用

| 命令 | 作用 |
|---|---|
| `clawdget` | 对话模式，连续聊天 |
| `clawdget "任意问题"` | 单次提问 |
| `clawdget -y` | 自动模式：AI 执行命令不再 y/n 确认 |
| `clawdget -r` | 恢复旧对话（显示最近 3 条消息帮助回忆） |
| `clawdget ls` / `rm 会话名` | 会话管理 |

**对话模式快捷命令**：`/new` · `/ls` · `/resume` · `/quit`。其他以 `/` 开头的
内容（比如路径 `/etc/config/network`）都当作正常对话发给 AI。

**安全说明**：默认 AI 执行命令前逐条 y/n 确认、写文件限制在 workspace 内。
确认环境安全后可用 `-y` 或 `"auto": true` 放开。

## 配置文件详解

```jsonc
{
  "model_list": [                    // 模型列表，目前用第一个
    {
      "api_base": "https://api.deepseek.com/v1",  // 接口地址（OpenAI 兼容）
      "api_key": "sk-xxx",           // 你的 key
      "model": "deepseek-chat",      // 模型名
      "temperature": 0.7,            // 可选：随机度
      "max_tokens": 4096             // 可选：单次回复上限
    }
  ],
  "agents": {
    "defaults": {
      "max_tool_iterations": 20,     // AI 单次任务最多执行几轮工具
      "max_history": 60              // 历史滑窗条数（控制 token 消耗）
    }
  },
  "tools": {
    "auto": false,                   // true = AI 执行命令不再询问
    "exec_confirm": true,            // exec 前是否 y/n 确认
    "show_tool_calls": false,        // 是否显示 [tool] 执行行
    "allow_paths": []                // 文件工具允许访问的额外路径
  }
}
```

环境变量覆盖：`CLAWDGET_API_BASE`、`CLAWDGET_API_KEY`、`CLAWDGET_MODEL`、
`CLAWDGET_HOME`、`CLAWDGET_CONFIG`、`CLAWDGET_CAINFO`、`CLAWDGET_WX_TOKEN`。

## Skills：教 AI 新技能（不用写代码）

一个 skill 就是一份 Markdown 操作手册，放进 `{workspace}/skills/名字/SKILL.md`
即可。AI 每次启动都会看到技能清单，遇到匹配的任务会自己读手册并照做。

```markdown
---
description: 对路由器做健康检查并生成报告
---
# 路由器体检

步骤：
1. 运行 sysinfo 工具
2. 用 exec 执行 uptime 和 free
3. 用 write_file 把总结写到 workspace/report.md
4. 告诉用户报告位置
```

也可以安装别人写好的：`clawdget skill add https://example.com/skill.md`

## Telegram 渠道（可选编译）

```sh
# 编译时启用
make mips TELEGRAM=1 CROSS=工具链前缀 ...
```

配置文件：

```json
"channels": {
  "telegram": {
    "token": "BotFather 给的 bot token",
    "proxy": "socks5://127.0.0.1:1080",
    "base_url": "https://api.telegram.org",
    "allow_from": [12345678, "@你的用户名"]
  }
}
```

- `proxy` 支持任意 libcurl 代理写法（`socks5://`、`http://`），国内网络必备
- `base_url` 可换成自建/镜像 API 地址
- `allow_from` 支持 chat id 或 `@用户名`；为空则任何私聊都响应
- 常驻运行：`clawdget gateway`（自动启动所有已启用渠道，微信/Telegram 可同时开）

## 微信渠道（可选编译）

让 AI 通过个人微信号收发消息（基于腾讯官方 iLink API，非逆向协议）：

```sh
# 编译时启用（不加 WEIXIN=1 则二进制完全不含此功能）
make mips WEIXIN=1 CROSS=工具链前缀 ...

clawdget auth weixin    # 扫码登录（终端直接显示二维码）
clawdget gateway        # 常驻网关：AI 开始接收并回复微信消息
```

- 每个微信用户对应独立会话（`wx-xxx`），支持 `allow_from` 白名单
- token 与设备绑定，别处扫码会踢掉当前会话
- ⚠️ 高频自动回复可能触发微信风控，请合理使用

## 自己编译

### 方式一：build-static.sh（推荐）

只需一个交叉工具链，脚本自动下载并静态编译 mbedtls + curl，产出**零依赖单文件**：

```sh
# 下载工具链（以 musl.cc 为例）
wget https://musl.cc/mipsel-linux-musl-cross.tgz && tar xzf mipsel-linux-musl-cross.tgz

# 一键构建
CROSS=$PWD/mipsel-linux-musl-cross/bin/mipsel-linux-musl- ./build-static.sh
# 产物：build-static/clawdget（换平台就换工具链前缀，如 arm-linux-musleabi-）
```

### 方式二：make + 目标平台 libcurl

```sh
make mips CROSS=mipsel-openwrt-linux- \
     CURL_INC=/目标libcurl的include路径 CURL_LIB=/目标libcurl的lib路径
```

个人的路径偏好写进 `local.mk`（已被 git 忽略）。

### 本机开发调试

```sh
make debug && make test && make e2e   # x86 构建 + 单测 + mock 全链路
```

## 工具一览

| 工具 | 功能 |
|---|---|
| `exec` | 执行 shell 命令（超时击杀、输出截断） |
| `read_file` / `write_file` / `edit_file` / `list_dir` | 文件操作（锁定 workspace） |
| `http_fetch` | 抓取网页/API 内容 |
| `sysinfo` | 主机名、负载、内存、磁盘、网卡状态 |

## 架构

```
src/
├── main.c       CLI / REPL / 子命令
├── config.c     JSON 配置 + 环境变量覆盖
├── provider.c   OpenAI 兼容客户端（SSE 流式、工具调用增量拼装）
├── agent.c      工具循环
├── tools.c      工具注册表 + JSON Schema
├── t_shell.c    exec 工具
├── t_fs.c       文件工具
├── t_httpfetch.c  http_fetch + sysinfo
├── session.c    JSONL 多会话存储
├── prompt.c     system prompt
├── spinner.c    thinking 指示器
├── gateway.c    多渠道网关（每渠道一个线程）
├── http.c       libcurl 封装（SSE 流式 / 缓冲 POST / GET）
└── util.c       工具函数
thirdparty/cjson.c       cJSON 兼容最小实现
thirdparty/qrcodegen.c   二维码生成（微信渠道用）
src/weixin/              微信渠道（WEIXIN=1 时编译）
src/telegram/            Telegram 渠道（TELEGRAM=1 时编译）
```

## 路线图

- [ ] 更多平台的预编译产物（aarch64、riscv）
- [ ] 硬件工具（GPIO / i2c）
- [ ] 定时任务
- [ ] 基于总结的上下文压缩
- [ ] 更多渠道接入

欢迎提 issue 和 PR。

## 协议

MIT — 详见 [LICENSE](LICENSE)。© 2026 yang ([@apsara2825](https://github.com/apsara2825))
