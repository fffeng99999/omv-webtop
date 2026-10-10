# openmediavault-webtop

OpenMediaVault 8.x 原生插件：把 **Webtop**（linuxserver 出品、浏览器可访问的完整 Linux 桌面）集成进 OMV Web 界面。

- 插件只做 **OMV 侧集成**（配置、注册栈、生命周期、状态展示），**不改动 Webtop 自身功能**；桌面及其应用一律在其原生 Web 界面管理。
- 采用 **Compose 轨**：Docker Compose 栈由 **openmediavault-compose 插件**渲染与管理，本插件通过 Compose RPC（`Compose.setFile`）把栈注册进去，**不自己写栈文件**。
- 版本号体系：`8.x.y`（大版本跟随 OMV 大版本）。

## 依赖

- `openmediavault (= 8.*)`
- `openmediavault-compose (>= 8.2.0)`：栈的生命周期通过 Compose 插件 helper `omv-compose-run` 执行，该脚本自 Compose 8.2.0 起提供。**使用前必须先在 `服务 → Compose → 设置` 中选好 Compose 共享文件夹。**
- 镜像为 `lscr.io/linuxserver/webtop`，部署主机必须能访问 **`lscr.io`**（linuxserver 的镜像仓库），否则拉取镜像失败。

## 工作原理

- 配置存于 `conf.service.webtop`（datamodel + confdb create 脚本 + RPC + 模块）。
- 保存设置时 RPC `set()` 调用 `Compose.setFile` 把栈发布到 Compose 插件：
  栈文件落在 `<Compose 共享文件夹>/webtop/webtop.yml` 与 `webtop.env`（含 `compose.yml`/`.env` 软链），
  因此在 `服务 → Compose → 文件 / 服务` 页也能看到、编辑、启停该栈。
- 配置/数据共享文件夹通过 Compose 占位符 `${{ sf:"<共享文件夹名>" }}` 引用，共享文件夹挪位置由 Compose 在部署时解析。
  `sourceRef`（数据文件夹）为空时，栈里**不生成** `:/nas` 挂载。
- 「应用」时 Salt state（`srv/salt/omv/deploy/webtop/`）只做生命周期：
  `enable` 分支 `omv-compose-run webtop up -d`，关闭分支 `down`（**绝不带 -v、不删数据**）；
  配置共享文件夹或 Compose 插件未配置时整段跳过，绝不导致 Apply 失败。
- 部署/启停/升级/日志走 RPC 后台任务（`omv-webtop-ctl` 经 `omv-compose-run` 执行 `docker compose`），任务弹窗实时显示输出。

## 配置字段

| 字段 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enable` | boolean | `false` | 总开关。关闭时「应用」会移除容器（配置/数据文件夹保留）。 |
| `version` | string | `latest` | 镜像 `lscr.io/linuxserver/webtop:<version>` 的标签。 |
| `httpPort` | integer | `3000` | 纯 HTTP 桌面界面的宿主端口（容器内固定 3000）。 |
| `httpsPort` | integer | `3001` | HTTPS 桌面界面的宿主端口（容器内固定 3001）。 |
| `timeZone` | string | `Etc/UTC` | 容器内时区。 |
| `configRef` | string | `""` | 共享文件夹（映射容器 `/config`，桌面家目录与全部设置）。`enable` 时必填。 |
| `sourceRef` | string | `""` | 可选共享文件夹（映射容器 `/nas`，供桌面访问文件）。 |
| `customUser` | string | `admin` | 网页登录用户名（CUSTOM_USER）。 |
| `password` | string | `""` | 网页登录密码。`enable` 时必填。 |
| `title` | string | `Webtop` | 页面标题（TITLE）。 |
| `puid` | integer | `1000` | 容器内运行的用户 ID。 |
| `pgid` | integer | `1000` | 容器内运行的用户组 ID。 |

> 配置（数据）目录未配置完成前，插件**不注册栈**（`syncComposeStack` 守卫），Salt state 也不会拉起服务，因此服务在配好数据目录前无法启动。

## 渲染出的栈

栈名 `webtop`，单个服务 `webtop`（`lscr.io/linuxserver/webtop`）。
环境变量由插件写入 `webtop.env`（`WEBTOP_VERSION`、`WEBTOP_HTTP_PORT`、`WEBTOP_HTTPS_PORT`、`TZ`、`WEBTOP_PUID`、`WEBTOP_PGID`、`WEBTOP_USER`、`WEBTOP_PASSWORD`、`WEBTOP_TITLE`），由栈内的 `${...}` 读取。
容器设置了 `shm_size: 1gb`（桌面应用需要），端口映射 3000/3001（HTTP/HTTPS）。

## 安装 / 卸载

```bash
# 安装（需先装好 openmediavault-compose 并在 Compose 设置里选好共享文件夹）
apt-get install ./openmediavault-webtop_8.0.0_all.deb

# 卸载（保留配置/数据共享文件夹）
apt-get remove openmediavault-webtop
# 彻底清除（删除配置节点）
apt-get purge openmediavault-webtop
```

- 安装时 **不启动服务**：装完后到 `服务 → Webtop → 设置` 选好配置共享文件夹、设置登录用户/密码，勾选总开关并「应用」才会拉起栈。
- 卸载时 `prerm` 调用 `omv-webtop-ctl unregister` 从 Compose 插件注销栈（移除容器与生成的栈文件；**配置/数据共享文件夹不动**）。

## 栈归 omv-compose 管理

栈文件由 openmediavault-compose 插件落盘并管理，本插件只生成内容（body + env）后经 `Compose.setFile` 提交；
请勿手工编辑 `<Compose 共享文件夹>/webtop/webtop.yml`（头部标注 auto-generated），要改栈内容请走 Compose 页或本插件的设置页。

## 安全提醒

- **HTTPS 才能用全功能**：剪贴板、音频、摄像头等需要安全上下文，请用 HTTPS 端口访问。
- **自签名证书**：容器提供自签名证书，浏览器会提示证书警告，属正常现象。
- **容器内有免密 sudo**：Webtop 容器内部拥有免密 sudo 权限，**切勿将其直接暴露到公网**；公网访问请走隧道（如 frpc）并只按需暴露。
- 请设置强登录密码，不要留空 `password` 字段。

## 状态与版本

设置页显示：服务状态（运行中/停止/未安装）、渲染出的栈文件位置、日志查看、渲染出的 compose 文件预览，
以及「打开 Webtop」（HTTPS）按钮。状态文案由 RPC 返回英文 msgid，前端经 `translate` 过滤器本地化。

## 发布流程

push tag `v*` → GitHub Actions（`.github/workflows/build.yml`）lint + 构建 `_all.deb` + 附到 Release；
`${GITHUB_USER}` 占位符由 `render-vars.sh` 按 `debian/variables.env` 渲染。
