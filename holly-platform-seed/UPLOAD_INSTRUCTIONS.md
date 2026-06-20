# Deploying this seed into the holly-platform repo

Once these files land in `chestnutinfrastructure/holly-platform` on
`main`, any new Claude Code session on that repo will pick up `CLAUDE.md`
automatically and be ready to work as v6-Carson.

You have three routes — pick whichever suits where you are.

---

## Route A — GitHub web (no git, easiest from mobile)

1. If the repo doesn't exist yet:
   - Go to `github.com/new`
   - Owner: `chestnutinfrastructure`
   - Name: `holly-platform`
   - Visibility: **Private**
   - Tick **"Add a README"** (so it isn't empty)
   - Create

2. In the repo → **Add file → Upload files**. Drag in EVERY file from
   this `holly-platform-seed/` folder, including the hidden ones:

   - `CLAUDE.md`
   - `HARDWARE_v6.md`
   - `Holly_Master_Context_Prompt_v4.md`
   - `Holly_Migration_Plan_v6000.md`
   - `KICKOFF_PROMPT.md`
   - `README.md` ← overwrites the auto-created one
   - `.gitignore`
   - `.mcp.json`

   GitHub web can hide dotfiles in some upload pickers — if `.gitignore`
   and `.mcp.json` don't appear in your file picker, on Mac press
   `Cmd+Shift+.` to show hidden files; on Windows tick "Show hidden
   files" in the file explorer view options.

3. Commit message: `Seed holly-platform with v6 spec, hardware, kickoff`
4. Commit directly to `main`.

## Route B — Desktop git

```bash
git clone https://github.com/chestnutinfrastructure/holly-platform.git
cd holly-platform

# Copy every file from holly-platform-seed/ in the classroomsounds repo
# into this folder (including hidden .gitignore and .mcp.json):
cp -a /path/to/classroomsounds/holly-platform-seed/. .

git add -A
git commit -m "Seed holly-platform with v6 spec, hardware, kickoff"
git push origin main
```

## Route C — Let Carson do it from a session with BOTH repos in scope

Open a Claude Code session that has BOTH `classroomsounds` and
`holly-platform` writable, and say:

> "Copy every file from `classroomsounds/holly-platform-seed/` into the
> root of `holly-platform`, commit as 'Seed holly-platform with v6 spec,
> hardware, kickoff' and push to main."

This is the fastest route if your environment supports cross-repo scope.

---

## After upload — start the v6 chat

1. Open a fresh Claude Code session on **`chestnutinfrastructure/holly-platform`**.
2. As your first message, paste the entire contents of `KICKOFF_PROMPT.md`.
3. v6-Carson will read the four docs, confirm the repo, and start
   Phase 0 — the Postgres schema SQL file.

For the v5 chat: open a Claude Code session on
**`chestnutinfrastructure/classroomsounds`** — the `CLAUDE.md` at the
root of that repo defines v5-Carson, so no kickoff prompt is needed.
Just describe what you need: a bug fix, an OTA bump, a dashboard tweak,
help with Lyppard.

---

## Once holly-platform is seeded — clean classroomsounds

After the upload succeeds, the following files in `classroomsounds` are
**leftovers** from the staging period and should be removed from v5 to
maintain the hard separation:

- `Holly_Migration_Plan_v6000.md` — belongs only in holly-platform
- `Holly_Master_Context_Prompt_v4.md` — belongs in both, actually (it's
  generic project history) — keep, or duplicate is fine
- `.mcp.json` — points at v6 Supabase; should NOT live in the v5 repo,
  remove
- `holly-platform-seed/` — the whole staging folder, remove
- `holly-platform-seed/UPLOAD_INSTRUCTIONS.md` — this file, remove

Either ask v5-Carson to do this cleanup in your next v5 session (it's
purely deletion + commit), or do it by hand and push. Don't do it
BEFORE the v6 upload — once these files are gone here, they're gone.
