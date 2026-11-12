package providergeo

import (
	"bufio"
	"bytes"
	"io"
	"os"
	"path/filepath"

	"google.golang.org/protobuf/encoding/protowire"
)

// xrayEntry returns the entry as Xray's loader finds it: the category name upper case and first,
// as it compares the first field's bytes with the upper-case name of a rule.
func xrayEntry(entry []byte) ([]byte, bool, error) {
	var code, rest []byte
	codeFirst := false
	for i := 0; len(entry) > 0; i++ {
		num, typ, n := protowire.ConsumeTag(entry)
		if n < 0 {
			return nil, false, protowire.ParseError(n)
		}
		m := protowire.ConsumeFieldValue(num, typ, entry[n:])
		if m < 0 {
			return nil, false, protowire.ParseError(m)
		}
		if num == 1 && typ == protowire.BytesType {
			code, _ = protowire.ConsumeBytes(entry[n:])
			codeFirst = i == 0
		} else {
			rest = append(rest, entry[:n+m]...)
		}
		entry = entry[n+m:]
	}
	upper := bytes.ToUpper(code)
	if codeFirst && bytes.Equal(upper, code) {
		return nil, false, nil
	}
	out := protowire.AppendTag(nil, 1, protowire.BytesType)
	out = protowire.AppendBytes(out, upper)
	return append(out, rest...), true, nil
}

// NormalizeForXray rewrites a valid geo file in place when Xray could not find some of its categories.
func NormalizeForXray(path string) error {
	temp, err := os.CreateTemp(filepath.Dir(path), ".geo-*.dat")
	if err != nil {
		return err
	}
	defer os.Remove(temp.Name())
	w := bufio.NewWriter(temp)
	changed := false
	err = entries(path, func(_ string, r *io.SectionReader) (bool, error) {
		entry, err := io.ReadAll(r)
		if err != nil {
			return false, err
		}
		fixed, rewrote, err := xrayEntry(entry)
		if err != nil {
			return false, err
		}
		if rewrote {
			changed, entry = true, fixed
		}
		b := protowire.AppendTag(nil, 1, protowire.BytesType)
		_, err = w.Write(protowire.AppendBytes(b, entry))
		return false, err
	})
	if err == nil {
		err = w.Flush()
	}
	if cerr := temp.Close(); err == nil {
		err = cerr
	}
	if err != nil || !changed {
		return err
	}
	return os.Rename(temp.Name(), path)
}
