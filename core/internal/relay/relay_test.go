package relay

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/zalando/go-keyring"
)

type roundTripFunc func(*http.Request) (*http.Response, error)

func TestFingerprintEventsPreserveShortSecretCharacters(t *testing.T) {
	var output bytes.Buffer
	p := &protocol{output: json.NewEncoder(&output), secrets: []string{"1"}}
	fingerprint := "SHA256:a1b1c1d1e1f1"
	for _, kind := range []string{"trust", "host-key", "admin-pin"} {
		p.emit(kind, fingerprint, nil)
	}
	p.emit("log", "password=1", nil)
	decoder := json.NewDecoder(&output)
	for i := 0; i < 4; i++ {
		var event Event
		if e := decoder.Decode(&event); e != nil {
			t.Fatal(e)
		}
		if i < 3 && event.Text != fingerprint {
			t.Fatal("Fingerprint was corrupted by log redaction")
		}
		if i == 3 && event.Text != "password=[скрыто]" {
			t.Fatal("Console secret was not redacted")
		}
	}
}

func (f roundTripFunc) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }
func TestVKHashesCacheAndRateLimit(t *testing.T) {
	keyring.MockInit()
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)
	original := http.DefaultTransport
	defer func() { http.DefaultTransport = original }()
	calls := 0
	authorizations := 0
	limited := false
	http.DefaultTransport = roundTripFunc(func(r *http.Request) (*http.Response, error) {
		if r.URL.Host == "oauth.vk.ru" {
			authorizations++
			if r.Method != "GET" || r.URL.Query().Get("client_id") != "6287487" || r.URL.Query().Get("scope") != "messages" || r.Header.Get("User-Agent") != "browser-UA" || r.Header.Get("Accept") != "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8" || !strings.Contains(r.Header.Get("Cookie"), "remixsid=session") || !strings.Contains(r.Header.Get("Cookie"), "id_session=id-cookie") {
				t.Fatal("OAuth differs from original qWDTT cookie flow")
			}
			redirect := "https://oauth.vk.ru/blank.html#" + url.Values{"access_token": {"test-token"}, "user_id": {"42"}, "state": {r.URL.Query().Get("state")}}.Encode()
			return &http.Response{StatusCode: 302, Body: io.NopCloser(strings.NewReader("")), Header: http.Header{"Location": {redirect}}}, nil
		}
		if r.URL.Host != "api.vk.ru" || r.Method != "GET" || r.URL.Path != "/method/calls.start" || r.URL.Query().Get("access_token") != "test-token" || r.URL.Query().Get("v") != "5.199" || r.Header.Get("User-Agent") != "okhttp/4.12.0" {
			t.Fatal("Incorrect VK API request")
		}
		calls++
		body := `{"response":{"join_link":"https://vk.ru/call/join/abcdefghijklmnop"}}`
		if limited {
			body = `{"error":{"error_code":14}}`
		}
		return &http.Response{StatusCode: 200, Body: io.NopCloser(strings.NewReader(body)), Header: make(http.Header)}, nil
	})
	session := account{User: "42", UserAgent: "browser-UA", Cookies: []browserCookie{{Name: "remixsid", Value: "session", Domain: ".vk.ru", Path: "/"}, {Name: "id_session", Value: "id-cookie", Domain: "id.vk.com", Path: "/"}}}
	if e := saveAccount(session); e != nil {
		t.Fatal(e)
	}
	request, _ := json.Marshal(Request{Operation: "vk-hashes", Count: 1})
	for i := 0; i < 2; i++ {
		var output bytes.Buffer
		if e := Run(bytes.NewReader(append(request, '\n')), &output); e != nil {
			t.Fatal(e)
		}
		if !strings.Contains(output.String(), "abcdefghijklmnop") || strings.Contains(output.String(), "test-token") {
			t.Fatal(output.String())
		}
		if i == 0 {
			a, e := readAccount()
			if e != nil {
				t.Fatal(e)
			}
			a.Expires = 0
			if e = saveAccount(a); e != nil {
				t.Fatal(e)
			}
		}
	}
	if calls != 1 || authorizations != 1 {
		t.Fatalf("Cached hashes generated again: %d", calls)
	}
	limited = true
	if e := saveAccount(session); e != nil {
		t.Fatal(e)
	}
	for i := 0; i < 2; i++ {
		var output bytes.Buffer
		if e := Run(bytes.NewReader(append(request, '\n')), &output); e == nil {
			t.Fatal("VK limit ignored")
		}
	}
	if calls != 2 || authorizations != 2 {
		t.Fatal("Repeated request during VK cooldown")
	}
	unlink, _ := json.Marshal(Request{Operation: "secret-delete", ID: "account"})
	if e := Run(bytes.NewReader(append(unlink, '\n')), io.Discard); e != nil {
		t.Fatal(e)
	}
	info, e := accountInfo()
	if e != nil || info["linked"] != false {
		t.Fatal("Account remains linked")
	}
}
func TestVaultLockCancellation(t *testing.T) {
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)
	unlock, e := vaultLock(context.Background(), "account")
	if e != nil {
		t.Fatal(e)
	}
	defer unlock()
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, e = vaultLock(ctx, "account"); e == nil {
		t.Fatal("Cancelled waiter acquired lock")
	}
}

func TestVaultEncryption(t *testing.T) {
	keyring.MockInit()
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)
	secret := strings.Repeat("secret-key-material\n", 400)
	if e := secureSet("servers", secret); e != nil {
		t.Fatal(e)
	}
	read, e := secureGet("servers")
	if e != nil || read != secret {
		t.Fatalf("Vault read: %v", e)
	}
	path, e := vaultPath("servers")
	if e != nil {
		t.Fatal(e)
	}
	data, e := os.ReadFile(path)
	if e != nil {
		t.Fatal(e)
	}
	if bytes.Contains(data, []byte("secret-key-material")) {
		t.Fatal("Plaintext stored")
	}
	data[len(data)-1] ^= 1
	if e = os.WriteFile(path, data, 0600); e != nil {
		t.Fatal(e)
	}
	if _, e = secureGet("servers"); e == nil {
		t.Fatal("Tampered vault accepted")
	}
	if e = secureDelete("servers"); e != nil {
		t.Fatal(e)
	}
	if _, e = secureGet("servers"); e != keyring.ErrNotFound {
		t.Fatal(e)
	}
}

func TestOAuthRedirect(t *testing.T) {
	valid := "https://oauth.vk.com/blank.html#" + url.Values{"access_token": {"test-token"}, "user_id": {"42"}, "state": {"state-123"}, "expires_in": {"3600"}}.Encode()
	a, ok := oauthToken(valid, "state-123")
	if !ok || a.User != "42" || a.Token != "test-token" || a.Expires <= time.Now().Unix() {
		t.Fatal("Valid VK redirect rejected")
	}
	for _, raw := range []string{strings.Replace(valid, "https:", "http:", 1), strings.Replace(valid, "oauth.vk.com", "oauth.vk.com.evil.example", 1), strings.Replace(valid, "/blank.html", "/other", 1), strings.Replace(valid, "state-123", "wrong", 1), strings.Replace(valid, "oauth.vk.com", "user@oauth.vk.com", 1)} {
		if _, ok := oauthToken(raw, "state-123"); ok {
			t.Fatalf("Untrusted redirect accepted: %s", raw)
		}
	}
	for _, raw := range []string{"https://vk.com/", "https://m.vk.ru/", "https://login.vk.com/?act=grant_access"} {
		if !trustedVK(raw) {
			t.Fatal(raw)
		}
	}
	for _, raw := range []string{"http://vk.com/", "https://vk.com.evil/", "https://vk.com:444/", "https://evil@vk.com/"} {
		if trustedVK(raw) {
			t.Fatal(raw)
		}
	}
}

func TestVKTokenFallbackRequiresConfirmation(t *testing.T) {
	keyring.MockInit()
	dir := t.TempDir()
	t.Setenv("APPDATA", dir)
	t.Setenv("XDG_CONFIG_HOME", dir)
	t.Setenv("HOME", dir)
	original := http.DefaultTransport
	defer func() { http.DefaultTransport = original }()
	requests := 0
	http.DefaultTransport = roundTripFunc(func(r *http.Request) (*http.Response, error) {
		requests++
		if r.URL.Host != "oauth.vk.ru" {
			t.Fatal("API called without an access token")
		}
		return &http.Response{StatusCode: 200, Body: io.NopCloser(strings.NewReader("login required")), Header: make(http.Header)}, nil
	})
	if err := saveAccount(account{Cookies: []browserCookie{{Name: "remixsid", Value: "valid-session", Domain: ".vk.ru"}}}); err != nil {
		t.Fatal(err)
	}
	var output bytes.Buffer
	err := Run(strings.NewReader("{\"operation\":\"vk-hashes\",\"count\":1}\nreject\n"), &output)
	if err == nil || !strings.Contains(err.Error(), "отменено") || !strings.Contains(output.String(), "vk-confirm") || requests != 1 {
		t.Fatalf("Browser fallback ignored confirmation: %v, requests=%d, output=%s", err, requests, output.String())
	}
	// A headless caller with no UI response must fail instead of launching a browser.
	output.Reset()
	if err = Run(strings.NewReader("{\"operation\":\"vk-hashes\",\"count\":1}\n"), &output); err == nil || !strings.Contains(err.Error(), "отменено") {
		t.Fatalf("Unattended fallback: %v", err)
	}
	commands := make(chan string, 1)
	commands <- "approve"
	p := protocol{ctx: context.Background(), commands: commands, output: json.NewEncoder(io.Discard)}
	if err = p.confirmVKBrowser(); err != nil {
		t.Fatal(err)
	}
}

func TestVKLoginMatchesOriginalCompletion(t *testing.T) {
	cookies := []browserCookie{{Name: "remixsid", Value: "valid-session-cookie", Domain: ".vk.ru"}}
	if !vkLoginComplete("https://m.vk.ru/feed", cookies) {
		t.Fatal("completed session rejected")
	}
	for _, location := range []string{"https://m.vk.ru/login", "https://vk.com/login.php", "https://vk.ru/?act=login", "https://id.vk.ru/auth", "https://id.vk.com/authorize", "about:blank", "https://vk.ru.evil.example/feed"} {
		if vkLoginComplete(location, cookies) {
			t.Fatalf("login ended too early on %s", location)
		}
	}
	cookies[0].Value = "short"
	if vkLoginComplete("https://vk.ru/feed", cookies) {
		t.Fatal("short session accepted")
	}
}

func TestVKHTTPRateLimit(t *testing.T) {
	previous := http.DefaultTransport
	defer func() { http.DefaultTransport = previous }()
	http.DefaultTransport = roundTripFunc(func(*http.Request) (*http.Response, error) {
		return &http.Response{StatusCode: http.StatusTooManyRequests, Body: io.NopCloser(strings.NewReader("")), Header: make(http.Header)}, nil
	})
	_, err := vkAPI(context.Background(), "test-token", "calls.start")
	limit, ok := err.(vkError)
	if !ok || limit.Code != 6 {
		t.Fatal("HTTP rate limit did not engage VK cooldown")
	}
}
func TestShellQuote(t *testing.T) {
	if quote("a'b") != "'a'\"'\"'b'" {
		t.Fatal("Shell quote")
	}
}
func TestSplitSecretsAreRedacted(t *testing.T) {
	var output, raw bytes.Buffer
	p := &protocol{output: json.NewEncoder(&output), secrets: []string{"private-secret"}}
	w := deployWriter{p: p, buffer: &raw}
	w.Write([]byte("value=private-"))
	w.Write([]byte("secret\n"))
	w.flush()
	if strings.Contains(output.String(), "private-") || !strings.Contains(output.String(), "[скрыто]") {
		t.Fatal(output.String())
	}
}
