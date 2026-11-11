package relay

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/pbkdf2"
	"crypto/sha256"
	"encoding/base64"
	"testing"
)

// Built as SlipNet's BundleCrypto.encrypt builds it.
func sealSlipnetBundle(t *testing.T, plain, password string) string {
	t.Helper()
	salt := []byte("0123456789abcdef")
	iv := []byte("abcdefghijkl")
	key, err := pbkdf2.Key(sha256.New, password, salt, slipnetBundleRounds, 32)
	if err != nil {
		t.Fatal(err)
	}
	block, _ := aes.NewCipher(key)
	gcm, _ := cipher.NewGCM(block)
	data := append([]byte{0x01}, salt...)
	data = append(data, iv...)
	data = gcm.Seal(data, iv, []byte(plain), nil)
	return slipnetBundleScheme + base64.StdEncoding.EncodeToString(data)
}

func TestSlipnetBundle(t *testing.T) {
	link := sealSlipnetBundle(t, "slipnet://one\nslipnet://two", "secret")
	if got, err := decryptSlipnetBundle(link, "secret"); err != nil || got != "slipnet://one\nslipnet://two" {
		t.Fatalf("decrypt = %q, %v", got, err)
	}
	if _, err := decryptSlipnetBundle(link, "wrong"); err == nil {
		t.Fatal("a wrong password must fail")
	}
	if _, err := decryptSlipnetBundle(link, ""); err == nil {
		t.Fatal("an empty password must fail")
	}
}
