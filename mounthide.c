// SPDX-License-Identifier: GPL-2.0
/*
 * mounthide.ko - 隐藏 KernelSU 模块挂载，使其不出现在
 *   /proc/self/mountinfo (show_mountinfo)
 *   /proc/self/mounts    (show_vfsmnt)
 *   /proc/self/mountstat (show_vfsstat)
 * 的输出中。
 *
 * 原理: 三个 proc 文件的后端都是 seq_file 写回调，本模块用 kprobe
 * 挂在 show_mountinfo/show_vfsmnt/show_vfsstat 入口，在回调执行前
 * 检查挂载根路径 (dentry_path_raw(mnt_root)) 是否命中特征前缀
 * (默认 /adb/modules)，命中则把指令流重定向到 trampoline 直接返回
 * 0 (seq_file 语义: 该条目不输出，继续下一条)。
 *
 * 过滤对所有进程生效，不影响挂载本身的功能。
 *
 * 用法:
 *   insmod mounthide.ko [hide_prefix=/adb/modules;...]
 * 卸载: rmmod mounthide
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/vfs.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/ptrace.h>

#define MOUNTHIDE_MAX_PREFIX 8
#define MOUNTHIDE_MAX_PREFIX_LEN 64
#define MOUNTHIDE_BUF_SIZE (PATH_MAX * 2)

static char hide_prefixes[MOUNTHIDE_MAX_PREFIX][MOUNTHIDE_MAX_PREFIX_LEN];
static int hide_prefix_count;
static char hide_prefix_param[MOUNTHIDE_MAX_PREFIX * MOUNTHIDE_MAX_PREFIX_LEN] = "/adb/modules";
module_param_string(hide_prefix, hide_prefix_param,
                    sizeof(hide_prefix_param), 0644);
MODULE_PARM_DESC(hide_prefix,
                 "Semicolon-separated mount root prefixes to hide (default /adb/modules)");

/* 解析 hide_prefix 参数到 hide_prefixes[] */
static void parse_hide_prefixes(void)
{
    char *s;
    char *tok;
    int i = 0;

    hide_prefix_count = 0;
    s = hide_prefix_param;
    /* module_param_string 的字符串就是本地副本，可直接分割 */
    while ((tok = strsep(&s, ";")) != NULL && i < MOUNTHIDE_MAX_PREFIX) {
        if (*tok == '\0')
            continue;
        strscpy(hide_prefixes[i], tok, MOUNTHIDE_MAX_PREFIX_LEN);
        i++;
    }
    hide_prefix_count = i;
    if (hide_prefix_count == 0) {
        strscpy(hide_prefixes[0], "/adb/modules", MOUNTHIDE_MAX_PREFIX_LEN);
        hide_prefix_count = 1;
    }
    pr_info("mounthide: hiding %d prefix(es)", hide_prefix_count);
    for (i = 0; i < hide_prefix_count; i++)
        pr_info("mounthide:   [%d] %s", i, hide_prefixes[i]);
}

/* 判断挂载 root 是否命中特征前缀 */
static bool mnt_root_matches(struct vfsmount *mnt)
{
    char *buf;
    char *root;
    bool matched = false;
    int i;

    if (!mnt->mnt_root)
        return false;

    buf = kmalloc(MOUNTHIDE_BUF_SIZE, GFP_ATOMIC);
    if (!buf)
        return false;

    /* dentry_path_raw(): 沿 dentry 链, 不跨挂载边界 -> mountinfo root 字段语义
     * (dentry_path 未导出, 用 raw 版本; 两者语义等价) */
    root = dentry_path_raw(mnt->mnt_root, buf, MOUNTHIDE_BUF_SIZE);
    if (IS_ERR(root)) {
        kfree(buf);
        return false;
    }

    /* dentry_path 输出可能是相对或绝对, 两种前缀都兼容 */
    for (i = 0; i < hide_prefix_count; i++) {
        const char *p = hide_prefixes[i];
        size_t len;
        if (*p == '/')
            p++; /* 跳过前导 '/' 再比, 兼容 root 无前导符 */
        len = strlen(p);
        if (strncmp(root, p, len) == 0)
            matched = true;
        if (root[0] == '/') {
            if (strncmp(root, hide_prefixes[i], strlen(hide_prefixes[i])) == 0)
                matched = true;
        }
    }
    if (matched)
    kfree(buf);
    return matched;
}

/* trampoline: 命中挂载时跳到此处直接返回 0 (seq 语义: 不输出当前条目) */
static int mounthide_skip_show(struct seq_file *m, struct vfsmount *mnt)
{
    return 0;
}

/* kprobe pre_handler: x0=seq_file*, x1=vfsmount* (arm64) */
static int mounthide_pre(struct kprobe *p, struct pt_regs *regs)
{
    struct vfsmount *mnt = (struct vfsmount *)regs->regs[1];

    if (mnt && mnt_root_matches(mnt)) {
        regs->pc = (unsigned long)mounthide_skip_show;
        return 1; /* 跳过单步，直接执行 trampoline */
    }
    return 0;
}

static struct kprobe kp_mountinfo, kp_vfsmnt, kp_vfsstat;

static int kp_setup(struct kprobe *kp, const char *name)
{
    kp->symbol_name = name;
    kp->pre_handler = mounthide_pre;
    return register_kprobe(kp);
}

static int __init mounthide_init(void)
{
    int ret = 0;
    int step = 0;

    parse_hide_prefixes();

    ret = kp_setup(&kp_mountinfo, "show_mountinfo");
    if (ret) {
        pr_err("mounthide: register show_mountinfo failed: %d\n", ret);
        return ret;
    }
    step = 1;
    ret = kp_setup(&kp_vfsmnt, "show_vfsmnt");
    if (ret) {
        pr_err("mounthide: register show_vfsmnt failed: %d\n", ret);
        goto err;
    }
    step = 2;
    ret = kp_setup(&kp_vfsstat, "show_vfsstat");
    if (ret) {
        pr_err("mounthide: register show_vfsstat failed: %d\n", ret);
        goto err;
    }

    pr_info("mounthide: active on %s/%s/%s\n",
            kp_mountinfo.symbol_name, kp_vfsmnt.symbol_name,
            kp_vfsstat.symbol_name);
    return 0;

err:
    if (step >= 1)
        unregister_kprobe(&kp_mountinfo);
    if (step >= 2)
        unregister_kprobe(&kp_vfsmnt);
    return ret;
}

static void __exit mounthide_exit(void)
{
    unregister_kprobe(&kp_mountinfo);
    unregister_kprobe(&kp_vfsmnt);
    unregister_kprobe(&kp_vfsstat);
    pr_info("mounthide: unregistered\n");
}

module_init(mounthide_init);
module_exit(mounthide_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("tees");
MODULE_DESCRIPTION("Hide KernelSU module mounts from /proc mounts output");
