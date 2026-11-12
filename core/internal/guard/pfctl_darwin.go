package guard

import (
	"regexp"

	"RawlineCore/internal/pfctl"
)

// /etc/pf.conf evaluates the com.apple/* children in name order: "000." runs before Apple's and the bridge's.
const pfAnchorPrefix = pfctl.RootAnchor + "/000.rawline-guard."

var pfAnchorIDPattern = regexp.MustCompile(`^[0-9a-f]{8}$`)

func pfLockPath(id string) string {
	return "/var/run/rawline-guard." + id + ".lock"
}
