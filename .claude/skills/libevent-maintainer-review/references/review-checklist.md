# Review checklist

Every item is labeled with its actual grounding. Say which kind you're citing when you raise something in a
review — don't let a generic item read as if it were fork-specific precedent, and don't let the one
fork-grounded item read as if a human reviewer has ever actually enforced it here.

## From this fork's own CONTRIBUTING.md (real, current, but never observed being enforced in a review — see
`references/mined-history.md`)

1. **Coding style.** `CONTRIBUTING.md` points at `./checkpatch.sh -r HEAD` (or piping `git format-patch` /
   `git show` output into it) as the project's own style-check tool. If you have the diff checked out, run it;
   otherwise read the diff against the general shape of the surrounding file (this codebase's existing style
   is the practical baseline `checkpatch.sh` itself checks against).
2. **New tests live under `test/regress_*.c`.** If the PR changes behavior in a `.c` file under the repo root
   (not just docs/CI/build metadata), check whether a corresponding `test/regress_*.c` test was added or
   updated. This is stated policy in `CONTRIBUTING.md` today; note in your review that it's real, current
   guidance, not something enforced by a past reviewer on this fork.
3. **`make verify`** is the project's own documented test-run command. If the PR's description doesn't mention
   having run it (or an equivalent CI-driven build+test), that's a reasonable, specifically-grounded question
   to ask rather than a generic one.

## Generic due diligence for a low-level, widely-embedded C networking library (NOT drawn from this fork's
history — say so if you raise it)

4. **Memory safety in touched code.** Manual buffer/length arithmetic, `malloc`/`free` pairing, use of
   `strcpy`/`sprintf`/`strcat` or similar unchecked functions, off-by-one risk in loop bounds — libevent is
   embedded in a huge number of downstream projects, so a memory-safety regression here has an unusually wide
   blast radius. Flag anything that looks unchecked, not just anything that looks obviously wrong.
5. **Portability across event backends and platforms.** libevent's whole design spans epoll/kqueue/select/
   poll/devpoll/evport/IOCP/wepoll and Unix/Windows. A change that only makes sense against one backend
   (e.g. edits to `epoll.c` without considering `kqueue.c`/`select.c`'s equivalent code path, or POSIX-only
   assumptions in code also compiled under `WIN32-Code`) is worth flagging even if it compiles and passes
   tests on the platform the PR was developed on.
6. **Thread-safety of anything touching `evthread`/lock-protected state.** Base-lock acquisition order,
   whether a callback can be invoked with a lock already held, and whether new shared state is protected
   consistently with the surrounding code's existing locking pattern.
7. **Public API/ABI stability.** Changes to headers under `include/` — signature changes, struct layout
   changes, removed/renamed public symbols — are a much bigger deal for a library this widely linked against
   than for an application; flag any such change explicitly even if internal-looking, and ask whether it was
   intentional and versioned appropriately.
8. **Build-system consistency.** libevent ships both autotools (`configure.ac`, `Makefile.am`) and CMake
   (`CMakeLists.txt`) build definitions in parallel. A new source file, test, or dependency added to one but
   not the other is a common class of drift to check for.

## What NOT to do

- Don't present items 4–8 as if they came from an observed review pattern on this fork — they didn't; there
  is no observed review pattern on this fork at all (see `references/mined-history.md`).
- Don't skip items 4–8 on the theory that "CI would catch it" — this skill has no confirmed accounting of
  what CI actually runs against a PR opened on this fork (none ever has), so treat these as your own reading,
  not a backstopped safety net.
- Don't manufacture a finding in every category to look thorough. If nothing concrete stands out, say so, or
  prefer `skip_comment: true`.
