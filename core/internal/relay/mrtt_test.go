package relay

import (
	"strings"
	"testing"
)

func TestMaxPageReadsLikeTheOriginal(t *testing.T) {
	for raw, want := range map[string]string{
		"https://web.max.ru/-80123":          "-80123",
		"https://web.max.ru/chat/-5":         "-5",
		"https://web.max.ru/c/77?x=1":        "77",
		"https://web.max.ru/?chat_id=-9":     "-9",
		"https://web.max.ru/":                "",
		"https://web.max.ru/settings":        "",
		"http://web.max.ru/-1":               "",
		"https://web.max.ru.evil.example/-1": "",
		"https://user@web.max.ru/-1":         "",
		"https://web.max.ru:444/-1":          "",
	} {
		if got := maxGroupFromURL(raw); got != want {
			t.Errorf("%s: %q, want %q", raw, got, want)
		}
	}
	if validMaxToken("short") || validMaxToken(strings.Repeat("a", 31)+" b") || !validMaxToken(strings.Repeat("a", 32)) {
		t.Fatal("token check")
	}
}
