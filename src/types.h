// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause

#pragma once

#include <linux/types.h>

#define MAX_ADDRESSES 16

typedef enum
{
    PAM_SERVICE = 1,       /* The service name */
    PAM_USER = 2,          /* The user name */
    PAM_TTY = 3,           /* The tty name */
    PAM_RHOST = 4,         /* The remote host name */
    PAM_CONV = 5,          /* The pam_conv structure */
    PAM_AUTHTOK = 6,       /* The authentication token (password) */
    PAM_OLDAUTHTOK = 7,    /* The old authentication token */
    PAM_RUSER = 8,         /* The remote user name */
    PAM_USER_PROMPT = 9,   /* the prompt for getting a username -PAM extensions */
    PAM_FAIL_DELAY = 10,   /* app supplied function to override failure delays */
    PAM_XDISPLAY = 11,     /* X display name */
    PAM_XAUTHDATA = 12,    /* X server authentication data */
    PAM_AUTHTOK_TYPE = 13, /* The type for pam_get_authtok */
} pam_item_type_t;

typedef enum
{
    SP_IGNORE,
    SP_USERMODE,
    SP_OPEN_WRITE_PROC_MEM,
    SP_PROCESS_VM_WRITEV,
    SP_PTRACE_POKETEXT,
    SP_PTRACE_POKEDATA,
    SP_PTRACE_SETREGS,
    SP_PTRACE_SETREGSET,
    SP_PTRACE_POKEUSR,
    SP_PTRACE_ATTACH,
    SP_PTRACE_SEIZE,
    SP_MPROTECT,
    SP_PKEY_MPROTECT,
    SP_MOUNT,
    SP_SETUID,
    SP_SETGID,
    SP_SETREUID,
    SP_SETREGID,
    SP_SETRESUID,
    SP_SETRESGID,
} syscall_pattern_t;

#define COMMON_FIELDS \
    u32 pid;          \
    u32 tid;          \
    u64 mono_ns;      \
    u32 __pad;        \
    syscall_pattern_t syscall_pattern;

typedef struct
{
    COMMON_FIELDS;
    u32 target_pid;
    u32 _pad;
} trace_process_event_t;

typedef struct
{
    COMMON_FIELDS;
    u32 target_pid;
    u64 addresses[MAX_ADDRESSES];
} write_process_memory_event_t;

typedef struct
{
    COMMON_FIELDS;
    u64 address;
    u64 len;
    u32 prot;
    u32 _pad;
} change_memory_permission_event_t;

typedef struct
{
    COMMON_FIELDS;
    u64 flags;
    u8 source[128];
    u8 target[128];
    u8 fs_type[16];
    u8 data[64];
} mount_event_t;

typedef struct
{
    COMMON_FIELDS;
    u32 current_ruid;
    u32 current_rgid;
    u32 euid;
    u32 egid;
    u32 ruid;
    u32 rgid;
    u32 suid;
    u32 sgid;
} credentials_event_t;

typedef enum
{
    PAM_START,
    PAM_AUTHENTICATE,
    PAM_CHAUTHTOK,
    PAM_SET_ITEM,
    PAM_SET_CRED,
    PAM_END,
} pam_stage_t;

typedef struct
{
    u8 service_name[128];
    u8 user_name[128];
} pam_start_t;

typedef struct
{
    pam_item_type_t item_type;
    u8 data[256];
} pam_set_item_t;

typedef struct
{
    COMMON_FIELDS;
    u64 pam_handle;
    pam_stage_t stage;
    u32 result;
    u32 flags;
    union
    {
        pam_start_t pam_start;
        pam_set_item_t pam_set_item;
    } u;

} pam_event_t;

typedef struct
{
    COMMON_FIELDS;
    char value[384];
} read_return_string_t;