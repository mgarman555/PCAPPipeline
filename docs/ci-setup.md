# CI — setup & operations

Every push now gets a real compile. This page is how you stand that up, how you read it when it goes red, and how you turn it off.

Until now the acceptance gate was manual: author away from the engine → push → pull on the Windows box → build in Visual Studio → confirm. That round trip is the thing CI replaces. `main` is the branch the Windows machine builds from and it must always compile ([CONTRIBUTING](../CONTRIBUTING.md#git-workflow)) — CI is what makes that claim checkable instead of remembered.

---

## What runs, and when

| Workflow | File | Runs on | Triggers | Gates |
|---|---|---|---|---|
| **host-tests** | [`.github/workflows/host-tests.yml`](../.github/workflows/host-tests.yml) | GitHub-hosted `ubuntu-24.04` (free) | every branch push, every PR **including forks**, manual | The VCam numeric core — `FVCamInputLayer` · `FPCAPVCamProcessor` · `FVCamCurveSmoothing` |
| **Windows UE 5.8 build** | [`.github/workflows/windows-build.yml`](../.github/workflows/windows-build.yml) | **your Windows box**, self-hosted | push to `main` and `claude/**`, manual, weekly Mon 13:00 UTC | `PCAPPipelineEditor Win64 Development` — the real compile |

**host-tests** builds `Plugins/PCAPTool/HostTests/run.sh` — the real shipping translation units against stubs, with plain clang++ and g++. No engine, no Windows, no Vicon SDK. ~2.5 s of work, so `main` stays verifiable even when the build box is powered off. Two blocking legs (clang++ and g++ on `ubuntu-24.04`) plus a non-blocking `ubuntu-latest` canary that warns you before the image migration breaks the pinned legs.

**Windows UE 5.8 build** runs the same command your Visual Studio build runs, minus the Visual Studio:

```text
Build.bat PCAPPipelineEditor Win64 Development -Project="<workspace>\PCAPPipeline.uproject" -WaitMutex -architecture=x64 -Log="<workspace>\Saved\Logs\UBT-CI.log"
```

`-FromMsBuild` is dropped — it only formats UBT's output for the VS Error List, and nothing in Actions is MSBuild. `-WaitMutex` is kept: UnrealBuildTool's single-instance mutex is scoped to the *engine install*, so a CI build and you hitting F7 in Visual Studio contend for it. Without `-WaitMutex` CI would just go red because you were building.

### Branch protection

Make these required status checks on `main`. A status check is named after the **job**, so search the branch-protection picker for these:

| Search for | Shown in the UI as | Why that job |
|---|---|---|
| `gate` | `host-tests / gate` | The matrix leg names change whenever the matrix does; `gate` never changes. It aggregates the blocking legs and ignores the canary. |
| `PCAPPipelineEditor Win64 Development` | `Windows UE 5.8 build / PCAPPipelineEditor…` | The real compile. Only add this once the runner is up — a required check with no runner blocks every merge. |

The picker only lists checks it has seen run recently, so let each workflow run once before configuring protection.

> **⚠️ Before you make the Windows build a required check, read this.**
>
> That workflow triggers on **`push` to `main` and `claude/**` only** — it has no `pull_request` trigger, deliberately (see [Security](#security) for why fork PRs must not run on a self-hosted runner).
>
> So a PR from a branch named anything else **never produces that check**, and requiring it makes such a PR **unmergeable forever**, stuck on *"Expected — waiting for status"* with no way out but an admin override or editing branch protection.
>
> Pick one:
> - **Keep branch names under `claude/**`** (what this repo already does) — the check runs on the push and attaches to the PR head. This is the intended setup.
> - **Require only `host-tests / gate`**, which runs on `pull_request` from any branch, and leave the Windows build as an informational check.
> - **Widen the trigger** in `windows-build.yml` to include your other branch prefixes — but never to `pull_request` from forks.

## What CI catches — and what it doesn't

Be precise about this, because a green check that means less than you think is worse than no check.

| | host-tests | Windows build |
|---|---|---|
| VCam logic regressions | ✅ | ✅ |
| Compile errors in PCAPTool | partial (5 files) | ✅ all ~47 |
| **Unity-build breaks** (the `C4459 'Dt'` class) | ❌ structurally impossible | ✅ — and better than your box |
| **UBT target-config rejection** (the V6/V7 class) | ❌ fires before any compiler | ✅ |
| Vicon SDK code paths | ❌ | ✅ (`WITH_VICON_SDK=1`) |
| The editor actually opening | ❌ | ❌ — it compiles, it doesn't launch |
| Vicon hardware working | ❌ | ❌ — the `.dll` is delay-loaded and isn't in the repo |

The unity row is the interesting one. UBT decides which files to pull *out* of the unity blob by running `git status` — dirty files get compiled standalone. Your working tree is usually dirty, so files you're actively editing escape unity batching and cross-file collisions stay invisible. A CI checkout is clean, so **everything** goes into unity. That's exactly the condition that produced the recorded `C4459 declaration of 'Dt' hides global declaration` break. Expect CI to catch things that build fine for you locally. That is the system working.

---

## Set up the Windows runner

One time, on the build box, in an **elevated PowerShell**. Do steps 4–6 in one sitting — the registration token expires after 60 minutes.

### 1. Create a dedicated service account

```powershell
$pw = Read-Host -AsSecureString 'Password for ghrunner'
New-LocalUser -Name ghrunner -Password $pw -PasswordNeverExpires -AccountNeverExpires `
              -Description 'GitHub Actions runner service'
```

**Do not add it to Administrators. Do not run the runner as `mocapstaff`.** A workflow run gets whatever the runner account has — running as `mocapstaff` would hand every CI job your Git credential-manager token, SSH keys, browser cookies and Vicon licences. `ghrunner` gets its own profile, which also means its own warm DerivedDataCache across runs.

Confirm afterwards that `ghrunner` can read `C:\Program Files\Epic Games\UE_5.8` and the Visual Studio install (both are under Program Files, readable by `Users` by default), and that **`git.exe` is on the SYSTEM PATH** — UBT shells out to `git status` and preflight checks the tree is clean.

### 2. Enable long paths, before the first build

```powershell
New-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' `
                 -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force
git config --system core.longpaths true
```

### 3. Pick a short runner root

Use `C:\ghr`, not GitHub's suggested `C:\actions-runner`. The workspace ends up at `C:\ghr\_work\PCAPPipeline\PCAPPipeline` — 38 characters, i.e. no deeper than the `C:\Users\mocapstaff\Desktop\PCAPPipeline` tree that already builds. `C:\actions-runner` costs you 11 more characters on every `Intermediate\Build\Win64\x64\...` path. Keep it at the drive root so system accounts can traverse it.

### 4. Get the registration token

**GitHub ▸ your repo ▸ Settings ▸ Actions ▸ Runners ▸ New self-hosted runner ▸ Windows / x64.**

That page shows the current runner version, its checksum, and a `./config.cmd` line with a token embedded. Copy the token from there — it is generated fresh each time and **expires in one hour**. Never commit it, never paste it into a file, never put it in a repo secret.

Scriptable equivalent if you have the `gh` CLI authenticated:

```powershell
gh api --method POST /repos/mgarman555/PCAPPipeline/actions/runners/registration-token --jq .token
```

### 5. Download and verify

Take the version number and SHA256 from that same **New self-hosted runner** page — they change with each release, and the page always shows the current pair.

```powershell
mkdir C:\ghr; Set-Location C:\ghr
$ver  = '2.336.0'   # from the New self-hosted runner page
$hash = 'D59123A43003E357B0805B5D0F611D0BD2F65AB67D51BD070DD4E7A0F685C162'  # ditto
$zip  = "actions-runner-win-x64-$ver.zip"
Invoke-WebRequest -Uri "https://github.com/actions/runner/releases/download/v$ver/$zip" -OutFile $zip
if ((Get-FileHash -Path $zip -Algorithm SHA256).Hash.ToUpper() -ne $hash.ToUpper()) {
    throw 'Checksum mismatch — do not extract this file'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::ExtractToDirectory("$PWD\$zip", "$PWD")
```

Don't skip the hash check.

### 6. Configure as a service

```powershell
.\config.cmd --url https://github.com/mgarman555/PCAPPipeline `
             --token <PASTE_REGISTRATION_TOKEN> `
             --name pcap-win-ue58 `
             --labels ue-5.8,vicon-sdk `
             --work _work `
             --runasservice `
             --windowslogonaccount ".\ghrunner" `
             --windowslogonpassword "<the password from step 1>" `
             --replace --unattended
```

Flag notes, all of which bite if you improvise:

| Flag | Why |
|---|---|
| `--labels ue-5.8,vicon-sdk` | **Adds** to the automatic `self-hosted` / `Windows` / `X64`. `ue-5.8` is what `runs-on` matches on. `vicon-sdk` records that this box has the SDK — a runner without it produces a materially different binary. |
| `--runasservice` | Service install is only offered *at config time*. Configure without it and you must remove and re-add the runner to convert. |
| `--windowslogonaccount` | Without it the service runs as `NETWORK SERVICE`, which gets a different profile: cold DDC, and UBT logs buried in `C:\Windows\ServiceProfiles\`. |
| `--replace` | Makes re-running this command idempotent. |
| *not* `--no-default-labels` | It would strip `self-hosted` / `Windows` and nothing would ever match. Note this is the **only** hyphenated flag — `--runasservice` and `--disableupdate` are not. Don't "correct" them. |
| *not* `--disableupdate` | With auto-update off you have 30 days before GitHub refuses to queue jobs to the runner. |
| *not* `--ephemeral` | It deregisters after one job and needs a re-registration loop to maintain. See the security section for the tradeoff being accepted here. |
| *not* `--runnergroup` | Org-level only. Invalid for a repo runner on a personal account. |

### 7. Verify

```powershell
Get-Service 'actions.runner.*' | Format-List Name,Status,StartType   # Running / Automatic
```

Then in **Settings ▸ Actions ▸ Runners**, `pcap-win-ue58` should show **Idle** with exactly these labels: `self-hosted`, `Windows`, `X64`, `ue-5.8`, `vicon-sdk`. Read them character by character against `runs-on: [self-hosted, windows, 'ue-5.8']` in the workflow. `runs-on` is AND across every label and a typo does **not** error — the job just queues for 24 hours and then fails with nothing useful.

If the service won't start, add `ghrunner` to **secpol.msc ▸ Local Policies ▸ User Rights Assignment ▸ Log on as a service**. `config.cmd` normally grants that itself.

### 8. Firewall the UBA listener

Every build binds `0.0.0.0:1345` — Unreal Build Accelerator's work-distribution port, unauthenticated, on all interfaces. That's a real listener on your capture LAN for the duration of each job. Block it inbound; local builds use loopback, which firewall rules don't touch, so UBA keeps making the 32-core box fast:

```powershell
New-NetFirewallRule -DisplayName 'Block inbound UBA 1345' -Direction Inbound `
                    -LocalPort 1345 -Protocol TCP -Action Block
```

### Removing or re-registering

Needed at every engine upgrade (the `ue-5.8` label can't be edited by `config.cmd` — only via the UI gear icon or a re-register).

```powershell
Set-Location C:\ghr
.\config.cmd remove --token <REMOVAL_TOKEN>   # Settings ▸ Actions ▸ Runners ▸ the runner ▸ Remove
```

`remove` accepts only `--token`, `--pat` or `--local`. It uninstalls the service too. If the box is dead, use **Force remove this runner** in the UI; if the GitHub side is already gone but the local config remains, `.\config.cmd remove --local` lets you re-register without re-downloading.

---

## Security posture

**The repository is public today.** That is the single fact that makes this setup risky, and it's worth being blunt about why.

### The fork-PR problem

On a `pull_request` from a fork, **the workflow YAML that runs is the one from the PR head, not from `main`**. An attacker doesn't need to abuse a workflow you wrote — they add their own file with `runs-on: [self-hosted, Windows, X64]` and it gets scheduled onto your box. Every `if:` guard, `environment:` gate or job-level restriction you could write in your own YAML is attacker-removable on that trigger.

The usual reassurance — "fork PRs don't get secrets and the token is read-only" — is true and irrelevant. That protects the *repository*. Their code still executes on *your machine*, as the runner account. GitHub's own wording: self-hosted runners "do not have guarantees around running in ephemeral clean virtual machines, and can be persistently compromised by untrusted code in a workflow", and "should almost never be used for public repositories".

Blast radius here is not a disposable VM. It's the mocap operator's production workstation: reach into the `mocapstaff` profile, the LAN the Vicon hardware sits on, and write access to `C:\Program Files\Epic Games\UE_5.8` — where dropping a wrapper DLL backdoors every future build silently. Wiping a cloud VM is cheap; re-imaging a calibrated capture workstation is not.

On a public repo the **only** thing between an anonymous GitHub user and code execution on that box is the fork-approval click. And that control fails in the normal case, not the exceptional one: maintainers click "Approve and run" routinely, just to see whether a contributor's PR builds.

### What the design does about it

| Control | What it does |
|---|---|
| **Make the repo private** | Deletes the anonymous-fork attack class outright rather than mitigating it. Costs nothing today: 0 stars, 0 forks, 1 collaborator, license explicitly undecided, and a vendored third-party Vicon SDK binary that shouldn't be public anyway. **Do this first.** Everything below is harm reduction for the case where you don't. |
| **Windows workflow has no `pull_request` trigger** | `push` / `schedule` / `workflow_dispatch` only. A `push` event can only be produced by someone with write access — that's a structural boundary, not a guard, so it survives attacker-authored YAML. |
| **`pull_request_target` is banned** | It runs the base branch's workflow with full secrets and a write token. A compile check has zero need for it. |
| **Fork contributors get host-tests instead** | Real compile-and-run signal on real shipping translation units, on a disposable GitHub-hosted VM. That's what makes the strict Windows policy affordable rather than obstructive. |
| **`permissions: contents: read`** | Unlisted scopes become `none`. The build only needs to clone. Never grant `id-token` to a self-hosted job — that turns a runner compromise into a cloud-identity compromise. |
| **`persist-credentials: false`** | Otherwise `actions/checkout` writes the job token into `.git/config`, in a workspace that outlives the job on your machine. |
| **Actions pinned to full commit SHAs** | A tag is mutable. A SHA isn't. |
| **No Actions secrets in this repo** | The build needs none — engine is local, Vicon `.lib` is committed. Keep it that way; any secret added is one `printenv` away from a compromised job. |

### Repo settings to apply

**Settings ▸ Actions ▸ General:**

- **Actions permissions** → *Allow `mgarman555`, and select non-`mgarman555`, actions* — allowlist explicitly. Not "Allow all actions".

  **You must then allow the actions the workflows use, or nothing will run.** Selecting that radio and stopping leaves the allowlist empty, and every run dies on its first step with `actions/checkout@<sha> is not allowed in mgarman555/PCAPPipeline` — a correctly-registered runner and zero working builds. Tick **Allow actions created by GitHub** (this covers `actions/checkout` and `actions/upload-artifact`, the only third-party actions either workflow uses). Alternatively, paste these into the allowlist box instead:

  ```
  actions/checkout@*,
  actions/upload-artifact@*
  ```
- **Fork pull request workflows** → **Require approval for all external contributors** (the default is only "first-time contributors").
- **Workflow permissions** → *Read repository contents and packages permissions*, and **uncheck** "Allow GitHub Actions to create and approve pull requests".

**Settings ▸ Secrets and variables ▸ Actions:** confirm the secrets list is empty.

### `.Build.cs` and `.Target.cs` are privileged code

UnrealBuildTool compiles and executes your rules files as **arbitrary C#** before any C++ is built — and these files already do filesystem traversal outside their own plugin and directory enumeration. Treat a `.Build.cs` or `.Target.cs` diff as a privileged code change in review, not as build config.

### Accepted risks, recorded deliberately

- **The runner is persistent, not ephemeral.** `Binaries/`, `Intermediate/`, `Saved/` and the UBA cache are never wiped between jobs, because wiping them means a cold rebuild every push. The same mechanism that makes builds fast makes cleanup impossible: one successful compromise persists.
- **`UE_ROOT` is a repository *variable*, not a secret** — deliberately. It's a path, not a credential, and it's already in this repo in three places. Making it a secret would make GitHub redact it from logs, and the engine root appears in nearly every UBT line — you'd get `***\Engine\Source\...` in exactly the lines you need to read. The tradeoff: anyone with repo write access can point CI at another directory and the runner will execute `<that path>\Engine\Build\BatchFiles\Build.bat`. Preflight narrows this (the candidate must parse as a real `Build.version` at the right major.minor and carry both Performance Capture plugins) but does not eliminate it.
- **Runner groups aren't available** on a personal account, so repository-scoped registration is the only isolation boundary. If the runner fleet ever grows past one, move the repo into an organization.

---

## Reading a failed build

Work in this order.

**1. The job summary.** Actions ▸ the run ▸ the job. The summary is written first and is the most useful surface — it has no annotation cap and it carries the explainers. You get:

- A **Preflight** table: resolved engine root, `Build.version`, EngineAssociation, Visual Studio + MSVC toolset, Windows SDK, workspace path, checkout mode (clean vs incremental), and which account the runner ran as. This is the audit trail the manual round trip never had — it answers "which engine actually compiled this commit".
- A **Build** table: result, target, engine, commit, elapsed.
- A deduplicated error list with repo-relative `file:line:col`.
- Callout explainers for the two known failure classes (below).
- The permanent `[Upgrade]` include-order block, collapsed. **That block is expected noise on every single build**, green or red — `IncludeOrderVersion` is deliberately pinned to `Unreal5_7`. Ignore it.

**2. Inline annotations** on the Files changed tab. Capped at **10 errors + 10 warnings + 10 notices per step**, and GitHub gives no indication when it truncates — so a broad unity break shows you 10, you fix 10, push, and get 10 more. The summary's `… N more` footer is the real count.

Two annotation quirks worth knowing:

- MSVC `note:` lines point at a **different file** than the error. For the `C4459` case the error lands on `VCamInputLayer.cpp` but the fix belongs in the test file that declares the shadowing symbol. Notes are routed to `notice` severity so they get their own budget and never displace real errors.
- Annotations are spoofable by anyone who can get chosen text to the start of a line in compiler output. They are a convenience, not the record. **The step exit code and the uploaded log are the record.**

**3. The `ue-build-logs-<run_id>-<run_attempt>` artifact.** Uploaded on failure only, kept **7 days**. Contains the full UBT log and the UBA trace, with the workspace path prefix scrubbed. If both the workflow's `-Log=` output and `%LOCALAPPDATA%\UnrealBuildTool\Log.txt` were missing you'll see a `No UBT log` warning instead — that means `-Log=` isn't doing what's expected on this UBT and wants a look.

**On exit codes:** any non-zero is a failure. `6` is `OtherCompilationError` and is what both recorded failures produced — note it is **not** `1`, so never test for equality with 1.

---

## Forcing a clean rebuild

The Windows job checks out with `clean: false` on purpose. `actions/checkout`'s default runs `git clean -ffdx`, and `-x` deletes ignored files — which in this repo is `Binaries/`, `Intermediate/`, `Saved/` and `DerivedDataCache/` at the root **and inside every plugin**. That's every byte of incremental state, gone on every push.

It's safe because checkout still runs `git checkout --force`, so every tracked file is forced to match the commit. Only gitignored build output survives, and ignored paths never show in `git status`, so keeping them warm can't perturb UBT's adaptive-unity working set.

Three ways to get a cold build:

| Method | How |
|---|---|
| **Manual** | Actions ▸ *Windows UE 5.8 build* ▸ **Run workflow** ▸ tick `clean_build`. |
| **Per-commit** | Put `[ci clean]` anywhere in the commit message. |
| **Automatic** | The weekly canary, Mondays 13:00 UTC, always takes the clean path. |

That weekly canary is not optional hygiene — incremental state can hide real breaks indefinitely (a stale `.generated.h` from a deleted `UCLASS`, a stale `.obj`, a stale plugin DLL). It bounds how long a false green can survive to one week. It assumes the box is powered on overnight; if it isn't, move the cron to a working-hours slot.

If you need to reclaim disk by hand on the box, delete `<workspace>\Intermediate`, `<workspace>\Plugins\*\Intermediate`, `<workspace>\Binaries` and `%LOCALAPPDATA%\UnrealBuildTool`. Never let anything outside `GITHUB_WORKSPACE` get cleaned — the engine install under Program Files is shared machine state you use interactively.

---

## Turning CI off

| Scope | How | Watch out |
|---|---|---|
| **One commit** | Put `[skip ci]` in the commit message | Skips both workflows. Also skips the check, so a required check stays unresolved on a PR. |
| **One workflow, indefinitely** | Actions ▸ pick the workflow ▸ `⋯` ▸ **Disable workflow** | A disabled workflow reports **no status at all**. If it's a required check, remove it from branch protection in the same sitting or PRs block forever on "Expected — waiting for status". |
| **All Actions** | Settings ▸ Actions ▸ General ▸ **Disable actions** | Same required-check caveat, doubled. |
| **Just the Windows box** (shoot day, machine needed) | `Stop-Service 'actions.runner.*'` on the box | Jobs **queue** rather than fail — for up to 24 hours, then they fail. Restart with `Start-Service 'actions.runner.*'` and the queue drains. This is the right lever when you need the 32 cores for capture work, because it leaves `main`'s history of results intact. |

Removing the `schedule:` block from `windows-build.yml` is the right move if the box is routinely off overnight — it stops a weekly red build that means nothing.

---

## Troubleshooting

Preflight collects **every** problem before exiting, so a cold runner doesn't cost you four round trips. Each item is tagged with an ID; find the ID here.

| Symptom / preflight ID | Cause | Fix |
|---|---|---|
| **`[engine-root]`** — "Unreal Engine 5.8 was not found on this runner" | Preflight tried, in order: `vars.UE_ROOT` → `$env:UE_ROOT` → `HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8` (both registry views) → `LauncherInstalled.dat` → the per-app `*.item` manifests → `HKCU` Builds (source builds only) → globbing `Program Files\Epic Games\UE_*`. All missed. The HKLM key is commonly absent when the engine was installed non-elevated. | Re-run the Epic Launcher **as Administrator** and use **Verify** (that's what writes the key). Or set the repository variable: `gh variable set UE_ROOT --body "C:\Program Files\Epic Games\UE_5.8"`. Or set a machine-level `UE_ROOT` and restart the runner service. |
| **`[engine-override]`** | `UE_ROOT` is set but doesn't point at a usable engine. An explicit override is never allowed to fall through to autodetection — a typo must be loud, not silently masked by a lucky registry hit. | Correct the value, or clear it to fall back to autodetection. |
| **`[engine-version]` mismatch** | The engine reports a different `Engine\Build\Build.version` than `PCAPPipeline.uproject`'s `EngineAssociation`. Usually a stale registry key or a hand-edited `LauncherInstalled.dat` pointing at a leftover 5.7 tree that still has a perfectly valid `Build.bat`. | Point `UE_ROOT` at the right install. Only bump `EngineAssociation` deliberately — and re-check both `Target.cs` pins in the same commit. |
| **`[engine-install]`** | `UnrealBuildTool.dll` or the bundled DotNet is missing. A partial/interrupted install. | Epic Launcher ▸ Library ▸ UE 5.8 ▸ dropdown ▸ **Verify**. |
| **`[plugin-core]` / `[plugin-workflow]`** — plugin missing | Performance Capture Core or Workflow isn't installed in this engine. Launcher installs let you deselect plugin sets. `PCAPTool.Build.cs` hard-depends on both and defines `WITH_PCAP_WORKFLOW=1` unconditionally, so absence is a hard failure, not a degradation. | Epic Launcher ▸ UE 5.8 ▸ **Verify**, or install the Performance Capture plugins. To go Core-only, all four move together: `WITH_PCAP_WORKFLOW=0`, drop `PerformanceCaptureWorkflowRuntime` from `PrivateDependencyModuleNames`, and remove the plugin entry from both `PCAPPipeline.uproject` and `PCAPTool.uplugin`. |
| **`[plugin-workflow-module]`** | The Workflow plugin **is** installed but doesn't export `PerformanceCaptureWorkflowRuntime`. Note the plugin name and the module name differ by design — this is the trap. | If the engine renamed the module, update `PCAPTool.Build.cs` **and** the `/Script/...` paths in `PCAPStageBridge.cpp`. Otherwise go Core-only as above. |
| **`[plugin-core-path]` / `[plugin-workflow-path]`** *(warning only)* | Plugin found, but not in the folder the docs claim. UBT discovers plugins by scanning the whole tree, so the build is fine. | Update `README.md` / `Reference/README.md`. Nothing to fix in the build. |
| **`[vicon-sdk]`** *(warning)* | `ViconDataStreamSDK_CPP.lib` is missing, so `WITH_VICON_SDK=0`. **The build still goes green** — but `PCAPViconSDKMarkerSource.cpp` and the Volume Visualizer's raw marker cloud compile out to no-ops. A green build with a dead feature. | The `.lib` is tracked in git (~140 KB). If it's gone, the checkout is wrong — check sparse-checkout / LFS / submodule config. |
| **`[vicon-sdk-corrupt]`** | The `.lib` exists but doesn't start with `!<arch>` — an LFS pointer, or eol-mangled. `PCAPTool.Build.cs` only calls `File.Exists()`, so it sets `WITH_VICON_SDK=1` and you get a LINK error at the very end of a full build. | Re-checkout with `core.autocrlf=false`; confirm `.gitattributes` (`* text=auto`) isn't converting `*.lib`. |
| **`error C4459: declaration of 'X' hides global declaration`** | A **unity-build-only** break — two files that only see each other because UBT batched ~47 `.cpp` into one `Module.PCAPTool.cpp`. It does not reproduce with a per-file build. The `note:` line names the *other* file, which is where the fix goes. | **Rename the symbol.** The repo's own precedent: commit `84e6dfb` renamed the test-side `Dt` to `kDt` rather than suppressing the warning. **Do not** add a `WarningLevel` override to a `Target.cs` — see the next row for exactly why that backfires. |
| **`PCAPPipelineEditor modifies the values of properties: [...]. This is not allowed`** | The V6/V7 class. Your target set a property that UBT locks to the shared build environment it inherits from the prebuilt `UnrealEditor` binaries. Fires in under a second, before any compilation, and carries **no file, no line and no severity token** — only the exit code and `Result: Failed` mark it. | Restore `DefaultBuildSettings = BuildSettingsVersion.V7;` on **both** targets. Don't use `BuildSettingsVersion.Latest` — it's a moving alias that diverges the day Epic ships a V8. Don't reach for `BuildEnvironment = TargetBuildEnvironment.Unique` (impossible against a launcher-installed engine) or `bOverrideBuildEnvironment = true` (trades a loud rejection for silent ABI skew). |
| **`[target-warninglevel]`** | Someone added a `*WarningLevel` property to a `Target.cs` — almost always to silence a unity shadow warning. Warning-level properties are *exactly* the family UBT locks, so this breaks the build harder than the warning did. | Remove it. Rename the shadowing symbol instead. |
| **`[target-unity]`** | Someone disabled unity builds. It looks like CI hardening and is the opposite — it structurally deletes the `C4459` regression class from the gate. | Remove it. A clean CI tree already maximises unity coverage; that's the point. |
| **`[unity-fidelity]`** — dirty working tree | Untracked or modified files survived into the workspace. UBT pulls every dirty file out of its unity blob, so those files stop being covered by the unity gate. | Re-run with `clean_build`, or push a `[ci clean]` commit. If it recurs, something in the build is writing a non-ignored file into the workspace — find it and either fix it or gitignore it. |
| **`[workflow-consistency]`** | The `WITH_PCAP_WORKFLOW` wiring is half-applied across its four places. Define on but module dep gone → `PCAPMocapBridge.cpp` fails on `#include "PCapPropComponent.h"` minutes in. | Move all four together. |
| **`[msvc]` / `[winsdk]`** | No C++ toolchain or no SDK ≥ 10.0.22621.0 visible **to the runner account**. A per-user VS install is invisible to a service running as a different account — this can fail in CI while working fine for you interactively. | Install with the repo's pinned component set: `vs_installer.exe modify --installPath "<VS path>" --config "<workspace>\.vsconfig" --passive --norestart`. |
| **`[msvc-version]`** *(warning)* | MSVC 14.44.x absent; UBT silently uses the newest toolset. The only compile this project is known to pass used 14.44.35207. | Install `Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64` (already in `.vsconfig`). Warn, don't panic — a VS auto-update shouldn't red-light the only build box. |
| **`[buildconfiguration-xml]`** *(warning)* | A machine-local UBT `BuildConfiguration.xml` is in effect. Nothing in the repo controls it, and it can disable unity or change parallelism — silently weakening the gate. | Review it, or delete it so CI behaviour is decided by the repo alone. |
| **`[long-paths]`** *(warning)* | Long paths disabled and the workspace path is over 40 chars. Shows up mid-compile naming a random `.obj`, which reads like a compiler bug. | Step 2 above, and/or re-register with a shorter `--work`. |
| **`[disk]`** | Under 25 GB free fails, under 60 GB warns. A cold build wants 40–60 GB and UBA reserves 40 GB by itself. Running out mid-link produces errors that mention neither disk nor space. | Free space; run the clean build to reclaim `Intermediate/`. |
| **`[workspace]`** | The runner is checked out inside `C:\Users\mocapstaff\Desktop\PCAPPipeline`. `actions/checkout` runs `git checkout --force`, which would **discard your uncommitted work** in the tree that CONTRIBUTING documents as shared. | Re-register the runner with its own work folder (`config.cmd --work C:\r`). Never point CI at the Desktop checkout. |
| **Job stuck "Queued" forever** | `runs-on` is AND across labels and a label typo doesn't error — it queues 24 h and then fails. Or the runner service is stopped / the box is off. | Compare labels character-by-character in Settings ▸ Actions ▸ Runners. `Get-Service 'actions.runner.*'`. |
| **Job hangs until the 90-minute timeout** | `-WaitMutex` blocks with no timeout. Something else holds UBT's engine-scoped mutex — usually a Visual Studio build you left running, occasionally a wedged prior job. | Let the VS build finish, or kill stray `UnrealBuildTool` / `UbaAgent` / `cl.exe` processes owned by `ghrunner`. The timeout exists precisely so this can't pin the runner for GitHub's 6-hour default. |
| **Red build, but no annotations anywhere** | Either the failure has no parseable diagnostic (the V6/V7 class is exactly this), or `.github/ue-problem-matcher.json` is missing — in which case the log carries a `No problem matcher` warning. The build still gates either way. | Read the job summary and the artifact. Annotations are surfacing, not the gate. |
| **Annotations in Checks but not on Files changed** | The runner failed to convert absolute Windows paths to repo-relative forward-slash form. | Set `ACTIONS_STEP_DEBUG=true` as a repository *variable* and re-run to see the conversion. Paths under the engine install are dropped on purpose — they're outside the workspace. |

---

## Confirm on the box, first run

These are safe defaults chosen without access to the machine. Check them once; none should need changing more than once.

| # | Assumption | How to check |
|---|---|---|
| 1 | `HKLM\SOFTWARE\EpicGames\Unreal Engine\5.8\InstalledDirectory` exists | The Preflight summary names which probe won. If it fell through to the glob, the registry key is missing — harmless, but set `UE_ROOT` for determinism. |
| 2 | UBT accepts `-Log=<path>` | If not, the Collect step emits a `No UBT log` warning and falls back to `%LOCALAPPDATA%\UnrealBuildTool\Log.txt`. |
| 3 | Dropping `-FromMsBuild` doesn't change the log shape in a way the matcher cares about | Both archived logs (`CodeErrors`, `CurrentErrorCodes`) are Visual Studio Output-window captures with MSBuild's `1>` prefix, which won't appear here. The matchers treat that prefix as optional either way. Diff the first run's raw output against `CurrentErrorCodes`. |
| 4 | The box is powered on Mondays 13:00 UTC | Otherwise the weekly clean canary is a permanent red. Move the cron or delete the `schedule:` block. |
| 5 | The first CI run may fail on real C++ | Several commits have landed with no in-repo Windows build record, and CI compiles in full unity where your tree usually doesn't. A red first run is most likely genuine drift, not a broken CI setup. |
| 6 | Warm-workspace non-determinism | Both Vicon `Build.cs` files enumerate `Binaries/Win64/*.dll` at rules-evaluation time, and `Binaries/` is gitignored — so run 1 (cold) and run 2 (warm) legitimately compute different `RuntimeDependencies`. Don't treat a single green run as proof of a clean-tree build; that's the weekly canary's job. |
| 7 | First build will be much slower than you expect | `ghrunner` has its own profile and therefore a cold DerivedDataCache. Budget accordingly before concluding something is wrong. |
| 8 | `ViconDataStreamSDK.Build.cs` passes a full path to `PublicDelayLoadDLLs` where a bare filename is expected | Expect `LNK4199`-class warning noise on the first **clean** link — neither archived log covers a clean link. It's a real (pre-existing) bug in the vendored file, not a CI regression. Don't react by adding warnings-as-errors. |

> Workflows: [`.github/workflows/windows-build.yml`](../.github/workflows/windows-build.yml) · [`.github/workflows/host-tests.yml`](../.github/workflows/host-tests.yml) · matchers: [`.github/ue-problem-matcher.json`](../.github/ue-problem-matcher.json)
>
> The two archived failure logs this design was built against are in the repo root: `CodeErrors` (the V6/V7 rejection) and `CurrentErrorCodes` (the `C4459` unity break).
