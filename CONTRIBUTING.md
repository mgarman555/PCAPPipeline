# Contributing to PCAP Pipeline

This repo has an unusual but deliberate workflow. Read this before you build or push — a couple of the conventions exist to stop sessions from stepping on each other.

## The big convention: Mac authors, Windows builds

- **Code is authored on macOS**, where the engine that's installed is **UE 5.4** — used only as an **API reference** (header lookups, signatures, patterns). The Mac **cannot compile** this project.
- **The project compiles and runs on Windows** against **Unreal Engine 5.8** (MSVC).
- Practical upshot: a change authored on the Mac is *not verified* until it builds on Windows. Author carefully against the real headers; treat "it compiles on Windows" as the acceptance gate.

So a typical change is: **author on Mac → push → pull & build on Windows → confirm → restart editor.**

## Build & run (Windows)

1. Right-click `PCAPPipeline.uproject` → **Generate Visual Studio project files**.
2. Build **Development Editor / Win64** (or just open the `.uproject` and let it compile the modules).
3. **Restart the editor after a fresh build** — the tool tabs register at module startup, so new/renamed tabs won't appear until a restart.
4. Find the tools under **Window ▸ Tools** (`PCAP Tools` and `Databases` groups).

## Git workflow

`main` is the branch the Windows machine builds from, so it must always compile. Work flows to `main` by **fast-forward** whenever possible.

### Always fetch before you push

Multiple autonomous sessions can race `origin/main`. **Only push when `origin/main` is an ancestor of your `HEAD`** (i.e. a clean fast-forward). The guarded push:

```bash
git fetch origin
if git merge-base --is-ancestor origin/main HEAD; then
    git push origin HEAD:main
else
    git rebase origin/main   # then re-run the check
fi
```

If the fast-forward check fails, **rebase onto `origin/main`** and try again — never force-push `main`.

### ⚠️ Sessions share ONE working tree

Parallel agent sessions operate on the **same working directory**. That means:

- **A branch checkout in one session switches the branch for *all* of them.** Don't `git checkout <other-branch>` casually mid-task — you'll yank the rug out from under a sibling session.
- **Commit by explicit path**, not `git add -A`, when another session may have unrelated modified files staged in the tree. Scope each commit to the files you actually changed.
- For genuinely isolated work, use a dedicated worktree under `.claude/worktrees/` instead of switching the shared tree.

### Commit messages

Conventional-commits style, scoped to the toolset:

```
feat(pcap): Call Sheet "+" creates new everywhere
fix(pcap): SeedNewShootDay slot-only ShotID
docs(pcap): consolidate design specs under docs/
```

## Asset & path conventions

- **Never hardcode `/Game/...` paths.** Every tool derives its content paths from `PCAPPaths` in [`PCAPToolPaths.h`](Plugins/PCAPTool/Source/PCAPTool/Public/PCAPToolPaths.h). All tool assets live under the single root `/Game/PCAPTool/`.
- **Folder/package names must be space-free** (UE's `INVALID_LONGPACKAGE_CHARACTERS` includes the space). Spaces live only in display text (e.g. the "PCAP Tools" menu group).
- The master database asset (`MasterPCAPDatabase`) is **auto-created on first tool open** — no manual setup.

## Binary assets & Git LFS

Not currently used, and **not needed yet**: the heaviest tracked files are the Vicon plugin's sample `.uasset` meshes (~45 MB total) and the whole `.git` is only ~29 MB — lean for a UE project. The Vicon SDK `.lib` is ~140 KB.

**Revisit LFS only if** `Content/` grows to hundreds of MB, or large binaries (textures, FBX, captures) start **changing often** (LFS helps most with churning binaries, not static ones). Adopting it then means `git lfs install` on *every* machine + a coordinated history migration — don't do it piecemeal.

## Documentation

- Design specs and implementation plans live in [`docs/`](docs/README.md) (see the index there). Each substantial feature is brainstormed into a dated `*-design.md` spec before it's built.
- Per-tool usage how-tos live in [`docs/tools/`](docs/tools/) and are linked from the [plugin README](Plugins/PCAPTool/README.md).

## License

**To be determined** — see the note in the [README](README.md#license). Until a license is chosen, treat the code as all-rights-reserved, and note that bundled third-party components (Unreal Engine, the Vicon DataStream SDK) retain their own licenses.
