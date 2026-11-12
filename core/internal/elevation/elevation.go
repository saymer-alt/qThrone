// Package elevation runs the Windows core and guard elevated through the per-user scheduled task a one-time UAC grant
// registers: the unelevated RawlineCore Rawline starts becomes a stub that hands its stdio, IPC pipe and a job object to
// an elevated copy of itself, then mirrors its exit code.
package elevation

import "net"

// EnvTask is the task path Rawline sets when the core should start elevated.
const EnvTask = "RAWLINE_ELEVATION_TASK"

type Mode string

const (
	ModeCore  Mode = "core"
	ModeGuard Mode = "guard"
)

// Attached is what an elevated child took over from its stub.
type Attached struct {
	Mode    Mode
	GuiPID  int
	StubPID int
	// Core is the install's RawlineCore.exe the grant was made for, which the stub runs from.
	Core string
	// Conn is Rawline's IPC pipe; core mode only.
	Conn net.Conn
}
