# git clone 与 shell 笔记：代理变量、`-C`、重定向

> 横切文档：一次真实排错引出的 shell / git 基础。这些知识在 ROS 之外的场合同样常用，
> 值得单独记一份。

## 背景

一次 `git clone` 失败，排查后发现两种写法的命令**只有 4 处不同**（URL、目标路径
`src/examples`、`-b humble` 都一致）：

| 片段 | 写法 A（失败） | 写法 B（成功） | 是否影响结果 |
| --- | --- | --- | --- |
| 代理设置 | `set $env:all_proxy=...` | `export https_proxy=... http_proxy=...` | **A 语法错，实际没生效** |
| 工作目录 | 依赖终端当前目录 | `git -C /home/lxl/ros2_ws` | **A 第一次失败的直接原因** |
| 错误流合并 | 无 | `2>&1` | 只影响能否看到进度和报错 |
| 输出裁剪 | 无 | `\| tail -20` | 只影响输出长度 |

下面逐项说明。

## 1. `set $env:all_proxy=...` vs `export https_proxy=...`

`$env:all_proxy` 是 **PowerShell** 的环境变量语法，bash 不认识。实际展开过程：

- `$env` 在 bash 里是未定义变量 → 展开成空串
- 整条命令变成 `set :all_proxy=http://127.0.0.1:7890`
- bash 的 `set` 后面跟单词时是设置**位置参数**，于是 `$1` 变成了 `:all_proxy=http://127.0.0.1:7890`

可以紧跟一句 `echo "$1"` 验证。它没有设置任何环境变量，所以 git 也看不到代理——
**那次成功是直连通了，不是代理起作用**。

bash 里正确写法是 `export`：

- `export NAME=value` = 创建 shell 变量 + 标记为"导出"，之后启动的子进程（git、curl…）才能在自己的环境里看到它
- `export A=1 B=2` 只是 `export A=1; export B=2` 的简写
- 只想对**一条命令**生效时，可以写成前缀形式 `https_proxy=... git clone ...`，不污染当前 shell

## 2. `all_proxy` vs `https_proxy` + `http_proxy`

变量名本身不会导致失败（git 底层是 libcurl，`all_proxy`/`ALL_PROXY` 它认），但覆盖范围不同：

| 变量 | 生效范围 |
| --- | --- |
| `http_proxy` | 访问 `http://` 目标时 |
| `https_proxy` | 访问 `https://` 目标时 |
| `all_proxy` | 所有协议（libcurl 支持；wget、apt、npm 等不一定认） |

两个容易踩的点：

- **变量值的 scheme 指的是代理本身，不是目标流量。** `https_proxy=http://127.0.0.1:7890` 是对的：以 HTTPS 访问目标，但代理是个 HTTP 代理（Clash/v2ray 的 7890 混合端口就是这种）
- **小写优先。** libcurl 只认小写 `http_proxy`（大写 `HTTP_PROXY` 是留给 CGI 的，有请求头注入的历史问题）；`https_proxy` 大小写都认。统一写小写最省事

## 3. `git -C <path>`

含义是：**先切到 `<path>`，再执行后面的 git 操作**。等价于 `(cd <path> && git ...)`，
但只影响 git 自己，shell 的当前目录不动。它是 git 的顶层选项，必须写在子命令（`clone`）之前。

这是那次失败的直接原因——目标路径是相对路径，按当前工作目录（current working directory, cwd）解析：

```text
~/ros2_ws/src$ git clone <url> src/examples
          ↓ 相对路径按 cwd 拼接
~/ros2_ws/src/src/examples        # 多套了一层，于是留下了那个空 src/
```

在 `~/ros2_ws` 下执行，拼出来就是 `~/ros2_ws/src/examples`，正确。

> 用绝对路径 + `-C` 的好处：不依赖当前目录，写进脚本/自动化时更可靠
> （顺带：`cd` 在某些带权限确认的环境里会触发弹窗，`-C` 能绕开）。

## 4. `2>&1`

作用：把**标准错误（file descriptor 2, fd 2）**重定向到**标准输出（fd 1）当前指向的地方**。

为什么需要它：管道 `|` 只搬运 stdout，而 git 的进度信息（`remote:`、`Receiving objects:`）和
`fatal:` 报错**全都写在 stderr**。不合并的话，`tail` 收到的 stdout 基本是空的，
进度信息则绕过管道直接打到终端——截出来的"最后 20 行"恰好什么都看不到。

顺序有讲究，重定向**从左到右**依次生效：

| 写法 | 效果 |
| --- | --- |
| `cmd 2>&1 \| tail` | git 的 stdout + stderr 都进管道（本文用法） |
| `cmd \| tail 2>&1` | 只重定向 `tail` 自己的 stderr，git 的照样直连终端 |
| `cmd 2>&1 > file` | 陷阱：stderr 去了终端，stdout 去了文件 |
| `cmd > file 2>&1` | 两者都进文件（"全都要"的正确写法） |

## 5. `| tail -20`

纯粹是**裁剪输出**，对功能零影响。git clone 会打几十上百行进度，只关心末尾有没有报错和摘要时，
截最后 20 行能避免整屏进度刷屏。交互式终端里不需要这层。

但它有个**真实的副作用**：管道的退出码是 `tail` 的，不是 `git` 的。所以
`git ... | tail -20` 即使 clone 失败，`$?` 也可能是 0，写进脚本会误判成功。要拿真实状态：

```sh
set -o pipefail           # 管道中任一环失败，整体即失败
# 或事后查：
echo "${PIPESTATUS[0]}"   # git 的退出码
```

## 一句话总结

真正会**改变行为**的只有两处：`export`（否则代理根本没设上）和路径（否则克隆进错误目录）；
`2>&1 | tail -20` 纯粹是输出降噪，与功能无关。
