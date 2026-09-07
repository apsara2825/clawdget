<div align="center">

# 🐾 clawdget 中文文档

### **能塞进路由器里的 AI Agent**

让一台从没想过能跑 AI 的设备，跑上 AI Agent。

![size](https://img.shields.io/badge/binary-550%20KB-blue)
![ram](https://img.shields.io/badge/RAM-%3C1%20MB-success)
![arch](https://img.shields.io/badge/arch-MIPS%20%7C%20ARM%20%7C%20x86-informational)
![license](https://img.shields.io/badge/license-MIT-green)

[English](README.md) | 中文（新手教程）

</div>

---

## 这是个啥？

一句话：**给老路由器、开发板装一个 AI 助手**。

你平时用的 AI（ChatGPT、DeepSeek 这些）跑在云上，但它们没法直接操作你的设备。
clawdget 装在你的路由器上之后，它就是一个"住在设备里的 AI"——你说"帮我看看路由器
有没有问题"，它会**自己决定**执行哪些命令（看内存、看日志、查网络……），把结果
看完再总结给你。

整个过程只需要一个几百 KB 的小程序，内存占用不到 1MB，十年前的路由器都能跑。

![演示动图](docs/demo.gif)

## 五分钟上手（不用编译，下载就能用）

### 第 1 步：下载程序

到 [Releases](https://github.com/apsara2825/clawdget/releases) 页面下载
对应你设备的文件：

| 文件 | 适合什么设备 | 说明 |
|---|---|---|
| `clawdget-linux-mipsel-slim` | MT7628/MT7688 等 MIPS 路由器 | 精简版 ~550KB，要求固件自带 openssl（多数 OpenWrt 都有） |
| x86 / ARM 设备 | 电脑、开发板等 | 请按下方「自己编译」自行构建 |

传到设备上并赋予执行权限（在你的电脑上执行，`192.168.x.1` 换成路由器 IP）：

```sh
scp clawdget-linux-mipsel root@192.168.x.1:/data/clawdget
ssh root@192.168.x.1 "chmod +x /data/clawdget; ln -sf /data/clawdget /usr/bin/clawdget"
```

> 💡 没有 `/data` 分区的设备，把程序放 `/usr/bin/` 目录也可以。

### 第 2 步：填配置

在路由器上随便跑一次 `clawdget`，它会自动生成配置模板，然后编辑它：

```sh
vi /data/.clawdget/config.json    # 没有 vi 就用 cat > 重写整个文件
```

需要修改的只有三行（**去哪里拿 key？**：DeepSeek、Moonshot、阿里百炼、
火山方舟等任何 OpenAI 兼容的平台注册后都能拿到，几块钱能用很久）：

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
clawdget                        # 进入对话模式
clawdget -n "路由器内存多少"      # 单次提问后退出
```

完事！下面是更详细的功能和进阶玩法。

## 日常使用

| 命令 | 作用 |
|---|---|
| `clawdget` | 进入对话模式，连续聊天 |
| `clawdget "任意问题"` | 问一句就退出 |
| `clawdget -y` | 自动模式：AI 执行命令不再需要你按 y 确认 |
| `clawdget -r` | 恢复旧对话：列出历史会话，输序号继续聊 |
| `clawdget ls` | 列出所有会话 |
| `clawdget rm 会话名` | 删除某个会话 |

**对话模式里的快捷命令**：`/new` 新会话 · `/ls` 列会话 · `/resume` 恢复会话 ·
`/quit` 退出。其他以 `/` 开头的内容（比如文件路径 `/etc/config/network`）
都会当作正常对话发给 AI。

**安全说明**：默认情况下 AI 每次要执行命令都会先打印出来问你 y/n；它写文件
只能在 workspace 目录里。确认自己环境安全后可以用 `-y` 或配置里 `"auto": true`
放开限制。

## 自己编译（进阶）

### 方式一：build-static.sh 脚本（推荐，最省心）

只要有一个交叉编译工具链，脚本会自动下载并静态编译 mbedtls 和 curl，
最后产出一个**不依赖任何系统库**的单文件程序：

```sh
# 1. 下载一个交叉工具链（以 musl.cc 为例，也可以用 OpenWrt SDK 的工具链）
wget https://musl.cc/mipsel-linux-musl-cross.tgz
tar xzf mipsel-linux-musl-cross.tgz

# 2. 一键构建（CROSS 填工具链前缀）
CROSS=$PWD/mipsel-linux-musl-cross/bin/mipsel-linux-musl- \
    ./build-static.sh

# 3. 产物在 build-static/clawdget，全静态，拷到设备就能跑
```

换个平台就换个工具链前缀，比如 ARM：`CROSS=arm-linux-musleabi-`。

### 方式二：make + 目标平台的 libcurl

如果你的工具链环境里已经有编译好的目标平台 libcurl：

```sh
make mips CROSS=mipsel-openwrt-linux- \
     CURL_INC=/目标libcurl的include路径 \
     CURL_LIB=/目标libcurl的lib路径
```

个人的路径偏好可以写进 `local.mk`（已被 git 忽略），不用每次敲。

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
      "max_history": 60              // 对话历史滑窗条数（控制 token 消耗）
    }
  },
  "tools": {
    "auto": false,                   // true = AI 执行命令不再询问
    "exec_confirm": true,            // exec 前是否 y/n 确认
    "show_tool_calls": false,        // 是否在终端显示 [tool] 执行行
    "allow_paths": []                // 文件工具允许访问 workspace 之外的路径
  }
}
```

也可以用环境变量临时覆盖：`CLAWDGET_API_BASE`、`CLAWDGET_API_KEY`、
`CLAWDGET_MODEL`、`CLAWDGET_HOME`、`CLAWDGET_CONFIG`。

## Skills：教 AI 新技能（不用写代码）

一个 skill 就是一份 Markdown 操作手册，放进
`{workspace}/skills/名字/SKILL.md` 即可。AI 每次启动都会看到技能清单，
遇到匹配的任务会自己去读手册并照做。

举例 —— 教它每天体检路由器：创建
`/data/.clawdget/workspace/skills/check/SKILL.md`：

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

然后对它说"做个体检"就行了。也可以从网上安装别人写好的：

```sh
clawdget skill add https://example.com/some-skill.md
```

## 常见问题

**Q：HTTPS 报证书错误？**
设备上缺少 CA 证书目录。OpenWrt 可装 `ca-certificates`（`opkg install
ca-certificates`），或确认 `/etc/ssl/certs/` 目录存在。

**Q：多大的设备能跑？**
二进制 550KB，运行内存 <1MB。实测 MT7628（580MHz、59MB 内存）流畅运行。
理论上任何能跑 Linux 且有 1MB 空闲内存的设备都行。

**Q：会不会把路由器搞坏？**
默认模式下 AI 执行每条命令都要你确认，写文件限制在 workspace 目录。
只要不开 `-y` 自动模式，风险可控。

**Q：对话记录存在哪？会不会写坏 flash？**
`{home}/sessions/` 下的 JSONL 文件。放在 /data 等持久分区会写 flash，
高频使用建议把 `CLAWDGET_HOME` 指到内存盘（如 /tmp），代价是重启丢失。

**Q：支持哪些模型？**
任何 OpenAI 兼容接口：DeepSeek、Qwen、Moonshot、OpenAI、Ollama、vLLM、
各类中转站均可。

## 参与贡献

欢迎提 issue 和 PR。开发调试：

```sh
make debug && make test && make e2e   # 本机构建 + 单测 + mock 全链路
```

## 协议

MIT — 详见 [LICENSE](LICENSE)。© 2026 yang ([@apsara2825](https://github.com/apsara2825))
