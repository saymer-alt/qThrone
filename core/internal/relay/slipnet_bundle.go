package relay

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/pbkdf2"
	"crypto/sha256"
	"encoding/base64"
	"errors"
	"strings"
)

// SlipNet's "Export All (Encrypted)": slipnet-bundle-enc:// + base64 of
// [0x01][salt 16][iv 12][AES-256-GCM ciphertext+tag], the key PBKDF2-HMAC-SHA256 of the
// user's password, 600000 rounds (its BundleCrypto). The plain text is its usual export.
const (
	slipnetBundleScheme     = "slipnet-bundle-enc://"
	slipnetBundleRounds     = 600_000
	slipnetBundleSaltLength = 16
	slipnetBundleIVLength   = 12
)

func decryptSlipnetBundle(link, password string) (string, error) {
	link = strings.TrimSpace(link)
	if len(link) < len(slipnetBundleScheme) || !strings.EqualFold(link[:len(slipnetBundleScheme)], slipnetBundleScheme) {
		return "", errors.New("not a SlipNet encrypted bundle")
	}
	if password == "" {
		return "", errors.New("the bundle needs its password")
	}
	payload := strings.TrimSpace(link[len(slipnetBundleScheme):])
	var data []byte
	var err error
	for _, encoding := range []*base64.Encoding{base64.StdEncoding, base64.RawStdEncoding, base64.URLEncoding, base64.RawURLEncoding} {
		if data, err = encoding.DecodeString(payload); err == nil {
			break
		}
	}
	if err != nil {
		return "", errors.New("the bundle is damaged")
	}
	if len(data) < 1+slipnetBundleSaltLength+slipnetBundleIVLength+16 || data[0] != 0x01 {
		return "", errors.New("unsupported bundle format")
	}
	salt := data[1 : 1+slipnetBundleSaltLength]
	iv := data[1+slipnetBundleSaltLength : 1+slipnetBundleSaltLength+slipnetBundleIVLength]
	key, err := pbkdf2.Key(sha256.New, password, salt, slipnetBundleRounds, 32)
	if err != nil {
		return "", err
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return "", err
	}
	gcm, err := cipher.NewGCM(block)
	if err != nil {
		return "", err
	}
	plain, err := gcm.Open(nil, iv, data[1+slipnetBundleSaltLength+slipnetBundleIVLength:], nil)
	if err != nil {
		return "", errors.New("wrong password or damaged bundle")
	}
	return string(plain), nil
}
