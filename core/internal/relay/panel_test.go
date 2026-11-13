package relay

import (
	"bufio"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"golang.org/x/crypto/ssh"
)

// fakeVPS is an SSH server whose commands answer through a handler; it records each command and its stdin.
type fakeVPS struct {
	host, port, fingerprint string
	logins                  atomic.Int32
	mu                      sync.Mutex
	commands                []string
	stdins                  []string
}

func (f *fakeVPS) seen() ([]string, []string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	return append([]string(nil), f.commands...), append([]string(nil), f.stdins...)
}

func startFakeVPS(t *testing.T, password string, handler func(command, stdin string) (string, uint32)) *fakeVPS {
	t.Helper()
	_, private, e := ed25519.GenerateKey(rand.Reader)
	if e != nil {
		t.Fatal(e)
	}
	signer, e := ssh.NewSignerFromKey(private)
	if e != nil {
		t.Fatal(e)
	}
	listener, e := net.Listen("tcp", "127.0.0.1:0")
	if e != nil {
		t.Fatal(e)
	}
	t.Cleanup(func() { listener.Close() })
	f := &fakeVPS{fingerprint: ssh.FingerprintSHA256(signer.PublicKey())}
	f.host, f.port, _ = net.SplitHostPort(listener.Addr().String())
	config := &ssh.ServerConfig{PasswordCallback: func(c ssh.ConnMetadata, p []byte) (*ssh.Permissions, error) {
		f.logins.Add(1)
		if string(p) != password {
			return nil, fmt.Errorf("invalid password")
		}
		return nil, nil
	}}
	config.AddHostKey(signer)
	go func() {
		for {
			connection, e := listener.Accept()
			if e != nil {
				return
			}
			go f.serve(connection, config, handler)
		}
	}()
	return f
}

func (f *fakeVPS) serve(connection net.Conn, config *ssh.ServerConfig, handler func(string, string) (string, uint32)) {
	defer connection.Close()
	server, channels, requests, e := ssh.NewServerConn(connection, config)
	if e != nil {
		return
	}
	defer server.Close()
	go ssh.DiscardRequests(requests)
	for channel := range channels {
		switch channel.ChannelType() {
		case "direct-tcpip":
			var target struct {
				Host       string
				Port       uint32
				OriginHost string
				OriginPort uint32
			}
			if ssh.Unmarshal(channel.ExtraData(), &target) != nil {
				channel.Reject(ssh.ConnectionFailed, "bad request")
				continue
			}
			remote, e := net.Dial("tcp", net.JoinHostPort(target.Host, fmt.Sprint(target.Port)))
			if e != nil {
				channel.Reject(ssh.ConnectionFailed, e.Error())
				continue
			}
			stream, requests, e := channel.Accept()
			if e != nil {
				remote.Close()
				continue
			}
			go ssh.DiscardRequests(requests)
			go func() { io.Copy(stream, remote); stream.Close() }()
			go func() { io.Copy(remote, stream); remote.Close() }()
		case "session":
			stream, requests, e := channel.Accept()
			if e != nil {
				return
			}
			go func() {
				for request := range requests {
					if request.Type != "exec" {
						request.Reply(false, nil)
						continue
					}
					var payload struct{ Command string }
					ssh.Unmarshal(request.Payload, &payload)
					request.Reply(true, nil)
					stdin, _ := io.ReadAll(stream)
					f.mu.Lock()
					f.commands = append(f.commands, payload.Command)
					f.stdins = append(f.stdins, string(stdin))
					f.mu.Unlock()
					output, status := handler(payload.Command, string(stdin))
					io.WriteString(stream, output)
					stream.SendRequest("exit-status", false, ssh.Marshal(struct{ Status uint32 }{status}))
					stream.Close()
					return
				}
			}()
		default:
			channel.Reject(ssh.UnknownChannelType, "unsupported")
		}
	}
}

func panelServer(f *fakeVPS) map[string]string {
	return map[string]string{"host": f.host, "sshPort": f.port, "user": "root", "auth": "password", "sshPassword": "ssh-secret", "hostKey": f.fingerprint}
}

type panelEvents []Event

func (events panelEvents) of(kind string) []Event {
	var out []Event
	for _, event := range events {
		if event.Type == kind {
			out = append(out, event)
		}
	}
	return out
}

// runPanel runs one panel request the way the GUI does: the request line, then the answers to its questions.
func runPanel(t *testing.T, action, id string, s map[string]string, answers ...string) (panelEvents, string, error) {
	t.Helper()
	return runPanelFor(t, action, id, s, 20*time.Second, answers...)
}

func runPanelFor(t *testing.T, action, id string, s map[string]string, timeout time.Duration, answers ...string) (panelEvents, string, error) {
	t.Helper()
	request, _ := json.Marshal(Request{Operation: "panel", Value: action, ID: id, Server: s})
	input := string(request) + "\n"
	for _, answer := range answers {
		input += answer + "\n"
	}
	reader, writer := io.Pipe()
	go func() { io.WriteString(writer, input) }()
	var output strings.Builder
	done := make(chan error, 1)
	go func() { done <- Run(reader, &output) }()
	var e error
	select {
	case e = <-done:
	case <-time.After(timeout):
		t.Fatal("panel request did not finish")
	}
	writer.Close()
	var events panelEvents
	for _, line := range strings.Split(strings.TrimSpace(output.String()), "\n") {
		var event Event
		if json.Unmarshal([]byte(line), &event) == nil {
			events = append(events, event)
		}
	}
	return events, output.String(), e
}

const statusJSON = `{"role": "combined", "mode": "local", "domain": "", "protocols": ["wdtt"], "public_ip": "203.0.113.7", "panel_url": "http://127.0.0.1:2860/Abc/", "panel_port": 2860, "panel_path": "/Abc/", "username": "admin", "password": "panel-secret", "version": "1.4.83"}`

// plain undoes the shell quoting of asRoot, so a test reads the command as the VPS runs it.
func plain(command string) string { return strings.ReplaceAll(command, `'"'"'`, `'`) }

func installerHandler(t *testing.T) func(string, string) (string, uint32) {
	return func(command, stdin string) (string, uint32) {
		command = plain(command)
		switch {
		case strings.Contains(command, "wdtt-install.sh status"):
			return "noise from apt\n" + statusJSON + "\n", 0
		case strings.Contains(command, "wdtt-install.sh check"):
			if strings.Contains(command, "WDTT_DOMAIN='bad.example.org'") {
				return "==> Проверка совместимости\nУстановка остановлена до изменения служб:\nДомен панели bad.example.org указывает на 198.51.100.1\n", 1
			}
			return "==> Проверка совместимости\nСовместимость проверена\n", 0
		case strings.Contains(command, "wdtt-install.sh uninstall"):
			return "Удалён компонент\n", 0
		case strings.Contains(command, "wdtt-install.sh node"), strings.Contains(command, "wdtt-install.sh panel"), strings.Contains(command, "wdtt-install.sh combined"):
			if strings.Contains(command, "WDTT_PROTOCOLS='broken'") {
				return "==> Сборка ядер протоколов\nerror: build failed\n", 1
			}
			return "==> Установка пакетов\n==> Сборка панели\nПанель: http://127.0.0.1:2860/Abc/\n", 0
		case strings.Contains(command, "getent ahostsv4"):
			return "203.0.113.7\n203.0.113.7\nnot-an-ip\n2001:db8::1\n", 0
		case strings.Contains(command, "bash -c 'id -u'"):
			return "0\n", 0
		case strings.Contains(command, "wdtt-panel-credentials"):
			return "Логин и пароль панели изменены\n", 0
		}
		return "unexpected command\n", 127
	}
}

func TestPanelHostKeyIsCheckedBeforePassword(t *testing.T) {
	f := startFakeVPS(t, "ssh-secret", installerHandler(t))
	for _, test := range []struct {
		name, key, reply string
		ok               bool
	}{{"rejected", "", "reject", false}, {"changed", "SHA256:wrong", "", false}, {"approved", "", "approve", true}, {"pinned", f.fingerprint, "", true}} {
		t.Run(test.name, func(t *testing.T) {
			f.logins.Store(0)
			s := panelServer(f)
			s["hostKey"] = test.key
			events, raw, e := runPanel(t, "status", "", s, test.reply)
			if (e == nil) != test.ok || (f.logins.Load() > 0) != test.ok {
				t.Fatalf("error %v, logins %d", e, f.logins.Load())
			}
			if strings.Contains(raw, "ssh-secret") {
				t.Fatal("SSH password leaked")
			}
			if test.ok && len(events.of("status")) != 1 {
				t.Fatalf("no status: %s", raw)
			}
			if test.name == "approved" && (len(events.of("host-key")) != 1 || events.of("host-key")[0].Text != f.fingerprint) {
				t.Fatal("approved key not reported")
			}
		})
	}
}

func TestPanelStatusParsesInstallerJSON(t *testing.T) {
	f := startFakeVPS(t, "ssh-secret", installerHandler(t))
	events, raw, e := runPanel(t, "status", "", panelServer(f))
	if e != nil {
		t.Fatal(e)
	}
	status := events.of("status")[0].Value.(map[string]any)
	if status["role"] != "combined" || status["panel_port"].(float64) != 2860 || status["password"] != "panel-secret" {
		t.Fatalf("status %v", status)
	}
	commands, _ := f.seen()
	if !strings.Contains(commands[0], panelInstaller) || !strings.Contains(commands[0], "bash /root/wdtt-install.sh status") {
		t.Fatalf("status command %q", commands[0])
	}
	if len(events.of("done")) != 1 {
		t.Fatal(raw)
	}
}

func TestPanelCheckReportsInstallerVerdict(t *testing.T) {
	f := startFakeVPS(t, "ssh-secret", installerHandler(t))
	s := panelServer(f)
	for k, v := range map[string]string{"role": "combined", "protocols": "wdtt,dnstt", "access": "nginx", "domain": "Panel.Example.org", "email": "", "dnsZone": "t.example.org", "dnsNs": "ns.example.org"} {
		s[k] = v
	}
	events, _, e := runPanel(t, "check", "", s)
	if e != nil {
		t.Fatal(e)
	}
	result := events.of("result")[0].Value.(map[string]any)
	if result["ok"] != true {
		t.Fatalf("check %v", result)
	}
	commands, _ := f.seen()
	commands[0] = plain(commands[0])
	want := "env WDTT_PROTOCOLS='wdtt,dnstt' WDTT_FRONTEND=nginx WDTT_DOMAIN='panel.example.org' WDTT_CERT_EMAIL='' "
	if !strings.Contains(commands[0], want) || !strings.Contains(commands[0], "WDTT_DNS_ZONE='t.example.org'") ||
		!strings.Contains(commands[0], "WDTT_DNS_NS='ns.example.org'") || !strings.Contains(commands[0], "bash /root/wdtt-install.sh check combined'") {
		t.Fatalf("check command %q", commands[0])
	}
	s["domain"] = "bad.example.org"
	events, _, e = runPanel(t, "check", "", s)
	result = events.of("result")[0].Value.(map[string]any)
	if e != nil || result["ok"] != false || !strings.Contains(result["text"].(string), "198.51.100.1") {
		t.Fatalf("failed check %v %v", result, e)
	}
}

func TestPanelRejectsUnsafeAnswers(t *testing.T) {
	f := startFakeVPS(t, "ssh-secret", installerHandler(t))
	base := panelServer(f)
	for k, v := range map[string]string{"role": "node", "protocols": "wdtt", "access": "local"} {
		base[k] = v
	}
	for key, value := range map[string]string{
		"role": "all", "protocols": "wdtt;reboot", "access": "apache", "dnsZone": "t.example.org;id", "dnsNs": "$(id)",
		"host": "127.0.0.1;id", "user": "root id", "sshPort": "70000",
	} {
		s := map[string]string{}
		for k, v := range base {
			s[k] = v
		}
		s[key] = value
		before, _ := f.seen()
		if _, _, e := runPanel(t, "install", "", s); e == nil {
			t.Fatalf("accepted %s=%q", key, value)
		}
		after, _ := f.seen()
		for _, command := range after[len(before):] {
			if strings.Contains(command, "wdtt-install.sh node") {
				t.Fatalf("installer ran with %s=%q", key, value)
			}
		}
	}
	for _, domain := range []string{"", "https://x.org", "x.org/path", "x.org:443", "a'b.org", "-x.org"} {
		s := map[string]string{}
		for k, v := range base {
			s[k] = v
		}
		s["access"], s["domain"] = "caddy", domain
		if _, _, e := runPanel(t, "check", "", s); e == nil {
			t.Fatalf("accepted domain %q", domain)
		}
	}
	s := map[string]string{}
	for k, v := range base {
		s[k] = v
	}
	s["access"], s["domain"], s["email"] = "nginx", "panel.example.org", "a@b.c'; id"
	if _, _, e := runPanel(t, "check", "", s); e == nil {
		t.Fatal("accepted email with quote")
	}
}

func TestPanelInstallStreamsStepsAndEndsWithStatus(t *testing.T) {
	f := startFakeVPS(t, "ssh-secret", installerHandler(t))
	s := panelServer(f)
	for k, v := range map[string]string{"role": "panel", "protocols": "", "access": "local"} {
		s[k] = v
	}
	events, raw, e := runPanel(t, "install", "", s)
	if e != nil {
		t.Fatal(e, raw)
	}
	var steps []string
	for _, event := range events.of("progress") {
		steps = append(steps, event.Text)
	}
	joined := strings.Join(steps, "|")
	if !strings.Contains(joined, "Установка пакетов|Сборка панели") || !strings.HasSuffix(joined, "Готово") {
		t.Fatalf("steps %q", joined)
	}
	if len(events.of("status")) != 1 || len(events.of("log")) == 0 {
		t.Fatal("install did not report status and log")
	}
	s["protocols"], s["role"] = "broken", "node"
	panelProtocols["broken"] = true
	defer delete(panelProtocols, "broken")
	_, raw, e = runPanel(t, "install", "", s)
	if e == nil || !strings.Contains(e.Error(), "error: build failed") {
		t.Fatalf("failed install: %v %s", e, raw)
	}
}

func TestPanelUninstallTargets(t *testing.T) {
	f := startFakeVPS(t, "ssh-secret", installerHandler(t))
	for _, target := range []string{"all", "panel", "node"} {
		if _, _, e := runPanel(t, "uninstall-"+target, "", panelServer(f)); e != nil {
			t.Fatal(e)
		}
		commands, _ := f.seen()
		found := false
		for _, command := range commands {
			found = found || strings.Contains(command, "bash /root/wdtt-install.sh uninstall "+target+"'")
		}
		if !found {
			t.Fatalf("no uninstall %s in %q", target, commands)
		}
	}
	if _, _, e := runPanel(t, "uninstall-everything", "", panelServer(f)); e == nil {
		t.Fatal("unknown target accepted")
	}
}

func TestPanelResolveKeepsOnlyIPv4(t *testing.T) {
	f := startFakeVPS(t, "ssh-secret", installerHandler(t))
	events, _, e := runPanel(t, "resolve", "Panel.Example.org", panelServer(f))
	if e != nil {
		t.Fatal(e)
	}
	got := events.of("result")[0].Value.([]any)
	if len(got) != 2 || got[0] != "203.0.113.7" {
		t.Fatalf("resolve %v", got)
	}
	commands, _ := f.seen()
	if !strings.Contains(plain(commands[0]), "getent ahostsv4 'panel.example.org'") {
		t.Fatalf("resolve command %q", commands[0])
	}
	if _, _, e := runPanel(t, "resolve", "x.org;reboot", panelServer(f)); e == nil {
		t.Fatal("unsafe name accepted")
	}
}

func TestPanelCredentialsStdin(t *testing.T) {
	for _, test := range []struct{ user, uid, want string }{
		{"root", "0", "boss\nNew-Pass 1\n"},
		{"debian", "1000", "ssh-secret\nboss\nNew-Pass 1\n"},
	} {
		handler := installerHandler(t)
		f := startFakeVPS(t, "ssh-secret", func(command, stdin string) (string, uint32) {
			if strings.Contains(plain(command), "bash -c 'id -u'") {
				return test.uid + "\n", 0
			}
			return handler(command, stdin)
		})
		s := panelServer(f)
		s["user"], s["panelUser"], s["panelPassword"] = test.user, "boss", "New-Pass 1"
		events, raw, e := runPanel(t, "credentials", "", s)
		if e != nil {
			t.Fatal(e)
		}
		commands, stdins := f.seen()
		found := false
		for i, command := range commands {
			if strings.Contains(command, "/usr/local/bin/wdtt-panel-credentials") {
				found = true
				if stdins[i] != test.want {
					t.Fatalf("%s stdin %q", test.user, stdins[i])
				}
			}
		}
		if !found || len(events.of("status")) != 1 {
			t.Fatal("credentials not set")
		}
		if strings.Contains(raw, "New-Pass 1") || strings.Contains(raw, "ssh-secret") {
			t.Fatal("secret in output")
		}
	}
	f := startFakeVPS(t, "ssh-secret", installerHandler(t))
	for _, bad := range [][2]string{{"", "x"}, {"a", ""}, {" a", "x"}, {"a", "x\ny"}, {"a", strings.Repeat("x", 257)}} {
		s := panelServer(f)
		s["panelUser"], s["panelPassword"] = bad[0], bad[1]
		if _, _, e := runPanel(t, "credentials", "", s); e == nil {
			t.Fatalf("accepted %q", bad)
		}
	}
}

func TestPanelTunnelForwardsToPanelPort(t *testing.T) {
	panel, e := net.Listen("tcp", "127.0.0.1:0")
	if e != nil {
		t.Fatal(e)
	}
	defer panel.Close()
	go http.Serve(panel, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, "panel "+r.URL.Path) }))
	_, panelPort, _ := net.SplitHostPort(panel.Addr().String())
	f := startFakeVPS(t, "ssh-secret", installerHandler(t))
	s := panelServer(f)
	s["panelPort"] = panelPort
	withTunnel(t, s, func(port int) {
		for i := 0; i < 3; i++ {
			response, e := http.Get(fmt.Sprintf("http://127.0.0.1:%d/Abc/", port))
			if e != nil {
				t.Fatal(e)
			}
			body, _ := io.ReadAll(response.Body)
			response.Body.Close()
			if string(body) != "panel /Abc/" {
				t.Fatalf("body %q", body)
			}
		}
	})
}

// withTunnel opens the panel tunnel as the window does, runs [use] with its local port, then closes the window's
// stdin and checks that the tunnel stops and frees the port.
func withTunnel(t *testing.T, s map[string]string, use func(port int)) {
	t.Helper()
	request, _ := json.Marshal(Request{Operation: "panel", Value: "tunnel", Server: s})
	reader, writer := io.Pipe()
	output, events := io.Pipe()
	done := make(chan error, 1)
	go func() { done <- Run(reader, events); events.Close() }()
	io.WriteString(writer, string(request)+"\n")
	lines := bufio.NewScanner(output)
	port := 0
	for port == 0 && lines.Scan() {
		var event Event
		json.Unmarshal(lines.Bytes(), &event)
		if event.Type == "error" {
			t.Fatal(event.Text)
		}
		if event.Type == "result" {
			port = int(event.Value.(map[string]any)["port"].(float64))
		}
	}
	go io.Copy(io.Discard, output)
	use(port)
	writer.Close()
	select {
	case e := <-done:
		if e != nil {
			t.Fatal(e)
		}
	case <-time.After(10 * time.Second):
		t.Fatal("tunnel kept running without its window")
	}
	if c, e := net.DialTimeout("tcp", fmt.Sprintf("127.0.0.1:%d", port), time.Second); e == nil {
		c.Close()
		t.Fatal("tunnel port still open")
	}
}

func TestPanelEnvNeverWaitsForTerminal(t *testing.T) {
	for _, s := range []map[string]string{
		{"protocols": "", "access": "local"},
		{"protocols": "wdtt", "access": "caddy", "domain": "p.example.org", "email": "me@example.org"},
	} {
		env, e := panelEnv(s)
		if e != nil {
			t.Fatal(e)
		}
		// Every question the installer asks has its answer: protocols, access, domain and email, DNS zone and NS.
		for _, name := range []string{"WDTT_PROTOCOLS=", "WDTT_FRONTEND=", "WDTT_DNS_ZONE=", "WDTT_DNS_NS="} {
			if !strings.Contains(env, name) {
				t.Fatalf("%s missing in %q", name, env)
			}
		}
		if s["access"] != "local" && (!strings.Contains(env, "WDTT_DOMAIN=") || !strings.Contains(env, "WDTT_CERT_EMAIL=")) {
			t.Fatalf("domain answers missing in %q", env)
		}
	}
}
