// OpenCode plugin — calls send.py via CLI args.
// send.py has two modes: stdin JSON (Claude/Codex hooks) and CLI args (us).
import { spawn } from "child_process"
import { join } from "path"
import { homedir } from "os"

// Try project-local path first (for project-scoped installs), fall back to global
import { existsSync } from "fs"
import { dirname } from "path"
import { fileURLToPath } from "url"
const __dirname = dirname(fileURLToPath(import.meta.url))
const PROJECT_SEND_PY = join(__dirname, "..", "..", "..", "transport", "send.py")
const GLOBAL_SEND_PY = join(homedir(), ".local", "share", "3dai-led", "transport", "send.py")
const SEND_PY = existsSync(PROJECT_SEND_PY) ? PROJECT_SEND_PY : GLOBAL_SEND_PY

const EVENT_TO_STATE = {
  "message.updated":     "thinking",
  "session.updated":     "thinking",
  "permission.asked":    "waiting",
  "permission.replied":  "thinking",
  "session.created":     "thinking",
  "session.idle":        "off",
  "session.error":       "error",
  "session.compacted":   "busy",
}

// Singleton: at most ONE send.py runs at any time.
// When events fire rapidly we kill the previous process before spawning
// a new one — prevents the process pile-up that ate 64 GB of RAM.
let _lastProc = null

function sendLedState(state) {
  if (_lastProc && _lastProc.exitCode === null) {
    try { _lastProc.kill("SIGTERM") } catch (_) {}
    try { _lastProc.kill("SIGKILL") } catch (_) {}
  }
  const project = process.cwd().split(/[/\\]/).filter(Boolean).pop() || ""
  const proc = spawn("python", [SEND_PY, state, "--project", project, "--platform", "opencode"], {
    stdio: "ignore",
    timeout: 5000,
  })
  proc.on("error", () => {})
  proc.on("exit", () => { _lastProc = null })
  proc.unref()  // don't block parent process exit
  _lastProc = proc
}

export const Esp32LedPlugin = async () => {
  return {
    event: async ({ event }) => {
      const state = EVENT_TO_STATE[event.type]
      if (state) sendLedState(state)
    },
  }
}
