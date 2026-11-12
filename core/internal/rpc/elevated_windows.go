package rpc

import "RawlineCore/internal/elevation"

func isElevated() bool {
	return elevation.Elevated()
}
