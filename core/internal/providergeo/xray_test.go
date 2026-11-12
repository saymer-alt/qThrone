package providergeo

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"

	"google.golang.org/protobuf/encoding/protowire"
)

func geoEntry(fields ...[]byte) []byte {
	entry := bytes.Join(fields, nil)
	return protowire.AppendBytes(protowire.AppendTag(nil, 1, protowire.BytesType), entry)
}

func bytesField(num protowire.Number, v string) []byte {
	return protowire.AppendString(protowire.AppendTag(nil, num, protowire.BytesType), v)
}

func TestNormalizeForXray(t *testing.T) {
	dir := t.TempDir()
	cidr := bytesField(2, "\x0a\x04\x01\x02\x03\x00\x10\x18")
	good := geoEntry(bytesField(1, "US"), cidr)
	path := filepath.Join(dir, "geo.dat")
	// "ru" lower case, and "cn" after its CIDR: Xray finds neither.
	if err := os.WriteFile(path, bytes.Join([][]byte{geoEntry(bytesField(1, "ru"), cidr), geoEntry(cidr, bytesField(1, "cn")), good}, nil), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := NormalizeForXray(path); err != nil {
		t.Fatal(err)
	}
	got, _ := os.ReadFile(path)
	want := bytes.Join([][]byte{geoEntry(bytesField(1, "RU"), cidr), geoEntry(bytesField(1, "CN"), cidr), good}, nil)
	if !bytes.Equal(got, want) {
		t.Fatalf("got %x want %x", got, want)
	}
	before, _ := os.Stat(path)
	if err := NormalizeForXray(path); err != nil {
		t.Fatal(err)
	}
	after, _ := os.Stat(path)
	if !after.ModTime().Equal(before.ModTime()) {
		t.Fatal("a file Xray reads as is must stay untouched")
	}
}
