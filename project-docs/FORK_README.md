# darktable-mac

A fork of [darktable](https://github.com/darktable-org/darktable), extending it with on-device AI-assisted
selective editing (people/sky/background segmentation) and content-aware fill/inpainting, for personal
macOS photo management — built in the open with an eye toward upstreaming generally-useful pieces.

- **PRD:** [PRD.md](PRD.md)
- **Phase 0 tracking:** [PHASE0_TASKS.md](PHASE0_TASKS.md)
- **Upstream:** https://github.com/darktable-org/darktable (remote `upstream`)
- **This fork:** https://github.com/tinkeran/darktable-mac (remote `origin`)

## Remotes

```
origin    https://github.com/tinkeran/darktable-mac.git  (your fork — push here)
upstream  https://github.com/darktable-org/darktable.git (read from here, PR back to here)
```

Pull upstream changes into `master` periodically:

```bash
git fetch upstream
git merge upstream/master
```

Do feature work on branches (e.g. `feature/trash-delete`, `feature/sam2-segmentation`), not directly on
`master`, so upstream merges stay clean.

## Licensing

GPL (inherited from darktable). New AI model integrations (SAM 2, LaMa) are Apache-2.0 and bundled
alongside the GPL codebase, per PRD §8c.
