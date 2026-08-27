---
name: libevent-maintainer-review
description: Review a redis-performance/libevent pull request, branch, or diff. This fork has NO independent PR/issue review history to mine at all — zero PRs have ever been opened against it, and issues are disabled — so this skill does NOT claim a mined "maintainer voice." Instead it grounds review in the one real artifact this fork does have (the project's own, unmodified CONTRIBUTING.md and checkpatch.sh) plus generic, clearly-labeled due-diligence checks appropriate to a low-level C networking library. Use this whenever asked to review a libevent PR "like a maintainer would," or wants a libevent-specific pre-merge check. Prefer this over a generic code-review skill for redis-performance/libevent so at least the honest state of this fork's review history is surfaced up front — but do not expect it to produce a "how the real reviewers here talk" style review, because no such reviewers are recorded.
---

# libevent maintainer-style review

## Honesty note — read this first

This skill is adapted from equivalent ones built for other redis-performance forks (e.g.
`redis/memtier_benchmark`, with ~4 years of real dialogic maintainer review comments, and
`redis-performance/go-ycsb`, with 27 PRs from a single author and 5 rubber-stamp approvals). Both of those
had *something* to mine. **`redis-performance/libevent` has nothing comparable — it has less recorded review
activity than either.** As mined on 2026-08-27:

- **Zero pull requests have ever been opened against `redis-performance/libevent`**, confirmed three ways:
  `gh pr list --repo redis-performance/libevent --state all` returns an empty list, the raw
  `GET /repos/redis-performance/libevent/pulls?state=all` API returns `[]`, and
  `search/issues?q=repo:redis-performance/libevent+is:pr` reports `total_count: 0`. There is no PR review
  comment, no approval, no requested-changes review, no PR-level discussion of any kind on this fork, ever.
- **Issues are disabled** on this repository entirely (`gh issue list` errors with "the repository has
  disabled issues"). There is no issue history either, and nothing for `claude-issue-triage.yml` to trigger
  on today — it is included alongside `claude-pr-review.yml` for parity with the org's standard automation
  set, and in case issues are ever enabled, exactly as the go-ycsb precedent did for its own (merely inactive,
  not disabled) issue tracker.
- This repo is a **fork of the upstream `libevent/libevent` project** (description: "Event notification
  library"; `isFork: true`, `parent: libevent/libevent`). `master` currently mirrors upstream: every commit on
  it in the visible history is authored by upstream contributors (Azat Khuzhin, Nick Mathewson,
  dependabot[bot], etc.) plus upstream's own merge commits. `CONTRIBUTING.md` and `README.md` at the
  repo root are **upstream's own files, unmodified** — they describe upstream's `checkpatch.sh` coding-style
  script, `test/regress_*.c` conventions, and `make verify`, but say nothing specific to this fork. There is
  no `AGENTS.md` in this repository (a 404 on `GET /repos/redis-performance/libevent/contents/AGENTS.md`).
- Real redis-performance engineering activity clearly **does** happen in this repository — topic branches
  such as `io-uring-read-timeout`, `tls-bufferevent-bench`, `upstreamable`, `ssl-timing-counters`,
  `pr-skip-time-cache`, and `night/c4`/`night/c4a` contain substantive commits (io_uring multishot recv,
  bufferevent throughput benchmarking, read-timeout enforcement, etc.) authored by identifiable people
  (Dmitry Ilyin, Filipe Oliveira (Redis), and an automated `libevent-perf` committer). But **none of that work
  has ever gone through this fork's own PR mechanism** — it lives and is presumably reviewed on branches, or
  pushed directly, or is destined for upstream's own PR process (see the `upstreamable` branch name) rather
  than a PR opened against `redis-performance/libevent` itself. That means even this real activity yields
  zero recorded review comments to mine for a "voice."

**Conclusion: there is no maintainer voice to imitate here, not a thin one — none.** Do not invent reviewer
dialogue, quotes, nitpick patterns, or a personality attributed to any named person. Do not claim a review
comment class has "come up before" on this repo, because none has. What this skill can honestly offer is: (1)
this fork's one real, if borrowed, written convention document (`CONTRIBUTING.md`/`checkpatch.sh`), and (2)
generic, explicitly-labeled due-diligence appropriate to reviewing changes to a widely-embedded, portable C
networking library — flagged as generic engineering judgment, not distilled from this fork's own precedent.
See `references/mined-history.md` for the full accounting above with the exact commands run, and
`references/review-checklist.md` for the checklist itself.

## Process

1. **Get the material.** `gh pr view <n> --repo redis-performance/libevent --json body,commits,files,author`
   and `gh pr diff <n> --repo redis-performance/libevent`. Read the description in full. Since there is no
   fork-specific norm for what a "good" PR description looks like here (zero precedent), judge it on its own
   merits — does it explain the change and how it was tested — rather than against some observed local norm.

2. **Check the one real, if inherited, written convention this repo has**: run `./checkpatch.sh -r HEAD` (or
   `git format-patch --stdout -1 | ./checkpatch.sh -p` for a specific commit) if you have the code checked
   out, or at minimum read `CONTRIBUTING.md` and mentally apply its coding-style guidance. If the PR adds or
   changes behavior, check whether it added a test under `test/regress_*.c`, per `CONTRIBUTING.md`'s own
   "Testing" section — that's a real, current stated requirement, not a mined pattern, even though no
   recorded review has ever enforced it on this fork.

3. **Work the checklist** in `references/review-checklist.md`. Every item there is labeled by its actual
   grounding: "from this fork's own CONTRIBUTING.md" (real, current, but self-applied/unenforced-by-review) vs.
   "generic due diligence for a C networking library" (not from this fork's history at all — say so if you
   raise it). Don't blur that distinction in the review you write.

4. **Because there is no CI-signal accounting specific to this fork** (the upstream `.github/workflows/`
   — `build.yml`, `cifuzz.yml`, `master.yml`, `scorecard.yml` — run on upstream's schedule and triggers, not
   verified here to run identically on this fork's own PRs), don't assume "CI will catch it." Treat memory
   safety and portability issues in touched code as in scope for your own reading, same as you would for any
   unreviewed C change.

5. **Write the review terse and mostly as questions.** With no behavioral data point to match (unlike
   go-ycsb's "silent unless something concrete stands out" pattern, which was at least observed), default to:
   comment only on concrete, specific findings; if the PR is routine and nothing concrete stands out, prefer
   `skip_comment: true` over manufacturing generic C-review boilerplate to look thorough.

6. **Land on a plain-prose verdict.** No literal "Verdict:" label, no bolded summary line, no `@`-mention of
   any GitHub username, and open with the automated marker the workflow already provides — these apply
   regardless of mined history; see the workflow's own critical safety rules for why.

## What NOT to do

- Don't claim a mined "maintainer voice," a reviewer's supposed pattern, or that some issue class has "come
  up before" on this fork — none of that is in the data. See the honesty note above.
- Don't treat the topic branches (`io-uring-read-timeout`, `tls-bufferevent-bench`, etc.) as a source of
  reviewed precedent — real engineering work happened there, but it was never reviewed through this fork's PR
  mechanism, so it establishes no observed review standard, only that the code itself once existed.
- Don't cite `CONTRIBUTING.md` as though a reviewer has enforced it on this fork before — it's upstream's
  document, inherited, current, and worth citing on its own merits, but never as proven local precedent.
- Don't manufacture a duplicate-approval comment ("LGTM") — with no observed norm either way, silence on a
  clean PR is the more honest default than fabricated affirmation.
- Don't literally `@`-mention any GitHub username, ever, for any reason.
