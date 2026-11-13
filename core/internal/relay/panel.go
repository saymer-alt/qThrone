package relay

import (
	"encoding/json"
	"fmt"
	"io"
	"net"
	"regexp"
	"strconv"
	"strings"
	"time"

	"golang.org/x/crypto/ssh"
)

// The panel's own installer: every deploy step is one of its commands, so Rawline and the shell install the same way.
const panelInstaller = "https://raw.githubusercontent.com/ttrroyy/wdtt-panel/main/install.sh"

var panelProtocols = map[string]bool{
	"wdtt": true, "qwdtt": true, "wdtt-plus": true, "csqtt-2.1.9": true, "csqtt-2.5.0": true, "freeturn": true,
	"csqtt-2.5.3-community": true, "dnstt": true, "vaydns": true, "slipstream": true, "masterdns": true,
	"cottendns": true, "soar": true, "openflux": true,
}
var hostnamePattern = regexp.MustCompile(`^(?i)([a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?\.)+[a-z][a-z0-9-]{0,61}[a-z0-9]$`)
var emailPattern = regexp.MustCompile(`^[^\s@'"\\]+@[^\s@'"\\]+\.[^\s@'"\\]+$`)

// panelEnv is the installer's answers, so it never waits for a terminal.
func panelEnv(s map[string]string) (string, error) {
	var protocols []string
	for _, name := range strings.Split(s["protocols"], ",") {
		if name = strings.TrimSpace(name); name == "" {
			continue
		}
		if !panelProtocols[name] {
			return "", fmt.Errorf("Неизвестный протокол: %s", name)
		}
		protocols = append(protocols, name)
	}
	env := []string{"WDTT_PROTOCOLS=" + quote(strings.Join(protocols, ","))}
	switch s["access"] {
	case "local":
		env = append(env, "WDTT_FRONTEND=local")
	case "nginx", "caddy":
		if !hostnamePattern.MatchString(s["domain"]) {
			return "", fmt.Errorf("Укажите домен без схемы, порта и пути")
		}
		if s["email"] != "" && !emailPattern.MatchString(s["email"]) {
			return "", fmt.Errorf("Некорректный email")
		}
		env = append(env, "WDTT_FRONTEND="+s["access"], "WDTT_DOMAIN="+quote(strings.ToLower(s["domain"])), "WDTT_CERT_EMAIL="+quote(s["email"]))
	default:
		return "", fmt.Errorf("Выберите способ доступа к панели")
	}
	for key, name := range map[string]string{"dnsZone": "WDTT_DNS_ZONE", "dnsNs": "WDTT_DNS_NS"} {
		if s[key] != "" && !hostnamePattern.MatchString(s[key]) {
			return "", fmt.Errorf("Некорректное DNS-имя: %s", s[key])
		}
		env = append(env, name+"="+quote(strings.ToLower(s[key])))
	}
	return strings.Join(env, " "), nil
}

func panelRole(value string) (string, error) {
	if value != "panel" && value != "node" && value != "combined" {
		return "", fmt.Errorf("Выберите, что установить")
	}
	return value, nil
}

// panelFetch puts the current installer at /root/wdtt-install.sh; curl is added on images that lack it.
const panelFetch = "if ! command -v curl >/dev/null 2>&1; then apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq curl ca-certificates; fi; " +
	"curl -fsSL --retry 3 --max-time 120 " + panelInstaller + " -o /root/wdtt-install.sh"

func (p *protocol) panel(r Request) error {
	s := r.Server
	if !addressPattern.MatchString(s["host"]) || !userPattern.MatchString(s["user"]) {
		return fmt.Errorf("Укажите IP/домен и логин SSH")
	}
	if _, e := port(s, "sshPort"); e != nil {
		return e
	}
	if s["auth"] == "key" && s["key"] == "" {
		return fmt.Errorf("Загрузите SSH-ключ")
	}
	if s["auth"] != "key" && s["sshPassword"] == "" {
		return fmt.Errorf("Введите пароль SSH")
	}
	for _, k := range []string{"sshPassword", "key", "keyPassword", "sudoPassword", "panelPassword"} {
		p.secrets = append(p.secrets, s[k])
	}
	action := r.Value
	p.emit("progress", "Подключение к VPS", 2)
	// A known server must keep its key; only a server never seen before asks to be trusted.
	client, e := p.dial(s, s["hostKey"] == "")
	if e != nil {
		return e
	}
	defer client.Close()
	quiet := func(command string) (string, error) { return p.run(client, s, asRoot(command), 5*time.Minute, false) }
	logged := func(command string, timeout time.Duration) (string, error) {
		return p.run(client, s, asRoot(command), timeout, true)
	}
	status := func() error {
		output, e := quiet(panelFetch + " && bash /root/wdtt-install.sh status")
		if e != nil {
			return fmt.Errorf("Не удалось получить состояние VPS: %s", lastLine(output, e))
		}
		var value map[string]any
		if e = json.Unmarshal([]byte(lastLine(output, nil)), &value); e != nil {
			return fmt.Errorf("Установщик вернул неизвестный ответ")
		}
		p.emit("status", "", value)
		return nil
	}
	switch action {
	case "status":
		return status()
	case "resolve":
		// The VPS resolves the name the way the installer's check does.
		if !hostnamePattern.MatchString(r.ID) {
			return fmt.Errorf("Укажите домен без схемы, порта и пути")
		}
		output, _ := quiet("getent ahostsv4 " + quote(strings.ToLower(r.ID)) + " | awk '{print $1}' | sort -u")
		addresses := []string{}
		for _, line := range strings.Fields(output) {
			if ip := net.ParseIP(line); ip != nil && ip.To4() != nil {
				addresses = append(addresses, line)
			}
		}
		p.emit("result", "", addresses)
		return nil
	case "check", "install":
		role, e := panelRole(s["role"])
		if e != nil {
			return e
		}
		env, e := panelEnv(s)
		if e != nil {
			return e
		}
		if action == "check" {
			p.emit("progress", "Проверка совместимости", 10)
			output, e := quiet(panelFetch + " && env " + env + " bash /root/wdtt-install.sh check " + role)
			p.emit("result", "", map[string]any{"ok": e == nil, "text": strings.TrimSpace(output)})
			return nil
		}
		p.emit("progress", "Загрузка установщика", 3)
		if output, e := logged(panelFetch+" && env "+env+" bash /root/wdtt-install.sh "+role, 3*time.Hour); e != nil {
			return fmt.Errorf("Установка остановлена: %s", lastLine(output, e))
		}
		p.emit("progress", "Готово", 100)
		return status()
	case "uninstall-all", "uninstall-panel", "uninstall-node":
		target := strings.TrimPrefix(action, "uninstall-")
		p.emit("progress", "Удаление", 10)
		if output, e := logged(panelFetch+" && bash /root/wdtt-install.sh uninstall "+target, 30*time.Minute); e != nil {
			return fmt.Errorf("Удаление остановлено: %s", lastLine(output, e))
		}
		p.emit("progress", "Готово", 100)
		return status()
	case "credentials":
		if !validPanelCredential(s["panelUser"]) || !validPanelCredential(s["panelPassword"]) {
			return fmt.Errorf("Логин и пароль: без пробелов по краям и переводов строк, до 256 символов")
		}
		uid, e := quiet("id -u")
		if e != nil {
			return e
		}
		// Root reads the login straight away; sudo first takes its own password from stdin.
		input := s["panelUser"] + "\n" + s["panelPassword"] + "\n"
		if strings.TrimSpace(uid) != "0" {
			password := s["sudoPassword"]
			if password == "" {
				password = s["sshPassword"]
			}
			input = password + "\n" + input
		}
		output, e := p.runInput(client, asRootStdin("/usr/local/bin/wdtt-panel-credentials"), input, time.Minute)
		if e != nil {
			return fmt.Errorf("Не удалось сменить вход: %s", lastLine(output, e))
		}
		return status()
	case "tunnel":
		return p.panelTunnel(client, s)
	}
	return fmt.Errorf("Неизвестное действие")
}

func validPanelCredential(value string) bool {
	return value != "" && len(value) <= 256 && strings.TrimSpace(value) == value && !strings.ContainsAny(value, "\r\n\x00")
}

// lastLine is the installer's own message: its last non-empty output line, or the SSH error.
func lastLine(output string, e error) string {
	lines := strings.Split(strings.TrimSpace(output), "\n")
	if last := strings.TrimSpace(lines[len(lines)-1]); last != "" {
		return last
	}
	if e != nil {
		return e.Error()
	}
	return ""
}

// panelTunnel serves a panel that listens only on the VPS's 127.0.0.1 at a local port, through this SSH connection.
// Only the forward goes through SSH: the system's routes and Rawline's TUN stay as they are.
func (p *protocol) panelTunnel(client *ssh.Client, s map[string]string) error {
	target, e := strconv.Atoi(s["panelPort"])
	if e != nil || target < 1 || target > 65535 {
		return fmt.Errorf("Неизвестный порт панели")
	}
	// The panel's own port keeps the browser's login between openings; any free one when it is taken.
	listener, e := net.Listen("tcp", net.JoinHostPort("127.0.0.1", strconv.Itoa(target)))
	if e != nil {
		listener, e = net.Listen("tcp", "127.0.0.1:0")
	}
	if e != nil {
		return e
	}
	defer listener.Close()
	go func() { <-p.ctx.Done(); listener.Close() }()
	// Keeps an idle tunnel open through NAT and notices a dropped connection.
	go func() {
		for {
			select {
			case <-p.ctx.Done():
				return
			case <-time.After(30 * time.Second):
				if _, _, e := client.SendRequest("keepalive@openssh.com", true, nil); e != nil {
					listener.Close()
					return
				}
			}
		}
	}()
	p.emit("result", "", map[string]any{"port": listener.Addr().(*net.TCPAddr).Port})
	for {
		local, e := listener.Accept()
		if e != nil {
			if p.ctx.Err() != nil {
				return nil
			}
			return e
		}
		go func() {
			defer local.Close()
			remote, e := client.Dial("tcp", net.JoinHostPort("127.0.0.1", strconv.Itoa(target)))
			if e != nil {
				return
			}
			defer remote.Close()
			done := make(chan struct{}, 2)
			go func() { _, _ = io.Copy(remote, local); done <- struct{}{} }()
			go func() { _, _ = io.Copy(local, remote); done <- struct{}{} }()
			<-done
		}()
	}
}
