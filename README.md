# mounthide

把 KernelSU 模块挂载从 `/proc` 的 mountinfo/mounts/mountstat 输出里隐藏。
纯 LKM，不改内核源码。

## 原理

kprobe 挂 `show_mountinfo` / `show_vfsmnt` / `show_vfsstat` 三个回调，
挂载根路径命中 `/adb/modules`（默认）就跳过输出。对所有进程生效，
不影响挂载本身的功能。

### 顺带吐槽：Android 隔离进程是个笑话

`isolatedProcess` 号称隔离，实际只隔离了 UID 和 SELinux 域。挂载命名
空间没隔离——隔离进程照样跑在全局 mount ns 里，`/proc` 全量可见。
反而正常 app 被塞进沙箱挂载 ns（`/proc` hidepid 只能看见自己），
隔离进程却拥有全世界视野。PrivIsolated 这类检测器就是拿这个"隔离
不是隔离"的设计漏洞开挂：无权限、零成本看全设备挂载。

所以只要挂载存在，任何进程都能看到——安心藏，只能藏在内核输出层。

## 构建

CI 自动构建（`.github/workflows/build.yml`，DDK 容器，7 个 KMI，
产物在 Actions artifacts 里）。

本地：

```bash
podman run --rm -v "$PWD:/mnt/src" -w /mnt/src \
  ghcr.io/ylarod/ddk-min:android15-6.6-20260313 sh -c '
    unset ARCH CROSS_COMPILE; export ARCH=arm64
    make -C /opt/ddk/kdir/android15-6.6 M=/mnt/src modules'
```

需要目标内核开启：`CONFIG_KPROBES`、`CONFIG_KALLSYMS_ALL`、
`CONFIG_CFI_CLANG`。

## 使用

- KernelSU 模块：`mounthide-module.zip` 用 Manager 安装，开机自动加载
- 手动：`insmod mounthide.ko`（可加 `hide_prefix=/path1;/path2`，默认 `/adb/modules`）
- 卸载：`rmmod mounthide`

## 实测

6.6.118 GKI + PrivIsolated v1.1：**OK: Not found**（mountinfo 192→189 行，只藏 3 个 KSU 挂载，无误伤）

## License

GPL-2.0
