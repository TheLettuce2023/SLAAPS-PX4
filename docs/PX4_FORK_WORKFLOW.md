# SLAAPS PX4 Fork Workflow

This checkout uses a real GitHub fork and keeps the official PX4 repository as a read-only integration source:

```text
origin   = https://github.com/TheLettuce2023/SLAAPS-PX4.git
upstream = https://github.com/PX4/PX4-Autopilot.git
```

Project work belongs on `slaaps-sitl` or a later topic branch. Never push to `upstream`; push project branches only to `origin`.

## Inspect the repository

```bash
git branch --show-current
git remote -v
git status
git submodule status --recursive
```

## Synchronize with PX4

Fetch the official history, inspect the incoming changes, and merge deliberately:

```bash
git fetch upstream
git switch slaaps-sitl
git log --oneline --decorate --graph HEAD..upstream/main
git merge upstream/main
git submodule sync --recursive
git submodule update --init --recursive
```

Resolve and test any conflicts before pushing:

```bash
make px4_sitl
git push -u origin slaaps-sitl
```

Do not automatically rebase shared project work. A rebase may be appropriate for an unpublished personal topic branch, but it rewrites history and must be chosen intentionally.

## Baseline recorded 2026-09-15

- Authenticated GitHub owner: `TheLettuce2023`
- Fork: `TheLettuce2023/SLAAPS-PX4`
- Fork parent: `PX4/PX4-Autopilot`
- PX4 revision: `e880f1cd79e2453ef3dfaf68ee1c067b98ea9a53`
- PX4 describe: `v1.18.0-beta1-677-ge880f1cd79`
- Development branch: `slaaps-sitl`
- Initial Git state after clone, branch creation, and submodule initialization: clean
- Gazebo models submodule revision: `bb0b9cf974acf4f1bcb5f5fcf80b88841562dea9`
- Ubuntu: 24.04.5 LTS under WSL2
- WSL/kernel: WSL 2.7.3.0, kernel 6.6.114.1-microsoft-standard-WSL2

The stock `gz_rc_cessna` model and its `4003_gz_rc_cessna` airframe are permanent regression references. Custom SLAAPS model work must use separate model and airframe definitions.
