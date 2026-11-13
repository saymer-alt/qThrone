package relay

import (
	"bytes"
	"fmt"
	"io"
	"math"
	"net"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"

	"golang.org/x/crypto/ssh"
)

var addressPattern = regexp.MustCompile(`^[A-Za-z0-9.:_-]+$`)
var userPattern = regexp.MustCompile(`^[A-Za-z0-9_.-]+$`)

func port(s map[string]string, key string) (string, error) {
	n, e := strconv.Atoi(s[key])
	if e != nil || n < 1 || n > 65535 {
		return "", fmt.Errorf("Некорректный порт: %s", key)
	}
	return strconv.Itoa(n), nil
}

// sshAuth reads the server's SSH login: a key (with its passphrase) or a password.
func sshAuth(s map[string]string) (ssh.AuthMethod, error) {
	if s["auth"] != "key" {
		return ssh.Password(s["sshPassword"]), nil
	}
	var signer ssh.Signer
	var e error
	if s["keyPassword"] != "" {
		signer, e = ssh.ParsePrivateKeyWithPassphrase([]byte(s["key"]), []byte(s["keyPassword"]))
	} else {
		signer, e = ssh.ParsePrivateKey([]byte(s["key"]))
	}
	if e != nil {
		return nil, fmt.Errorf("Не удалось прочитать SSH-ключ")
	}
	return ssh.PublicKeys(signer), nil
}

// dial opens SSH to the server. The host key remembered at the first deploy is the only one
// accepted; only a deploy (trustNew) may ask the user to trust a server seen for the first time.
func (p *protocol) dial(s map[string]string, trustNew bool) (*ssh.Client, error) {
	if !trustNew && s["hostKey"] == "" {
		return nil, fmt.Errorf("Ключ SSH сервера не сохранён: сначала выполните деплой")
	}
	auth, e := sshAuth(s)
	if e != nil {
		return nil, e
	}
	cfg := &ssh.ClientConfig{User: s["user"], Auth: []ssh.AuthMethod{auth}, Timeout: 30 * time.Second, HostKeyCallback: func(host string, remote net.Addr, key ssh.PublicKey) error {
		fingerprint := ssh.FingerprintSHA256(key)
		if s["hostKey"] != "" {
			if fingerprint != s["hostKey"] {
				return fmt.Errorf("SSH-ключ сервера изменился")
			}
			return nil
		}
		p.emit("trust", fingerprint, nil)
		var answer string
		select {
		case answer = <-p.commands:
		case <-p.ctx.Done():
			return p.ctx.Err()
		case <-time.After(2 * time.Minute):
			return fmt.Errorf("SSH-ключ не подтверждён")
		}
		if answer != "approve" {
			return fmt.Errorf("SSH-ключ сервера не подтверждён")
		}
		p.emit("host-key", fingerprint, nil)
		// A rollback after a failed WDTT Plus update reconnects with the key just trusted.
		s["hostKey"] = fingerprint
		return nil
	}}
	address := net.JoinHostPort(strings.Trim(s["host"], "[]"), s["sshPort"])
	connection, e := (&net.Dialer{Timeout: 30 * time.Second}).DialContext(p.ctx, "tcp", address)
	if e != nil {
		return nil, e
	}
	_ = connection.SetDeadline(time.Now().Add(150 * time.Second))
	sshConnection, channels, requests, e := ssh.NewClientConn(connection, address, cfg)
	if e != nil {
		connection.Close()
		return nil, e
	}
	_ = connection.SetDeadline(time.Time{})
	client := ssh.NewClient(sshConnection, channels, requests)
	go func() { <-p.ctx.Done(); _ = client.Close() }()
	return client, nil
}

// run executes one command with the sudo password on its stdin. A logged run streams the
// output (secrets redacted) to the deploy console; a quiet one only returns it.
func (p *protocol) run(client *ssh.Client, s map[string]string, command string, timeout time.Duration, logged bool, stdinLines ...string) (string, error) {
	password := s["sudoPassword"]
	if password == "" {
		password = s["sshPassword"]
	}
	input := password + "\n"
	for _, line := range stdinLines {
		input += line + "\n"
	}
	var console *deployWriter
	if logged {
		console = &deployWriter{p: p, buffer: &bytes.Buffer{}, kind: s["kind"]}
	}
	return p.execute(client, command, input, timeout, console)
}

// runInput executes one command quietly with exactly the given stdin.
func (p *protocol) runInput(client *ssh.Client, command, input string, timeout time.Duration) (string, error) {
	return p.execute(client, command, input, timeout, nil)
}

func (p *protocol) execute(client *ssh.Client, command, input string, timeout time.Duration, console *deployWriter) (string, error) {
	session, e := client.NewSession()
	if e != nil {
		return "", e
	}
	defer session.Close()
	stdin, e := session.StdinPipe()
	if e != nil {
		return "", e
	}
	var output *bytes.Buffer
	var writer io.Writer
	if console != nil {
		output = console.buffer
		writer = console
	} else {
		output = &bytes.Buffer{}
		writer = &limitedBuffer{buffer: output}
	}
	session.Stdout = writer
	session.Stderr = writer
	if e = session.Start(command); e != nil {
		return "", e
	}
	go func() {
		_, _ = io.WriteString(stdin, input)
		_ = stdin.Close()
	}()
	done := make(chan error, 1)
	go func() { done <- session.Wait() }()
	select {
	case e = <-done:
	case <-time.After(timeout):
		_ = session.Close()
		e = fmt.Errorf("Превышено время выполнения команды на VPS")
	case <-p.ctx.Done():
		_ = session.Close()
		e = p.ctx.Err()
	}
	if console != nil {
		console.flush()
		console.mu.Lock()
		defer console.mu.Unlock()
	} else {
		writer.(*limitedBuffer).mu.Lock()
		defer writer.(*limitedBuffer).mu.Unlock()
	}
	return output.String(), e
}

// limitedBuffer keeps a quiet command's output without letting a server fill memory.
type limitedBuffer struct {
	mu     sync.Mutex
	buffer *bytes.Buffer
}

func (w *limitedBuffer) Write(b []byte) (int, error) {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.buffer.Len() < 8*1024*1024 {
		w.buffer.Write(b)
	}
	return len(b), nil
}

// asRoot runs a command as root: directly for root, through sudo (password on stdin) otherwise.
func asRoot(command string) string {
	return "if [ \"$(id -u)\" = 0 ]; then bash -c " + quote(command) + "; else sudo -S -p '' bash -c " + quote(command) + "; fi"
}

type deployWriter struct {
	p       *protocol
	buffer  *bytes.Buffer
	mu      sync.Mutex
	pending string
	kind    string
}

func (w *deployWriter) Write(b []byte) (int, error) {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.buffer.Len() < 2*1024*1024 {
		w.buffer.Write(b)
	}
	w.pending += string(b)
	for {
		at := strings.IndexByte(w.pending, '\n')
		if at < 0 {
			break
		}
		line := w.pending[:at+1]
		w.p.emit("log", line, nil)
		// The panel installer names its phases with "==> ".
		if step, ok := strings.CutPrefix(strings.TrimSpace(line), "==> "); ok {
			w.p.emit("progress", step, nil)
		}
		parts := strings.SplitN(strings.TrimSpace(line), "|", 3)
		prefix := "WDTT_PROGRESS"
		if w.kind == "csqtt" {
			prefix = "CSQTT_PROGRESS"
		}
		if len(parts) == 3 && parts[0] == prefix {
			if value, e := strconv.ParseFloat(parts[1], 64); e == nil && !math.IsNaN(value) && !math.IsInf(value, 0) && value >= 0 && value <= 1 {
				w.p.emit("progress", parts[2], int(value*99))
			}
		}
		w.pending = w.pending[at+1:]
	}
	if len(w.pending) > 1024*1024 {
		w.pending = ""
		w.p.emit("log", "[Слишком длинная строка установщика]\n", nil)
	}
	return len(b), nil
}
func (w *deployWriter) flush() {
	w.mu.Lock()
	defer w.mu.Unlock()
	if w.pending != "" {
		w.p.emit("log", w.pending, nil)
		w.pending = ""
	}
}

// asRootStdin runs a command that reads stdin as root; for a non-root login sudo takes the
// first stdin line as its password.
func asRootStdin(command string) string {
	return "if [ \"$(id -u)\" = 0 ]; then " + command + "; else sudo -S -p '' " + command + "; fi"
}
