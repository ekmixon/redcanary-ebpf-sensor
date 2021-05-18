// SPDX-License-Identifier: GPL-2.0+

#include <linux/kconfig.h>
#include <linux/ptrace.h>
#include <linux/version.h>
#include <linux/bpf.h>
#include <linux/uio.h>

#include "bpf_helpers.h"
#include "types.h"

#define MAX_TELEMETRY_STACK_ENTRIES 32

typedef struct
{
    void *iov_base; /* Starting address */
    size_t iov_len; /* Number of bytes to transfer */
} iovec_t, *piovec_t;

/*
***** MAPS
*/
struct bpf_map_def SEC("maps/wpm_events") write_process_memory_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 1024,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/cpm_events") change_process_memory_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 1024,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/tp_events") trace_process_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 1024,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/mount_events") mount_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 1024,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/cred_hash") cred_hash = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(u64),
    .value_size = sizeof(credentials_event_t),
    .max_entries = 256,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/cred_events") cred_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 1024,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/pam_hash") pam_hash = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(u64),
    .value_size = sizeof(pam_event_t),
    .max_entries = 256,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/pam_events") pam_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 1024,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/rrs_events") rrs_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 1024,
    .pinning = 0,
    .namespace = "",
};

/*
    Telemetry may have multiple events associated with an single
    syscall.
*/
struct bpf_map_def SEC("maps/telemetry_stack") telemetry_stack = {
    .type = BPF_MAP_TYPE_PERCPU_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(telemetry_event_t),
    .max_entries = MAX_TELEMETRY_STACK_ENTRIES,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/telemetry_index") telemetry_index = {
    .type = BPF_MAP_TYPE_PERCPU_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 1,
    .pinning = 0,
    .namespace = "",
};

struct bpf_map_def SEC("maps/telemetry_events") telemetry_events = {
    .type = BPF_MAP_TYPE_PERF_EVENT_ARRAY,
    .key_size = sizeof(u32),
    .value_size = sizeof(u32),
    .max_entries = 4096,
    .pinning = 0,
    .namespace = "",
};

static __always_inline syscall_pattern_type_t ptrace_syscall_pattern(u32 request)
{
    switch (request)
    {
    case PTRACE_POKETEXT:
        return SP_PTRACE_POKETEXT;
    case PTRACE_POKEDATA:
        return SP_PTRACE_POKEDATA;
#ifdef __aarch64__
    case COMPAT_PTRACE_SETREGS:
        return SP_PTRACE_SETREGS;
    case COMPAT_PTRACE_SET_SYSCALL:
        return SP_PTRACE_SET_SYSCALL;
    case COMPAT_PTRACE_SETVFPREGS:
        return SP_PTRACE_SETREGS;
    case COMPAT_PTRACE_SETHBPREGS:
        return SP_PTRACE_SETREGS;
#else
    case PTRACE_SETREGS:
        return SP_PTRACE_SETREGS;
#endif
    case PTRACE_SETREGSET:
        return SP_PTRACE_SETREGSET;
    case PTRACE_POKEUSR:
        return SP_PTRACE_POKEUSR;
    case PTRACE_ATTACH:
        return SP_PTRACE_ATTACH;
    case PTRACE_SEIZE:
        return SP_PTRACE_SEIZE;
    }

    return SP_IGNORE;
}

#define DECLARE_EVENT(TYPE, SP)                \
    u64 pid_tgid = bpf_get_current_pid_tgid(); \
    u32 pid = pid_tgid >> 32;                  \
    u32 tid = pid_tgid & 0xFFFFFFFF;           \
    u64 mono_ns = bpf_ktime_get_ns();          \
    TYPE ev = {                                \
        .syscall_pattern = SP,                 \
        .pid = pid,                            \
        .tid = tid,                            \
        .mono_ns = mono_ns,                    \
    }

#define DECLARE_CRED_EVENT(SP)                 \
    u64 pid_tgid = bpf_get_current_pid_tgid(); \
    u32 pid = pid_tgid >> 32;                  \
    u32 tid = pid_tgid & 0xFFFFFFFF;           \
    u64 ruid_rgid = bpf_get_current_uid_gid(); \
    u32 __rgid = ruid_rgid >> 32;              \
    u32 __ruid = ruid_rgid & 0xFFFFFFFF;       \
    u64 mono_ns = bpf_ktime_get_ns();          \
    credentials_event_t ev = {                 \
        .syscall_pattern = SP,                 \
        .pid = pid,                            \
        .tid = tid,                            \
        .current_ruid = __ruid,                \
        .current_rgid = __rgid,                \
        .euid = -1,                            \
        .egid = -1,                            \
        .ruid = -1,                            \
        .rgid = -1,                            \
        .suid = -1,                            \
        .sgid = -1,                            \
        .mono_ns = mono_ns,                    \
    }

#define DECLARE_PAM_EVENT(STAGE)               \
    u64 pid_tgid = bpf_get_current_pid_tgid(); \
    u32 pid = pid_tgid >> 32;                  \
    u32 tid = pid_tgid & 0xFFFFFFFF;           \
    u64 mono_ns = bpf_ktime_get_ns();          \
    pam_event_t ev = {                         \
        .syscall_pattern = SP_USERMODE,        \
        .pid = pid,                            \
        .tid = tid,                            \
        .stage = STAGE,                        \
        .mono_ns = mono_ns,                    \
    }

#define DECLARE_TELEMETRY_EVENT(SP)            \
    u64 pid_tgid = bpf_get_current_pid_tgid(); \
    u64 euid_egid = bpf_get_current_uid_gid(); \
    u32 pid = pid_tgid >> 32;                  \
    u32 tid = pid_tgid & 0xFFFFFFFF;           \
    u32 euid = euid_egid >> 32;                \
    u32 egid = euid_egid & 0xFFFFFFFF;         \
    u64 mono_ns = bpf_ktime_get_ns();          \
    telemetry_event_t ev = {0};                \
    ev.done = FALSE;                           \
    ev.u.pid = pid;                            \
    ev.u.tid = tid;                            \
    ev.u.ppid = -1;                            \
    ev.u.luid = -1;                            \
    ev.u.euid = euid;                          \
    ev.u.egid = egid;                          \
    ev.u.mono_ns = mono_ns;                    \
    bpf_get_current_comm(ev.u.comm, sizeof(ev.u.comm));

SEC("kprobe/sys_ptrace_write")
int BPF_KPROBE_SYSCALL(kprobe__sys_ptrace_write,
                       u32 request, u32 target_pid, void *addr)
{
    syscall_pattern_type_t syscall_pattern = ptrace_syscall_pattern(request);
    if (SP_IGNORE == syscall_pattern)
    {
        goto Exit;
    }

    if (SP_PTRACE_ATTACH != syscall_pattern && SP_PTRACE_SEIZE != syscall_pattern)
    {
        DECLARE_EVENT(write_process_memory_event_t, syscall_pattern);
        ev.target_pid = target_pid;
        ev.addresses[0] = (u64) addr;

        bpf_perf_event_output(ctx,
                              &write_process_memory_events,
                              bpf_get_smp_processor_id(),
                              &ev,
                              sizeof(ev));
    }

    Exit:
    return 0;
}

SEC("kprobe/sys_ptrace")
int BPF_KPROBE_SYSCALL(kprobe__sys_ptrace,
                       u32 request, u32 target_pid)
{
    syscall_pattern_type_t syscall_pattern = ptrace_syscall_pattern(request);
    if (SP_IGNORE == syscall_pattern)
    {
        goto Exit;
    }

    if (SP_PTRACE_ATTACH == syscall_pattern || SP_PTRACE_SEIZE == syscall_pattern)
    {
        DECLARE_EVENT(trace_process_event_t, syscall_pattern);
        ev.target_pid = target_pid;

        bpf_perf_event_output(ctx,
                              &trace_process_events,
                              bpf_get_smp_processor_id(),
                              &ev,
                              sizeof(ev));
    }

Exit:
    return 0;
}

SEC("kprobe/sys_process_vm_writev_5_5")
int BPF_KPROBE_SYSCALL(kprobe__sys_process_vm_writev_5_5,
        u32 target_pid, piovec_t liov, u32 liovcnt, piovec_t riov, u32 riovcnt)
{
    DECLARE_EVENT(write_process_memory_event_t, SP_PROCESS_VM_WRITEV);
    ev.target_pid = target_pid;

#pragma unroll
    for (u32 ii = 0; ii < MAX_ADDRESSES && ii < riovcnt; ++ii, riov++)
    {
        iovec_t remote_iov;
        bpf_probe_read_user(&remote_iov, sizeof(remote_iov), (const void *) riov);
        ev.addresses[ii] = (u64)remote_iov.iov_base;
    }

    bpf_perf_event_output(ctx,
                          &write_process_memory_events,
                          bpf_get_smp_processor_id(),
                          &ev,
                          sizeof(ev));

    return 0;
}

SEC("kprobe/sys_process_vm_writev")
int BPF_KPROBE_SYSCALL(kprobe__sys_process_vm_writev,
                       u32 target_pid)
{
    DECLARE_EVENT(write_process_memory_event_t, SP_PROCESS_VM_WRITEV);
    ev.target_pid = target_pid;

    bpf_perf_event_output(ctx,
                          &write_process_memory_events,
                          bpf_get_smp_processor_id(),
                          &ev,
                          sizeof(ev));

    return 0;
}

SEC("kprobe/sys_mprotect")
int BPF_KPROBE_SYSCALL(kprobe__sys_mprotect,
                       void *addr, u64 len, u32 prot)
{
    DECLARE_EVENT(change_memory_permission_event_t, SP_MPROTECT);
    ev.address = (u64) addr;
    ev.len = len;
    ev.prot = prot;

    bpf_perf_event_output(ctx,
                          &change_process_memory_events,
                          bpf_get_smp_processor_id(),
                          &ev,
                          sizeof(ev));

    return 0;
}

SEC("kprobe/do_mount")
int kprobe__do_mount(struct pt_regs *ctx)
{
    DECLARE_EVENT(mount_event_t, SP_MOUNT);

    bpf_probe_read_str(&ev.source, sizeof(ev.source), (void *)PT_REGS_PARM1(ctx));
    bpf_probe_read_str(&ev.target, sizeof(ev.target), (void *)PT_REGS_PARM2(ctx));
    bpf_probe_read_str(&ev.fs_type, sizeof(ev.fs_type), (void *)PT_REGS_PARM3(ctx));
    ev.flags = PT_REGS_PARM4(ctx);
    bpf_probe_read_str(&ev.data, sizeof(ev.data), (void *)PT_REGS_PARM5(ctx));

    bpf_perf_event_output(ctx,
                          &mount_events,
                          bpf_get_smp_processor_id(),
                          &ev,
                          sizeof(ev));

    return 0;
}

static __always_inline int dispatch_credentials_event(struct pt_regs *__ctx)
{
    struct pt_regs ctx = {};
    bpf_probe_read(&ctx, sizeof(ctx), (void *)SYSCALL_PARM1(__ctx));
    u64 pid_tgid = bpf_get_current_pid_tgid();

    credentials_event_t *pcreds = bpf_map_lookup_elem(&cred_hash, &pid_tgid);
    if (NULL == pcreds)
    {
        goto Exit;
    }

    if (0 == PT_REGS_RC(&ctx))
    {
        bpf_perf_event_output(__ctx,
                              &cred_events,
                              bpf_get_smp_processor_id(),
                              pcreds,
                              sizeof(*pcreds));
    }

    bpf_map_delete_elem(&cred_hash, &pid_tgid);
Exit:
    return 0;
}

SEC("kprobe/sys_setuid")
int BPF_KPROBE_SYSCALL(kprobe__sys_setuid,
                       u32 ruid)
{
    DECLARE_CRED_EVENT(SP_SETUID);
    ev.ruid = ruid;
    bpf_map_update_elem(&cred_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("kretprobe/sys_setuid")
int kretprobe__sys_setuid(struct pt_regs *__ctx)
{
    return dispatch_credentials_event(__ctx);
}

SEC("kprobe/sys_setgid")
int BPF_KPROBE_SYSCALL(kprobe__sys_setgid,
                       u32 rgid)
{
    DECLARE_CRED_EVENT(SP_SETGID);
    ev.rgid = rgid;
    bpf_map_update_elem(&cred_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("kretprobe/sys_setgid")
int kretprobe__sys_setgid(struct pt_regs *__ctx)
{
    return dispatch_credentials_event(__ctx);
}

SEC("kprobe/sys_setreuid")
int BPF_KPROBE_SYSCALL(kprobe__sys_setreuid,
                       u32 ruid, u32 euid)
{
    DECLARE_CRED_EVENT(SP_SETREUID);
    ev.ruid = ruid;
    ev.euid = euid;
    bpf_map_update_elem(&cred_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("kretprobe/sys_setreuid")
int kretprobe__sys_setreuid(struct pt_regs *__ctx)
{
    return dispatch_credentials_event(__ctx);
}

SEC("kprobe/sys_setregid")
int BPF_KPROBE_SYSCALL(kprobe__sys_setregid,
                       u32 rgid, u32 egid)
{
    DECLARE_CRED_EVENT(SP_SETREGID);
    ev.rgid = rgid;
    ev.egid = egid;
    bpf_map_update_elem(&cred_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("kretprobe/sys_setregid")
int kretprobe__sys_setregid(struct pt_regs *__ctx)
{
    return dispatch_credentials_event(__ctx);
}

SEC("kprobe/sys_setresuid")
int BPF_KPROBE_SYSCALL(kprobe__sys_setresuid,
                       u32 ruid, u32 euid, u32 suid)
{
    DECLARE_CRED_EVENT(SP_SETREUID);
    ev.ruid = ruid;
    ev.euid = euid;
    ev.suid = suid;
    bpf_map_update_elem(&cred_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("kretprobe/sys_setresuid")
int kretprobe__sys_setresuid(struct pt_regs *__ctx)
{
    return dispatch_credentials_event(__ctx);
}

SEC("kprobe/sys_setresgid")
int BPF_KPROBE_SYSCALL(kprobe__sys_setresgid,
                       u32 rgid, u32 egid, u32 sgid)
{
    DECLARE_CRED_EVENT(SP_SETREGID);
    ev.rgid = rgid;
    ev.egid = egid;
    ev.sgid = sgid;
    bpf_map_update_elem(&cred_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("kretprobe/sys_setresgid")
int kretprobe__sys_setresgid(struct pt_regs *__ctx)
{
    return dispatch_credentials_event(__ctx);
}

SEC("uprobe/pam_start")
int uprobe__pam_start(struct pt_regs *ctx)
{
    DECLARE_PAM_EVENT(PAM_START);
    bpf_probe_read(ev.u.pam_start.service_name, sizeof(ev.u.pam_start.user_name), (void *)PT_REGS_PARM1(ctx));
    bpf_probe_read(ev.u.pam_start.service_name, sizeof(ev.u.pam_start.user_name), (void *)PT_REGS_PARM2(ctx));
    ev.pam_handle = (u64)PT_REGS_PARM4(ctx);
    bpf_map_update_elem(&pam_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("uretprobe/pam_start")
int uretprobe__pam_start(struct pt_regs *ctx)
{
    u64 pid_tgid = bpf_get_current_pid_tgid();

    pam_event_t *ppam = bpf_map_lookup_elem(&pam_hash, &pid_tgid);
    if (NULL == ppam)
    {
        goto Exit;
    }

    if (0 == PT_REGS_RC(ctx))
    {
        bpf_probe_read(&ppam->pam_handle, sizeof(ppam->pam_handle), (void *)ppam->pam_handle);

        bpf_perf_event_output(ctx,
                              &pam_events,
                              bpf_get_smp_processor_id(),
                              ppam,
                              sizeof(*ppam));
    }

    bpf_map_delete_elem(&pam_hash, &pid_tgid);
Exit:
    return 0;
}

SEC("uprobe/pam_end")
int uprobe__pam_end(struct pt_regs *ctx)
{
    DECLARE_PAM_EVENT(PAM_END);
    ev.pam_handle = (u64)PT_REGS_PARM1(ctx);
    bpf_perf_event_output(ctx,
                          &pam_events,
                          bpf_get_smp_processor_id(),
                          &ev,
                          sizeof(ev));
    return 0;
}

SEC("uprobe/pam_authenticate")
int uprobe__pam_authenticate(struct pt_regs *ctx)
{
    DECLARE_PAM_EVENT(PAM_AUTHENTICATE);
    ev.pam_handle = (u64)PT_REGS_PARM1(ctx);
    ev.flags = (u64)PT_REGS_PARM2(ctx);
    bpf_map_update_elem(&pam_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("uretprobe/pam_authenticate")
int uretprobe__pam_authenticate(struct pt_regs *ctx)
{
    u64 pid_tgid = bpf_get_current_pid_tgid();

    pam_event_t *ppam = bpf_map_lookup_elem(&pam_hash, &pid_tgid);
    if (NULL == ppam)
    {
        goto Exit;
    }

    ppam->result = (u32)PT_REGS_RC(ctx);
    bpf_perf_event_output(ctx,
                          &pam_events,
                          bpf_get_smp_processor_id(),
                          ppam,
                          sizeof(*ppam));

    bpf_map_delete_elem(&pam_hash, &pid_tgid);
Exit:
    return 0;
}

SEC("uprobe/pam_chauthtok")
int uprobe__pam_chauthtok(struct pt_regs *ctx)
{
    DECLARE_PAM_EVENT(PAM_CHAUTHTOK);
    ev.pam_handle = (u64)PT_REGS_PARM1(ctx);
    ev.flags = (u64)PT_REGS_PARM2(ctx);
    bpf_map_update_elem(&pam_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("uretprobe/pam_chauthtok")
int uretprobe__pam_chauthtok(struct pt_regs *ctx)
{
    u64 pid_tgid = bpf_get_current_pid_tgid();

    pam_event_t *ppam = bpf_map_lookup_elem(&pam_hash, &pid_tgid);
    if (NULL == ppam)
    {
        goto Exit;
    }

    ppam->result = (u32)PT_REGS_RC(ctx);
    bpf_perf_event_output(ctx,
                          &pam_events,
                          bpf_get_smp_processor_id(),
                          ppam,
                          sizeof(*ppam));

    bpf_map_delete_elem(&pam_hash, &pid_tgid);
Exit:
    return 0;
}

SEC("uprobe/pam_set_item")
int uprobe__pam_set_item(struct pt_regs *ctx)
{
    DECLARE_PAM_EVENT(PAM_SET_ITEM);
    ev.pam_handle = (u64)PT_REGS_PARM1(ctx);
    ev.u.pam_set_item.item_type = (pam_item_type_t)PT_REGS_PARM2(ctx);
    bpf_probe_read(ev.u.pam_set_item.data, sizeof(ev.u.pam_set_item.data), (void *)PT_REGS_PARM3(ctx));
    bpf_map_update_elem(&pam_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("uretprobe/pam_set_item")
int uretprobe__pam_set_item(struct pt_regs *ctx)
{
    u64 pid_tgid = bpf_get_current_pid_tgid();

    pam_event_t *ppam = bpf_map_lookup_elem(&pam_hash, &pid_tgid);
    if (NULL == ppam)
    {
        goto Exit;
    }

    ppam->result = (u32)PT_REGS_RC(ctx);
    bpf_perf_event_output(ctx,
                          &pam_events,
                          bpf_get_smp_processor_id(),
                          ppam,
                          sizeof(*ppam));

    bpf_map_delete_elem(&pam_hash, &pid_tgid);
Exit:
    return 0;
}

SEC("uprobe/pam_setcred")
int uprobe__pam_setcred(struct pt_regs *ctx)
{
    DECLARE_PAM_EVENT(PAM_SET_CRED);
    ev.pam_handle = (u64)PT_REGS_PARM1(ctx);
    ev.flags = (u64)PT_REGS_PARM2(ctx);
    bpf_map_update_elem(&pam_hash, &pid_tgid, &ev, BPF_ANY);
    return 0;
}

SEC("uretprobe/pam_setcred")
int uretprobe__pam_setcred(struct pt_regs *ctx)
{
    u64 pid_tgid = bpf_get_current_pid_tgid();

    pam_event_t *ppam = bpf_map_lookup_elem(&pam_hash, &pid_tgid);
    if (NULL == ppam)
    {
        goto Exit;
    }

    ppam->result = (u32)PT_REGS_RC(ctx);
    bpf_perf_event_output(ctx,
                          &pam_events,
                          bpf_get_smp_processor_id(),
                          ppam,
                          sizeof(*ppam));

    bpf_map_delete_elem(&pam_hash, &pid_tgid);
Exit:
    return 0;
}

SEC("uprobe/read_return_string")
int uprobe__read_return_string(struct pt_regs *ctx)
{
    DECLARE_EVENT(read_return_string_event_t, SP_USERMODE);
    bpf_probe_read(ev.value, sizeof(ev.value), (void *)PT_REGS_RC(ctx));
    bpf_perf_event_output(ctx,
                          &rrs_events,
                          bpf_get_smp_processor_id(),
                          &ev,
                          sizeof(ev));
    return 0;
}

static __always_inline void clear_telemetry_events()
{
    // The key is always zero since the index is just a simple state counter
    u32 key = 0;
    u32 index = 0;
    bpf_map_update_elem(&telemetry_index, &key, &index, BPF_ANY);
}

static __always_inline void push_telemetry_event(ptelemetry_event_t ev)
{
    u32 key = 0;
    u32 *pcurrent_index = bpf_map_lookup_elem(&telemetry_index, &key);
    if (NULL == pcurrent_index)
    {
        return;
    }

    bpf_map_update_elem(&telemetry_stack, pcurrent_index, ev, BPF_ANY);

    // Update the index
    (*pcurrent_index) += 1;
    bpf_map_update_elem(&telemetry_index, &key, pcurrent_index, BPF_ANY);
}

static __always_inline void flush_telemetry_events(struct pt_regs *ctx)
{
    u32 key = 0;
    u32 *pcurrent_index = bpf_map_lookup_elem(&telemetry_index, &key);
    if (NULL == pcurrent_index)
    {
        return;
    }

#pragma unroll
    for (u32 ii = 0; ii < MAX_TELEMETRY_STACK_ENTRIES && ii < *pcurrent_index; ++ii)
    {
        ptelemetry_event_t ev = bpf_map_lookup_elem(&telemetry_stack, &ii);
        if (NULL == ev)
        {
            continue;
        }

        if (ii == (*pcurrent_index - 1))
        {
            ev->done = TRUE;
        }

        bpf_perf_event_output(ctx,
                              &telemetry_events,
                              bpf_get_smp_processor_id(),
                              ev,
                              sizeof(*ev));
    }

    clear_telemetry_events();
}

SEC("kprobe/sys_execve")
int kprobe__sys_execve(struct pt_regs *__ctx)
{
    struct pt_regs* ctx = NULL;
    bpf_probe_read(&ctx, sizeof(ctx), (void *)&PT_REGS_PARM1(__ctx));
    DECLARE_TELEMETRY_EVENT(SP_EXECVE);

    push_telemetry_event(&ev);
    return 0;
}

SEC("kretprobe/sys_execve")
int kretprobe__sys_execve(struct pt_regs *__ctx)
{
    struct pt_regs ctx = {};
    bpf_probe_read(&ctx, sizeof(ctx), (void *)PT_REGS_PARM1(__ctx));

    if (0 == PT_REGS_RC(&ctx))
    {
        flush_telemetry_events(__ctx);
    }
    else
    {
        clear_telemetry_events();
    }

    return 0;
}

char _license[] SEC("license") = "GPL";
uint32_t _version SEC("version") = 0xFFFFFFFE;
