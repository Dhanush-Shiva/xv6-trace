# xv6-riscv Recipe: System Call Tracing

A step-by-step implementation guide for the **System Call Tracing** lab
in xv6-riscv. It includes the idea, files to edit, code, build/run
commands, testing, common errors, and a short explanation you can give
your lecturer.

> **Version note:** This recipe targets the standard MIT xv6-riscv
> layout (`kernel/`, `user/`, `Makefile`) where system-call numbers are
> in `kernel/syscall.h` and process state is in `kernel/proc.h`. Lab
> distributions differ slightly. Inspect the existing files before
> editing, and avoid adding a second copy of a function or syscall
> number if your starter code already contains it.

------------------------------------------------------------------------

## 1. What are we implementing?

A process calls:

``` c
trace(mask);
```

`mask` is a bit mask. Each bit corresponds to one system-call number. If
bit `n` is set, syscall number `n` should be traced.

For example:

``` c
trace(1 << SYS_fork);
```

means "trace `fork` calls."

When a traced system call is about to return to user space, the kernel
prints:

``` text
PID: syscall NAME -> RETURN_VALUE
```

Example:

``` text
3: syscall read -> 1023
```

This means process ID `3` called `read`, and the system call returned
`1023`.

The tracing setting belongs to a **process**, not globally to the whole
kernel. A process's children inherit the mask when they are created by
`fork()`. Unrelated processes do not inherit it.

### What does the example `trace 32 grep hello README` mean?

The user-level `trace` program takes the mask (`32`) and the command to
run (`grep hello README`). It enables tracing and then executes `grep`.

In the usual xv6 syscall-number assignment, `SYS_read` is `5`, so:

``` text
32 = 1 << 5
```

Binary representation:

``` text
32 decimal = 000...00100000 binary
                         ^
                         bit 5 is set
```

Therefore, only syscall number 5 (`read`) is selected, so the output
shows `read` calls.

For the all-syscalls example, `2147483647` is `0x7fffffff`, with bits 0
through 30 set. It selects all syscall numbers in that range. A `trace`
line can appear too because the `trace()` syscall itself returns after
the mask has been enabled.

The output values are return values, not arguments. For example,
`exec -> 3` means `exec` returned `3`; it does **not** mean that the
command has PID 3.

------------------------------------------------------------------------

## 2. How the implementation works

``` text
User program: trace <mask> <command>
                  |
                  | calls trace(mask)
                  v
          sys_trace() in kernel
                  |
                  | saves mask in current process
                  v
          process continues / execs command
                  |
                  | invokes a system call
                  v
          syscall() dispatches the call
                  |
                  | system call finishes; return value is in a0
                  v
          test: mask & (1 << syscall_number)
             /                         \
           zero                       non-zero
            |                            |
       print nothing             print PID, name, return
```

The core test is:

``` c
if (p->tracemask & (1 << num)) {
    // Print this system call.
}
```

-   `num` is the syscall number.
-   `1 << num` creates a value with only bit `num` set.
-   `&` is bitwise AND. The result is non-zero if that bit is enabled in
    `tracemask`.

For example, if `num == 5`, then `1 << 5` is `32`. If the mask is `32`,
`32 & 32` is non-zero, so `read` is traced.

------------------------------------------------------------------------

## 3. Before you edit anything

### Step 1 --- Open your xv6-riscv directory

In WSL/Ubuntu, go to the directory where you cloned xv6. Example only:

``` bash
cd ~/xv6-riscv
```

If you are unsure where it is:

``` bash
find ~ -maxdepth 3 -type f -path '*/kernel/syscall.c' 2>/dev/null
```

Then `cd` into the directory containing `kernel/`, `user/`, and
`Makefile`.

### Step 2 --- Check the repository and relevant files

``` bash
git status
grep -n "SYS_close\\|SYS_trace" kernel/syscall.h
grep -n "struct proc" kernel/proc.h
grep -n "syscall(void)\\|syscalls\\[" kernel/syscall.c
grep -n "sys_close\\|sys_uptime" kernel/sysproc.c
grep -n "fork(void)" kernel/proc.c
grep -n "UPROGS\\|_trace" Makefile
ls user/trace.c
```

Some `grep` commands may find nothing; that is normal before
implementation. If `user/trace.c` is missing, check your course/lab
starter files. The assignment may expect you to use a supplied version.
Do not replace it blindly with a different lab version.

### Step 3 --- Optional but recommended: make a branch

``` bash
git switch -c syscall-tracing
```

If Git says the branch already exists, choose another name or continue
on your current branch.

------------------------------------------------------------------------

## 4. Modify `kernel/proc.h`: store the mask per process

Open the file:

``` bash
vim kernel/proc.h
```

Find `struct proc`. Add an integer field named `tracemask` inside the
structure, preferably near other process metadata such as `pid`,
`state`, or `parent`.

For example, the relevant part should look similar to:

``` c
struct proc {
  struct spinlock lock;

  // existing fields ...
  enum procstate state;
  void *chan;
  int killed;
  int xstate;
  int pid;

  // System-call tracing bit mask.
  int tracemask;

  // remaining existing fields ...
};
```

**Important:** do not replace the whole structure with this excerpt.
Keep all existing fields in your version and add only:

``` c
int tracemask;
```

Why here? Each process needs its own mask. A global variable would cause
one process's tracing configuration to affect other processes.

The process-allocation code in standard xv6 clears/initializes process
state. To be explicit, we will also initialize the new field in
`allocproc()`.

------------------------------------------------------------------------

## 5. Modify `kernel/proc.c`: initialize the mask and inherit it in `fork()`

Open:

``` bash
vim kernel/proc.c
```

### Step 5A --- Initialize it in `allocproc()`

Find `allocproc()`. After a process slot has been selected and its basic
fields are initialized, set:

``` c
p->tracemask = 0;
```

Place it in the initialization section for the newly allocated process,
before that process is returned for use. This ensures a reused process
slot does not retain an old tracing mask.

### Step 5B --- Copy it in `fork()`

Find the `fork()` implementation. It creates a child process `np` from
the current process `p`. Near the other inherited process attributes,
add:

``` c
np->tracemask = p->tracemask;
```

For example, if your code has a section similar to:

``` c
safestrcpy(np->name, p->name, sizeof(p->name));
pid = np->pid;
```

put the mask copy near that section:

``` c
safestrcpy(np->name, p->name, sizeof(p->name));
np->tracemask = p->tracemask;
pid = np->pid;
```

Use the variable names in your own `fork()` implementation. The key is
to copy the parent's mask to the child before the child begins running.

**Why?** The assignment says tracing must continue into children created
by `fork()`. The child gets a copy of the mask at fork time. Changing
the parent's mask later does not automatically change an already-created
child's mask.

------------------------------------------------------------------------

## 6. Modify `kernel/syscall.h`: assign a syscall number

Open:

``` bash
vim kernel/syscall.h
```

Find the existing syscall number definitions. In standard xv6-riscv, the
definitions commonly end with:

``` c
#define SYS_close  21
```

Add the next unused number:

``` c
#define SYS_trace  22
```

Do not reuse a number. If your starter tree already has another syscall
numbered 22, choose the next unused number and use it consistently in
all files.

The user program and kernel must agree on this number.

------------------------------------------------------------------------

## 7. Modify `kernel/sysproc.c`: implement `sys_trace()`

Open:

``` bash
vim kernel/sysproc.c
```

Add the following function alongside the other `sys_*` system-call
handlers:

``` c
uint64
sys_trace(void)
{
  int mask;

  if (argint(0, &mask) < 0)
    return -1;

  myproc()->tracemask = mask;
  return 0;
}
```

If the file already declares the required types/functions through its
existing includes, no new include should be necessary. Follow the style
of the neighboring syscall handlers.

### How it works

1.  `argint(0, &mask)` reads argument 0 from the user process.
2.  If reading the argument fails, return `-1`.
3.  `myproc()` returns the currently running process.
4.  Store the supplied mask in that process's `tracemask`.
5.  Return `0` to indicate success.

This is the kernel-side implementation. It does not print traces itself;
the central syscall dispatcher does that for all system calls.

------------------------------------------------------------------------

## 8. Modify `kernel/syscall.c`: register and print traced calls

This file performs syscall dispatch. You need to make three changes.

Open it:

``` bash
vim kernel/syscall.c
```

### Step 8A --- Declare the handler

Near the other syscall handler declarations, add:

``` c
extern uint64 sys_trace(void);
```

### Step 8B --- Register it in the syscall table

Find the `syscalls[]` table and add:

``` c
[SYS_trace] sys_trace,
```

For example, the end of the table may look similar to:

``` c
[SYS_close] sys_close,
[SYS_trace] sys_trace,
```

Keep the existing entries. Do not duplicate the `SYS_trace` entry.

### Step 8C --- Add syscall names and tracing output

The kernel needs a readable name for each syscall number. Add a name
table near the syscall dispatch table. Use designated indexes so each
string matches the syscall number.

For the standard xv6-riscv syscall set, this table is:

``` c
static char *syscall_names[] = {
  [SYS_fork]    "fork",
  [SYS_exit]    "exit",
  [SYS_wait]    "wait",
  [SYS_pipe]    "pipe",
  [SYS_read]    "read",
  [SYS_kill]    "kill",
  [SYS_exec]    "exec",
  [SYS_fstat]   "fstat",
  [SYS_chdir]   "chdir",
  [SYS_dup]     "dup",
  [SYS_getpid]  "getpid",
  [SYS_sbrk]    "sbrk",
  [SYS_sleep]   "sleep",
  [SYS_uptime]  "uptime",
  [SYS_open]    "open",
  [SYS_write]   "write",
  [SYS_mknod]   "mknod",
  [SYS_unlink]  "unlink",
  [SYS_link]    "link",
  [SYS_mkdir]   "mkdir",
  [SYS_close]   "close",
  [SYS_trace]   "trace",
};
```

Make sure each `SYS_*` symbol exists in your `kernel/syscall.h`. If your
lab adds extra syscalls, add their names too. Keep the name table in
sync with the syscall-number definitions and the dispatch table.

Now find the `syscall(void)` function. In standard xv6-riscv, it has
logic similar to:

``` c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %d\n",
           p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

Keep the existing function and add the trace check **after the handler
has executed and placed its return value in `p->trapframe->a0`**. One
safe way is to use this complete replacement for the function if its
structure matches your starter version:

``` c
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    p->trapframe->a0 = syscalls[num]();

    if ((p->tracemask & (1 << num)) &&
        num < NELEM(syscall_names) &&
        syscall_names[num] != 0) {
      printf("%d: syscall %s -> %d\n",
             p->pid, syscall_names[num], p->trapframe->a0);
    }
  } else {
    printf("%d %s: unknown sys call %d\n",
           p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
```

If your `syscall()` function has extra logic, preserve it and integrate
only the `if` block after the syscall handler runs.

### Why print after the handler?

The system call's return value is stored in `p->trapframe->a0`. Printing
after `syscalls[num]()` runs ensures that the output reports the actual
result.

### Why can `trace` trace itself?

`sys_trace()` sets `p->tracemask` before returning. When the dispatcher
checks the mask after the handler finishes, the new mask is already
active. Thus, if the new mask includes `SYS_trace`, the `trace` call
itself is printed.

### Why check `syscall_names[num] != 0`?

Designated arrays contain empty/null entries for indexes that were not
assigned. This check avoids printing a missing name if a valid syscall
number has no corresponding label.

------------------------------------------------------------------------

## 9. Connect the user-space interface

The kernel implementation is not enough by itself. User programs need a
declaration and a generated syscall stub.

### Step 9A --- `user/user.h`

Open:

``` bash
vim user/user.h
```

Add this declaration alongside the other system-call declarations:

``` c
int trace(int);
```

### Step 9B --- `user/usys.pl`

Open:

``` bash
vim user/usys.pl
```

Find the list of `entry("...");` declarations. Add:

``` perl
entry("trace");
```

This generates the assembly stub that lets user code invoke the kernel
system call.

### Step 9C --- `user/trace.c`

The assignment says a user-level `trace` program is provided. Check it:

``` bash
cat user/trace.c
```

A common version looks like this:

``` c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;
  int mask;

  if (argc < 3) {
    fprintf(2, "Usage: trace mask command\n");
    exit(1);
  }

  mask = atoi(argv[1]);
  trace(mask);

  for (i = 2; i < argc; i++)
    argv[i - 2] = argv[i];
  argv[argc - 2] = 0;

  exec(argv[0], argv);

  fprintf(2, "trace: exec %s failed\n", argv[0]);
  exit(1);
}
```

If your course supplied `user/trace.c`, normally keep that supplied file
rather than overwriting it. The essential behavior is: parse the mask,
call `trace(mask)`, shift the command arguments to the front of `argv`,
and `exec()` the command.

### Step 9D --- `Makefile`

Open:

``` bash
vim Makefile
```

Find `UPROGS`. Add this entry if it is not already present:

``` make
$U/_trace\
```

For example, the list may include `$U/_cat\`, `$U/_echo\`, and
`$U/_grep\`. Add `$U/_trace\` in the same style, preserving the
Makefile's backslash continuation syntax.

This causes xv6's build to include the user program in the filesystem
image. If `_trace` is already listed, do not add it twice.

------------------------------------------------------------------------

## 10. Review checklist before compiling

From the xv6 repository root, check all the required links:

``` bash
grep -n "tracemask" kernel/proc.h kernel/proc.c
grep -n "SYS_trace" kernel/syscall.h kernel/syscall.c
grep -n "sys_trace" kernel/sysproc.c kernel/syscall.c
grep -n "trace" user/user.h user/usys.pl Makefile
```

Expected changes:

  -----------------------------------------------------------------------
  File                                Required change
  ----------------------------------- -----------------------------------
  `kernel/proc.h`                     Add per-process `tracemask`

  `kernel/proc.c`                     Initialize mask to 0; copy parent
                                      mask to child

  `kernel/syscall.h`                  Define `SYS_trace` with an unused
                                      number

  `kernel/sysproc.c`                  Implement `sys_trace()`

  `kernel/syscall.c`                  Declare/register handler, map
                                      syscall names, print matching calls

  `user/user.h`                       Declare `int trace(int);`

  `user/usys.pl`                      Add `entry("trace");`

  `user/trace.c`                      User command wrapper; usually
                                      supplied

  `Makefile`                          Include `$U/_trace\` in `UPROGS`
  -----------------------------------------------------------------------

------------------------------------------------------------------------

## 11. Compile and run xv6

Run these commands from the repository root.

### Step 11A --- Clean and build

``` bash
make clean
make qemu
```

If the build succeeds, xv6 should boot in QEMU and show its shell
prompt:

``` text
$
```

If your course uses a different QEMU target, inspect the README or
Makefile. The standard MIT xv6-riscv setup usually supports `make qemu`.

### Step 11B --- Test only `read`

At the xv6 shell:

``` text
$ trace 32 grep hello README
```

Why `32`? In the standard syscall numbering, `SYS_read == 5`, and
`1 << 5 == 32`.

Expected output will resemble:

``` text
3: syscall read -> 1023
3: syscall read -> 966
3: syscall read -> 70
3: syscall read -> 0
```

The exact PID and return values can differ depending on the version of
the filesystem and the `README` contents.

### Step 11C --- Test many syscalls

``` text
$ trace 2147483647 grep hello README
```

You should see lines for several calls, such as `trace`, `exec`, `open`,
`read`, and `close`, depending on which calls occur and your exact
syscall table.

### Step 11D --- Check that tracing is not global

Run a command without the wrapper:

``` text
$ grep hello README
```

It should not produce tracing lines. The command's normal output may be
empty if no matching line exists.

### Step 11E --- Test inheritance through `fork()`

``` text
$ trace 2 usertests forkforkfork
```

In the standard syscall numbering, `SYS_fork == 1`, so `1 << 1 == 2`.
The test should print `fork` calls from processes in the fork tree, not
only from the original process.

If your lab image does not include the full `usertests` binary or does
not support that test name, use the tests provided by your course. The
central property to verify is that children inherit the mask.

------------------------------------------------------------------------

## 12. Understanding the bit mask

A mask is a compact way to enable multiple options using one integer.

  Syscall     Standard number Bit expression     Mask value
  --------- ----------------- ---------------- ------------
  `fork`                    1 `1 << 1`                    2
  `read`                    5 `1 << 5`                   32
  `exec`                    7 `1 << 7`                  128
  `open`                   15 `1 << 15`               32768
  `close`                  21 `1 << 21`             2097152

These numbers are for the standard xv6-riscv syscall table described
above. Confirm them in your own `kernel/syscall.h`.

To trace both `fork` and `read`, combine the bits with bitwise OR:

``` c
trace((1 << SYS_fork) | (1 << SYS_read));
```

Using the standard numbers, this is:

``` text
2 | 32 = 34
```

So this command traces both calls:

``` text
$ trace 34 somecommand
```

Avoid writing `1 << SYS_fork + SYS_read`; operator precedence and the
intended operation make that expression incorrect for combining mask
bits. Use parentheses and `|`.

------------------------------------------------------------------------

## 13. Common problems and fixes

### Error: `undefined reference to sys_trace`

Likely cause: `sys_trace()` was not added correctly or its
declaration/table registration is missing.

Check:

``` bash
grep -n "sys_trace" kernel/sysproc.c kernel/syscall.c
```

Ensure the function is defined in `kernel/sysproc.c`, declared with
`extern uint64 sys_trace(void);`, and registered in `syscalls[]`.

### Error: `trace` command is not found in xv6

Likely cause: the user program was not built into the filesystem image.

Check `user/trace.c` exists and `Makefile` contains:

``` make
$U/_trace\
```

Then rebuild:

``` bash
make clean
make qemu
```

### Error: `trace` is an unknown system call

Check all four places that connect the user program to the kernel:

1.  `SYS_trace` in `kernel/syscall.h`
2.  `sys_trace()` in `kernel/sysproc.c`
3.  `[SYS_trace] sys_trace` in `kernel/syscall.c`
4.  `entry("trace");` in `user/usys.pl`

Also confirm that the syscall number is unique and consistent.

### No trace output appears

-   Confirm the mask selects the desired syscall. For `read`, use `32`
    in the standard table.
-   Confirm the dispatcher checks the mask **after** running the syscall
    handler.
-   Confirm the child mask is copied in `fork()` if testing child
    processes.
-   Confirm the syscall name array has the matching designated entry.

### Only the parent is traced

You likely forgot:

``` c
np->tracemask = p->tracemask;
```

in `fork()`.

### A wrong syscall name is printed

Your syscall-name table is out of sync with `kernel/syscall.h`. Use
designated indexes such as `[SYS_read] "read"` rather than relying on a
manually ordered list of strings.

### Compiler complains about a missing field or duplicate definition

-   Ensure `tracemask` is inside `struct proc`, not outside it.
-   Add the field only once.
-   Add the syscall number and table entry only once.
-   Do not paste the sample `struct proc` or entire syscall table over
    your existing version.

------------------------------------------------------------------------

## 14. How to explain this project to your lecturer

**Aim:** To implement a per-process system-call tracing mechanism in
xv6-riscv for debugging.

**Working:**

1.  A user runs `trace <mask> <command>`.
2.  The user program parses the mask and invokes the new `trace()`
    system call.
3.  The kernel's `sys_trace()` stores the mask in the currently running
    process's `tracemask` field.
4.  When a system call occurs, the dispatcher obtains its number from
    register `a7`.
5.  The dispatcher invokes the handler and receives the return value in
    register `a0`.
6.  It checks `tracemask & (1 << syscall_number)`.
7.  If the bit is set, the kernel prints the process ID, syscall name,
    and return value.
8.  When `fork()` creates a child, the child inherits the parent's mask.
    Other processes have independent masks.

**Why is the mask stored in `struct proc`?** Because tracing is
process-specific. A global mask would incorrectly affect unrelated
processes.

**Why is the check done after the handler?** So the printed value is the
actual return value of the system call.

**Why does `fork()` copy the mask?** So tracing follows the process tree
created after tracing is enabled.

**Why is a syscall-name table required?** The syscall dispatcher has a
numeric ID, but the requested output must display a readable name such
as `read` or `fork`.

**What is the result?** The kernel selectively logs system calls for the
chosen process and its descendants, without enabling tracing globally.

------------------------------------------------------------------------

## 15. Final verification

Before submission, verify the following:

-   [ ] `trace()` is declared in `user/user.h`.
-   [ ] `entry("trace");` is present in `user/usys.pl`.
-   [ ] `SYS_trace` uses a unique syscall number.
-   [ ] `sys_trace()` saves the mask in the current process.
-   [ ] The syscall handler is declared and registered.
-   [ ] Every supported syscall has the correct name-table entry.
-   [ ] The dispatcher prints only when the corresponding mask bit is
    enabled.
-   [ ] The output is printed after the handler returns.
-   [ ] New processes start with a zero mask.
-   [ ] `fork()` copies the mask to the child.
-   [ ] `trace` is included in `UPROGS`.
-   [ ] `make qemu` succeeds.
-   [ ] `trace 32 grep hello README` traces `read`.
-   [ ] `grep hello README` without `trace` does not print tracing
    lines.
-   [ ] `trace 2 usertests forkforkfork` demonstrates inheritance, if
    that test is available.

**Core idea to remember:** the feature has three parts---store a mask
per process, inherit it during `fork()`, and check the mask in the
central syscall dispatcher after each handler returns.
