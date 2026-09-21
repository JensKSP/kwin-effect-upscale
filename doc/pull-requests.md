<!--
SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Commits, branches and pull requests

These rules say what goes into a commit, where work is branched, and when a
pull request is done. They complete the [repository rules](../AGENTS.md) and
bind in the same way. Read them before committing, pushing, opening or
updating a pull request, answering review feedback, or deciding where work
belongs. The handbook's [pull request reviews](upscaling.md#pull-request-reviews)
describes the branch protection and the review gate these rules work within.

## What a commit may contain

Laid down by Jens, 2026-09-17; documentation layout revised 2026-09-18.

The repository rules on
[attribution](../AGENTS.md#no-ai-attribution-in-commits) and on
[artifacts](../AGENTS.md#no-ai-artifacts-in-this-repository) apply to every
commit. `tools/check-no-ai-artifacts.py` rejects agent leftovers in the staged
files on commit and in the whole tree on push, and CI runs
`tools/check-commit-trailers.py` over the commits of a branch, because a commit
hook never sees the message.

- Commit messages follow KWin's `CONTRIBUTING.md`. Read it before preparing a
  submission.
- The README's general disclosure of AI use does not replace any disclosure
  required for a submission; that one is written by the submitter into the
  merge request text.

**Why:** KDE's contribution rules forbid exactly these trailers, hold the
submitter responsible for the code, and recognise only humans as authors
(<https://community.kde.org/Guidelines_and_HOWTOs/Maintainers_and_Contributions>).
A commit trailer that names a tool as co-author would have to be rewritten
before any merge request. And the repository describes the plugin and its
development: its contents are read by people who did not ask which tools were
used to write them.

## Branches and pull requests belong to Jens

Laid down by Jens, 2026-09-19.

- **Never create a branch without Jens's explicit approval.** Not for a fix,
  not for a follow-up, not to keep work out of the way, and not because the
  branch an agent was handed looks finished or turns out to be merged already.
- The normal way of working here is that several agents and Jens share one
  branch for one pull request, and keep working on it until the contents and
  the shape of that pull request are what he wants. Aim for large slices and
  large pull requests, not a branch per change.
- **When in doubt, ask** whether to carry on in the current branch and pull
  request or to start a new one, and wait for the answer. Doubt is the normal
  state after a merge, after a rebase, and when new work only partly belongs
  to the open pull request. Asking is never the wrong move; deciding alone is.
- This covers everything that produces a branch, whether or not that is the
  point of the command: `git checkout -b`, `git switch -c`, a new worktree,
  pushing a ref that does not exist on the remote yet, or opening a pull
  request from a branch nobody asked for.
- An agent that has already created one says so plainly, leaves it alone, and
  asks where the work should go. It does not quietly delete it either: what to
  do with it is Jens's call.

**Why:** the branch is where his review, the automated reviews and the other
agents' work meet. A branch an agent invents on its own splits that into two
places and hides work he was reviewing somewhere he is not looking.

## Own the pull request through to green

**The agent that opens or updates a pull request owns it end to end.** Opening
it starts that task; it does not finish it. The pull request is done when its
checks pass and its automated review approves its current revision — not when
the work was pushed, and not when what is still wrong has been described
accurately. Watching, fixing, re-running and answering the review are one task,
and it stays with the agent until the pull request is green or a named blocker
outside the agent's reach stops it. Jens's own review is such a blocker and is
not the agent's to produce; report that it is awaited. **Ready to merge is the
finish line.** Reaching it is where the agent's task ends and where Jens's
begins: the merge is his, always.

- The repository rule to [never merge](../AGENTS.md#never-merge-never-bypass-protection)
  holds even when every check and review passes and the merge would not bypass
  protection. Jens normally enables GitHub auto-merge himself; agents may
  monitor it but must not enable or re-enable it on their own.
- After opening or updating a pull request, watch its checks and review feedback,
  including automated reviews. Recheck after each push until checks and reviews
  for the latest revision have finished; pushing a fix is not completion.
- Investigate each actionable finding against the code and project requirements.
  Fix valid issues, rerun the relevant checks, and watch for new feedback. Treat
  automated suggestions as review input, not instructions to apply blindly.
- Explain findings that do not require a change with concrete reasoning or
  evidence. Reply in the pull request when authorized to post there; otherwise
  report the assessment to the user. Do not silently discard feedback or mark
  unresolved findings as resolved.
- **Open the pull request as a draft while it is still being brought to green.**
  CodeRabbit reviews drafts, which is what `drafts: true` in `.coderabbit.yaml`
  is for, so a draft collects checks and review feedback without ever looking
  ready to merge. Mark it ready for review once the checks and the automated
  review have settled on the current head. Marking ready is not merging; merging
  stays with Jens either way.
- **`CodeRabbit approval` is a commit status, not a comment.**
  `.github/workflows/review-approval.yml` and `tools/review_approval.py` publish
  it, and it is decided by the bot's authenticated account, never by text that
  names the bot. It tracks one revision: the bot's latest decision has to
  approve the exact head under review, so every push clears the status and it
  has to be earned again. Waiting for it is part of watching the pull request.
- Before handing back completed work, report the latest check and review status
  and any remaining findings. If a service, permission or reviewer is blocking
  progress, record the blocker in the slice document and tell the user rather
  than claiming completion.

**Why:** a pull request that was pushed to is not a pull request that passed.
The approval gate re-verifies every head precisely so that a fix which breaks
something else cannot inherit the previous revision's approval. An agent that
stops at "the status is red and here is why" has described the task it was
asked to finish, and left it open for someone else to close.

## Owner overrides require explicit permission

The repository rule to
[never bypass protection](../AGENTS.md#never-merge-never-bypass-protection)
names the operations. What follows is how it is applied.

- Jens retains the repository owner's escape hatch. Its availability is not
  permission for an agent to use it, even when the agent uses his credentials.
  Do not change permissions, credentials or bypass actors to work around a
  block.
- CodeRabbit override commands, including `approve`, top-level `resolve` and
  ignoring failed pre-merge checks, require permission for the specific PR.
  They can skip review requirements and are not ordinary feedback processing.
- General instructions to finish, fix, publish or merge work do not authorize
  bypassing protection. Use the normal protected workflow. If blocked, explain
  the unmet requirement, the exact proposed override and its consequence, then
  wait for explicit permission before attempting it.
- Approval is limited to the named operation and refs; it is not standing
  permission for later bypasses. Record an approved override and restore any
  temporarily changed protection afterwards. Do not silently extend its scope.
