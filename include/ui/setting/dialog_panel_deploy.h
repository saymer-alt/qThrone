#pragma once
#include <QJsonObject>
#include <functional>

class QDialog;
class QWidget;

// Deploy of WDTT Panel on a VPS, screen by screen: the connection, what to install, protocols, access, domain,
// the compatibility check and the install itself; then the server's page with the panel link, login and removal.
// [save] stores the server (its SSH access and what is installed); a new server is stored once it has connected.
QDialog *OpenPanelDeploy(QWidget *parent, const QJsonObject &server, const std::function<void(const QJsonObject &)> &save);
QJsonObject NewPanelServer();
