# Mined history — full accounting

Mined 2026-08-27 against `redis-performance/libevent`. Every claim below is reproducible with the exact
command shown; nothing here is inferred or extrapolated beyond what the command returned.

## PRs: zero, confirmed three independent ways

```
$ gh pr list --repo redis-performance/libevent --state all --limit 100 --json number,title,author,state
[]

$ gh api "repos/redis-performance/libevent/pulls?state=all&per_page=100" -q 'length'
0

$ gh api "search/issues?q=repo:redis-performance/libevent+is:pr" -q '.total_count'
0
```

No PR has ever been opened against this fork — not merged, not closed, not open, not draft. There is
therefore no PR description style, no review comment, no approval, no requested-changes review, and no
review-thread back-and-forth to learn a voice from.

## Issues: disabled

```
$ gh issue list --repo redis-performance/libevent --state all --limit 100
the 'redis-performance/libevent' repository has disabled issues
```

Issues are turned off at the repository-settings level, not merely empty. `claude-issue-triage.yml` is added
for parity with the org's standard automation set and in case issues are enabled later; today it has nothing
to trigger on.

## This is a fork, and `master` mirrors upstream

```
$ gh repo view redis-performance/libevent --json defaultBranchRef,description,isFork,parent
{"defaultBranchRef":{"name":"master"},"description":"Event notification library","isFork":true,
 "parent":{"name":"libevent","owner":{"login":"libevent"}}}
```

The 20 most recent commits on `master` (`gh api "repos/redis-performance/libevent/commits?sha=master"`) are
all authored by upstream contributors — Azat Khuzhin, Nick Mathewson, dependabot[bot] — continuing straight
through with no redis-performance-authored commit and no divergence from what a plain mirror of upstream
`master` would show.

## CONTRIBUTING.md / README.md are upstream's own, unmodified

`CONTRIBUTING.md` at the repo root documents `checkpatch.sh` for style checking, `test/regress_*.c` for new
unit tests, `make verify`, and points questions at libevent's own Gitter room — all upstream conventions, with
no redis-performance-specific addition anywhere in the file. `README.md` is upstream's standard build/install
doc. There is no `AGENTS.md`:

```
$ gh api repos/redis-performance/libevent/contents/AGENTS.md
{"message":"Not Found", ... "status":"404"}
```

## Real engineering activity exists — but not through this fork's PR mechanism

Topic branches on this fork carry substantive, identifiable work:

- `io-uring-read-timeout` — io_uring multishot recv, provided buffer rings, CQE callback locking, a bufferevent
  throughput benchmark; commits authored by "Dmitry Ilyin" and an automated "libevent-perf" committer.
- `tls-bufferevent-bench` — merges upstream `master` forward and layers TLS/bufferevent benchmarking on top;
  a merge commit here is authored by "Filipe Oliveira (Redis)".
- `upstreamable`, `ssl-timing-counters`, `pr-skip-time-cache`, `pr/529-reproducer`, `night/c4`, `night/c4a` —
  further topic branches, names suggesting work staged either for eventual upstream submission or internal
  benchmarking/reproduction use.

This confirms redis-performance engineers actively work in this repository. It does **not** provide any
review history: none of these branches has ever been opened as a PR against `redis-performance/libevent`
(same zero-PR count above covers all branches, not just `master`), so whatever review this work received (if
any) happened somewhere this mining can't see — a different fork, a different tool, informally, or not at
all. Do not treat commits on these branches as reviewed, or their commit messages as maintainer-approved
style, just because the code is substantive.

## What this means for the skill

There is strictly less to work with here than the `go-ycsb` precedent (27 PRs, one author, 5 empty-body
rubber-stamp approvals from a second person — thin, but real). This fork has no PR-shaped signal at all.
Anchor review guidance in the one artifact that is both real and current — the inherited `CONTRIBUTING.md` —
and otherwise fall back to generic, clearly-labeled C-library due diligence rather than anything claiming to
reflect this fork's own reviewers.
