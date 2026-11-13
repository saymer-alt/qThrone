package relay

import (
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"strings"
	"testing"
	"time"
)

// TestPanelOnRealVPS deploys WDTT Panel the way the window does, on a disposable VPS given by
// RAWLINE_PANEL_E2E_HOST and RAWLINE_PANEL_E2E_PASSWORD (root). It removes everything it installed.
func TestPanelOnRealVPS(t *testing.T) {
	host, password := os.Getenv("RAWLINE_PANEL_E2E_HOST"), os.Getenv("RAWLINE_PANEL_E2E_PASSWORD")
	if host == "" || password == "" {
		t.Skip("RAWLINE_PANEL_E2E_HOST and RAWLINE_PANEL_E2E_PASSWORD are not set")
	}
	s := map[string]string{"host": host, "sshPort": "22", "user": "root", "auth": "password", "sshPassword": password}
	step := func(action string, answers ...string) panelEvents {
		t.Helper()
		events, raw, e := runPanelFor(t, action, "", s, 3*time.Hour, answers...)
		if e != nil {
			t.Fatalf("%s: %v\n%s", action, e, tail(raw))
		}
		if strings.Contains(raw, password) {
			t.Fatalf("%s: SSH password in the output", action)
		}
		for _, event := range events.of("host-key") {
			s["hostKey"] = event.Text
		}
		return events
	}
	status := func(events panelEvents) map[string]any {
		t.Helper()
		all := events.of("status")
		if len(all) == 0 {
			t.Fatal("no status")
		}
		return all[len(all)-1].Value.(map[string]any)
	}
	if role := status(step("status", "approve"))["role"]; role != "" {
		t.Fatalf("VPS is not clean: %v", role)
	}
	for k, v := range map[string]string{"role": "panel", "protocols": "", "access": "local"} {
		s[k] = v
	}
	if result := step("check").of("result")[0].Value.(map[string]any); result["ok"] != true {
		t.Fatalf("check: %v", result["text"])
	}
	installed := status(step("install"))
	if installed["role"] != "panel" || installed["mode"] != "local" || installed["password"] == "" {
		t.Fatalf("install: %v", installed)
	}
	s["panelPort"] = fmt.Sprint(installed["panel_port"])
	path := installed["panel_path"].(string)
	// The panel through the tunnel, as the window opens it.
	login := func(user, pass string) bool {
		t.Helper()
		ok := false
		withTunnel(t, s, func(port int) {
			response, e := http.PostForm(fmt.Sprintf("http://127.0.0.1:%d%slogin", port, path), url.Values{"username": {user}, "password": {pass}})
			if e != nil {
				t.Fatal(e)
			}
			body, _ := io.ReadAll(response.Body)
			response.Body.Close()
			ok = strings.Contains(string(body), `"success":true`)
		})
		return ok
	}
	if !login(installed["username"].(string), installed["password"].(string)) {
		t.Fatal("installed login rejected")
	}
	s["panelUser"], s["panelPassword"] = "rawline", "E2e-"+fmt.Sprint(time.Now().UnixNano())
	changed := status(step("credentials"))
	if changed["username"] != "rawline" || changed["password"] != s["panelPassword"] {
		t.Fatalf("credentials: %v", changed)
	}
	if !login("rawline", s["panelPassword"]) || login(installed["username"].(string), installed["password"].(string)) {
		t.Fatal("new login not in force")
	}
	if role := status(step("uninstall-all"))["role"]; role != "" {
		t.Fatalf("uninstall left %v", role)
	}
}

func tail(text string) string {
	if len(text) > 4000 {
		return text[len(text)-4000:]
	}
	return text
}
