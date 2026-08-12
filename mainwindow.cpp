#include "mainwindow.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QKeyEvent>
#include <QDebug>

constexpr int NodeIdRole = Qt::UserRole + 1;

// =======================================================================
// CERTIFICATE VALIDATION DIALOG
// =======================================================================
CertValidationDialog::CertValidationDialog(const QByteArray &derData, const QString &certFileName, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Certificate Validation");
    resize(550, 600);

    setStyleSheet("QDialog { background-color: #F0F0F0; color: #000000; font-family: 'Segoe UI', sans-serif; }"
                  "QGroupBox { font-weight: bold; margin-top: 15px; border: 1px solid #CCCCCC; padding-top: 10px; }"
                  "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px; }"
                  "QLabel, QCheckBox, QPushButton { color: #000000; }"
                  "QTreeWidget, QTableWidget, QLineEdit { background-color: #FFFFFF; color: #000000; border: 1px solid #AAAAAA; }");

    QSslCertificate cert(derData, QSsl::Der);
    QString commonName = cert.subjectInfo(QSslCertificate::CommonName).join(", ");
    if (commonName.isEmpty()) commonName = certFileName;

    auto *mainLayout = new QVBoxLayout(this);
    auto *lblTitle = new QLabel(QString("The certificate of server '%1' was rejected.").arg(commonName));
    lblTitle->setWordWrap(true);
    mainLayout->addWidget(lblTitle);

    auto *txtStatus = new QLineEdit("BadCertificateUntrusted");
    txtStatus->setReadOnly(true);
    txtStatus->setStyleSheet("color: #D80000; font-weight: bold; background: #FFDDDD; padding: 5px;");
    mainLayout->addWidget(txtStatus);

    auto *grpChain = new QGroupBox("Certificate Chain");
    auto *lChain = new QVBoxLayout(grpChain);
    auto *tblChain = new QTableWidget(1, 2);
    tblChain->setHorizontalHeaderLabels({"Name", "Trust Status"});
    tblChain->horizontalHeader()->setStretchLastSection(true);
    tblChain->verticalHeader()->setVisible(false);
    tblChain->setItem(0, 0, new QTableWidgetItem(commonName));
    tblChain->setItem(0, 1, new QTableWidgetItem("Untrusted"));
    tblChain->setFixedHeight(70);
    lChain->addWidget(tblChain);
    mainLayout->addWidget(grpChain);

    auto *grpDetails = new QGroupBox("Certificate Details");
    auto *lDetails = new QVBoxLayout(grpDetails);
    auto *tree = new QTreeWidget();
    tree->setHeaderLabels({"Attribute", "Value"});
    tree->header()->resizeSection(0, 150);

    auto addNode = [](QTreeWidgetItem *parent, const QString &attr, const QStringList &vals) {
        new QTreeWidgetItem(parent, {attr, vals.join(", ")});
    };

    auto *subj = new QTreeWidgetItem(tree, {"Subject", ""});
    subj->setBackground(0, QColor("#888888")); subj->setForeground(0, QColor("white"));
    subj->setBackground(1, QColor("#888888"));
    addNode(subj, "Common Name", cert.subjectInfo(QSslCertificate::CommonName));
    addNode(subj, "Organization", cert.subjectInfo(QSslCertificate::Organization));
    addNode(subj, "OrganizationUnit", cert.subjectInfo(QSslCertificate::OrganizationalUnitName));
    addNode(subj, "Locality", cert.subjectInfo(QSslCertificate::LocalityName));
    addNode(subj, "State", cert.subjectInfo(QSslCertificate::StateOrProvinceName));
    addNode(subj, "Country", cert.subjectInfo(QSslCertificate::CountryName));
    subj->setExpanded(true);

    auto *iss = new QTreeWidgetItem(tree, {"Issuer", ""});
    iss->setBackground(0, QColor("#888888")); iss->setForeground(0, QColor("white"));
    iss->setBackground(1, QColor("#888888"));
    addNode(iss, "Common Name", cert.issuerInfo(QSslCertificate::CommonName));
    addNode(iss, "Organization", cert.issuerInfo(QSslCertificate::Organization));
    addNode(iss, "OrganizationUnit", cert.issuerInfo(QSslCertificate::OrganizationalUnitName));
    addNode(iss, "Locality", cert.issuerInfo(QSslCertificate::LocalityName));
    addNode(iss, "State", cert.issuerInfo(QSslCertificate::StateOrProvinceName));
    addNode(iss, "Country", cert.issuerInfo(QSslCertificate::CountryName));
    iss->setExpanded(true);

    lDetails->addWidget(tree);

    auto *lTrust = new QHBoxLayout();
    lTrust->addStretch();
    auto *btnTrust = new QPushButton("Trust Server Certificate");
    lTrust->addWidget(btnTrust);
    lDetails->addLayout(lTrust);
    mainLayout->addWidget(grpDetails);

    auto *lBottom = new QHBoxLayout();
    auto *chkTemp = new QCheckBox("Accept the server certificate temporarily for this session");
    chkTemp->setEnabled(false);
    auto *btnContinue = new QPushButton("Continue");
    auto *btnCancel = new QPushButton("Cancel");

    lBottom->addWidget(chkTemp);
    lBottom->addStretch();
    lBottom->addWidget(btnContinue);
    lBottom->addWidget(btnCancel);
    mainLayout->addLayout(lBottom);

    connect(btnTrust, &QPushButton::clicked, [=]() {
        m_trusted = true;
        lblTitle->setText(QString("The certificate of server '%1' was validated successfully.").arg(commonName));
        txtStatus->setText("Good");
        txtStatus->setStyleSheet("color: #008800; font-weight: bold; background: #EEFFEE; padding: 5px;");
        tblChain->item(0, 1)->setText("Trusted");
        btnTrust->setEnabled(false);
    });

    connect(btnContinue, &QPushButton::clicked, this, &QDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

// =======================================================================
// MAIN WINDOW IMPLEMENTATION
// =======================================================================

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    initDatabase(); // Spin up Black Box SQL

    // Initialize Timers BEFORE calling setupUi() to prevent SIGSEGV
    standbyTimer = new QTimer(this);
    standbyTimer->setSingleShot(true);
    connect(standbyTimer, &QTimer::timeout, this, &MainWindow::setToStandby);

    alertFlashTimer = new QTimer(this);
    alertFlashTimer->setInterval(400);
    connect(alertFlashTimer, &QTimer::timeout, this, &MainWindow::toggleAlertFlash);

    generalTimeoutTimer = new QTimer(this);
    generalTimeoutTimer->setSingleShot(true);
    connect(generalTimeoutTimer, &QTimer::timeout, this, &MainWindow::onGeneralAlarmTimeout);

    reconnectTimer = new QTimer(this);
    connect(reconnectTimer, &QTimer::timeout, this, &MainWindow::onConnectOpcUa);

    // Watchdog Timer to prevent application freezes
    systemHealthWatchdog = new QTimer(this);
    systemHealthWatchdog->setInterval(2000);
    connect(systemHealthWatchdog, &QTimer::timeout, this, &MainWindow::kickWatchdog);
    systemHealthWatchdog->start();

    // PLC Watchdog Heartbeat
    hmiHeartbeatTimer = new QTimer(this);
    hmiHeartbeatTimer->setInterval(1000);
    connect(hmiHeartbeatTimer, &QTimer::timeout, this, &MainWindow::sendHmiHeartbeat);

    // Now safe to build UI
    setupUi();
    setupTheme();

    this->setWindowTitle("FUSCHWOLF SYSTEM");

    setToStandby();

    // MQTT Authentication Hook
    mqttClient = new QMqttClient(this);
    mqttClient->setHostname("127.0.0.1");
    mqttClient->setPort(1883);

    connect(mqttClient, &QMqttClient::messageReceived, this, &MainWindow::onMqttMessageReceived);
    connect(mqttClient, &QMqttClient::stateChanged, this, [this](QMqttClient::ClientState state) {
        if (state == QMqttClient::Connected) {
            mqttClient->subscribe(QLatin1String("fw3011/maint/key"));
        }
    });
    mqttClient->connectToHost();

    // USB Physical Auth Hook
    usbWatcher = new QFileSystemWatcher(this);
    QString userDir = QDir::homePath();
    QString runMedia = QString("/run/media/%1").arg(QFileInfo(userDir).fileName());
    QString mediaDir = QString("/media/%1").arg(QFileInfo(userDir).fileName());

    if (QDir(runMedia).exists()) usbWatcher->addPath(runMedia);
    if (QDir(mediaDir).exists()) usbWatcher->addPath(mediaDir);
    usbWatcher->addPath("/run/media");

    connect(usbWatcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::checkForServiceDrive);

    auto *usbPollTimer = new QTimer(this);
    connect(usbPollTimer, &QTimer::timeout, this, &MainWindow::checkForServiceDrive);
    usbPollTimer->start(2000);

    appendLog("FUSCHWOLF SYSTEM KERNEL STARTED.", false, "SYSTEM");
}

MainWindow::~MainWindow() {
    clearHmiHeartbeat();

    if (mActiveNode) mActiveNode->deleteLater();

    for (QOpcUaNode* node : mMonitoredNodes) {
        if (node) node->deleteLater();
    }
    mMonitoredNodes.clear();

    if (mOpcUaClient) {
        mOpcUaClient->disconnectFromEndpoint();
        mOpcUaClient->deleteLater();
    }

    for(StateMapUI* tag : m_dynamicTags) {
        delete tag;
    }
    m_dynamicTags.clear();

    appendLog("SYSTEM SHUTDOWN INITIATED.", false, "SYSTEM");
}

// =======================================================================
// SQLITE BLACK BOX ARCHITECTURE
// =======================================================================

void MainWindow::initDatabase() {
    QString pkiDir = QDir::homePath() + "/.larc_system";
    QDir().mkpath(pkiDir);

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(pkiDir + "/fw3011_telemetry.db");

    if (!db.open()) {
        qDebug() << "Failed to connect to SQLite Database:" << db.lastError().text();
        return;
    }

    QSqlQuery query;
    // Audit Logs
    query.exec("CREATE TABLE IF NOT EXISTS SystemLogs (id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP, type TEXT, message TEXT)");
    // Header Nodes
    query.exec("CREATE TABLE IF NOT EXISTS HeaderSettings (id INTEGER PRIMARY KEY CHECK (id = 1), plcNode TEXT, railNode TEXT, interlockNode TEXT, ramNode TEXT, heartbeatNode TEXT)");
    // Dynamic OPC UA Tag Arrays
    query.exec("CREATE TABLE IF NOT EXISTS DynamicTags (id INTEGER PRIMARY KEY AUTOINCREMENT, nodeId TEXT, title TEXT, subtitle TEXT, bgColor TEXT, textColor TEXT)");
}

void MainWindow::appendLog(const QString &msg, bool isError, const QString &logType) {
    if (opcLogPanel) {
        if (isError) {
            opcLogPanel->append(QString("<span style='color:#FF3333;'>%1</span>").arg(msg));
        } else {
            opcLogPanel->append(msg);
        }
    }

    // Write to SQLite Black Box
    QSqlQuery query;
    query.prepare("INSERT INTO SystemLogs (type, message) VALUES (?, ?)");
    query.addBindValue(isError ? "ERROR" : logType);
    query.addBindValue(msg);
    query.exec();
}

void MainWindow::kickWatchdog() {
    // If main thread freezes, this QTimer stops ticking.
}

// ---------------------------------------------------------
// HEARTBEAT WRITERS
// ---------------------------------------------------------
void MainWindow::sendHmiHeartbeat() {
    if (!mOpcUaClient || mOpcUaClient->state() != QOpcUaClient::ClientState::Connected) return;
    if (activeHeartbeatNode.isEmpty()) return;

    QOpcUaNode *node = mOpcUaClient->node(activeHeartbeatNode);
    if (node) {
        node->writeAttribute(QOpcUa::NodeAttribute::Value, QVariant(true));
        node->deleteLater();
    }
}

void MainWindow::clearHmiHeartbeat() {
    if (!mOpcUaClient || mOpcUaClient->state() != QOpcUaClient::ClientState::Connected) return;
    if (activeHeartbeatNode.isEmpty()) return;

    QOpcUaNode *node = mOpcUaClient->node(activeHeartbeatNode);
    if (node) {
        node->writeAttribute(QOpcUa::NodeAttribute::Value, QVariant(false));
        node->deleteLater();
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == diagnosticConsole) {
        if (event->type() == QEvent::KeyPress ||
            event->type() == QEvent::KeyRelease ||
            event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonDblClick) {
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::setupUi() {
    auto *centralWidget = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(createTopHeader());

    mainStack = new QStackedWidget(this);

    operationModePage = createOperationModeDisplay();
    android11Page = createAndroid11();
    frame1Page = createFrame1();

    mainStack->addWidget(operationModePage);
    mainStack->addWidget(android11Page);
    mainStack->addWidget(frame1Page);

    rootLayout->addWidget(mainStack);
    setCentralWidget(centralWidget);

    mainStack->setCurrentIndex(0);
}

QWidget* MainWindow::createHeaderStatusItem(QLabel** dotLabel, QLabel** textLabel, const QString& defaultText, const QString& defaultColor) {
    auto *container = new QWidget(this);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(5, 0, 15, 0);
    layout->setSpacing(5);

    *dotLabel = new QLabel("●", container);
    (*dotLabel)->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;").arg(defaultColor));

    *textLabel = new QLabel(defaultText, container);
    (*textLabel)->setStyleSheet("color: #AAAAAA; font-family: 'Consolas', monospace; font-size: 13px; font-weight: bold; letter-spacing: 1px;");

    layout->addWidget(*dotLabel);
    layout->addWidget(*textLabel);
    return container;
}

QWidget* MainWindow::createTopHeader() {
    auto *header = new QWidget(this);
    header->setObjectName("TopHeaderBar");
    header->setFixedHeight(45);

    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(15, 0, 15, 0);

    layout->addWidget(createHeaderStatusItem(&hdrPlcDot, &hdrPlcText, "PLC NO AVAIL", "#555555"));
    layout->addWidget(createHeaderStatusItem(&hdrRailDot, &hdrRailText, "RAIL POS NO AVAIL", "#555555"));
    layout->addWidget(createHeaderStatusItem(&hdrIntLockDot, &hdrIntLockText, "INTERLOCK NO AVAIL", "#555555"));
    layout->addWidget(createHeaderStatusItem(&hdrRamDot, &hdrRamText, "RAM PRESSURE NO AVAIL", "#555555"));

    layout->addStretch();

    headerMiniModeLabel = new QLabel("STANDBY", header);
    headerMiniModeLabel->setStyleSheet("background-color: #333333; color: white; padding: 4px 10px; border-radius: 3px; font-weight: bold; font-family: 'Segoe UI'; font-size: 12px; margin-right: 15px;");
    layout->addWidget(headerMiniModeLabel);

    headerClockLabel = new QLabel(QDateTime::currentDateTime().toString("MMM d  ·  hh:mm"), header);
    headerClockLabel->setStyleSheet("color: #888888; font-family: 'Consolas', monospace; font-size: 13px; font-weight: bold; margin-right: 15px;");
    layout->addWidget(headerClockLabel);

    headerClockTimer = new QTimer(this);
    connect(headerClockTimer, &QTimer::timeout, this, [this]() {
        headerClockLabel->setText(QDateTime::currentDateTime().toString("MMM d  ·  hh:mm"));
    });
    headerClockTimer->start(1000);

    auto *abortBtn = new QPushButton("⭕ ABORT", header);
    abortBtn->setStyleSheet("background-color: transparent; border: 1px solid #FF3333; color: #FFFFFF; font-weight: 900; padding: 5px 15px; border-radius: 4px; letter-spacing: 2px; font-family: 'Segoe UI';");
    abortBtn->setCursor(Qt::PointingHandCursor);
    layout->addWidget(abortBtn);

    // =========================================================
    // NEW: EMERGENCY ABORT FIRING LOGIC
    // =========================================================
    connect(abortBtn, &QPushButton::clicked, this, [this]() {
        if (!mOpcUaClient || mOpcUaClient->state() != QOpcUaClient::ClientState::Connected) {
            appendLog("[ERROR] Cannot trigger ABORT: PLC is Offline.", true, "SAFETY");
            return;
        }

        QString abortNodeId;

        // Dynamically find whichever Node ID is currently mapped to "ABORT" in your SQLite config
        for (auto it = mNodeToStateConfigMap.cbegin(); it != mNodeToStateConfigMap.cend(); ++it) {
            if (it.value().displayText.toUpper() == "ABORT") {
                abortNodeId = it.key();
                break;
            }
        }

        // Fire the True bit to the PLC
        if (!abortNodeId.isEmpty()) {
            QOpcUaNode *node = mOpcUaClient->node(abortNodeId);
            if (node) {
                node->writeAttribute(QOpcUa::NodeAttribute::Value, QVariant(true));
                node->deleteLater();
                appendLog("[EMERGENCY] HARDWARE ABORT TRIGGERED BY OPERATOR!", true, "SAFETY");
            }
        } else {
            appendLog("[ERROR] Cannot trigger ABORT: Tag is not mapped in Settings.", true, "SAFETY");
        }
    });

    return header;
}

void MainWindow::resetHeaderToNoAvail() {
    hdrPlcText->setText("PLC NO AVAIL");
    hdrPlcDot->setStyleSheet("color: #555555; font-size: 14px; font-weight: bold;");

    hdrRailText->setText("RAIL POS NO AVAIL");
    hdrRailDot->setStyleSheet("color: #555555; font-size: 14px; font-weight: bold;");

    hdrIntLockText->setText("INTERLOCK NO AVAIL");
    hdrIntLockDot->setStyleSheet("color: #555555; font-size: 14px; font-weight: bold;");

    hdrRamText->setText("RAM PRESSURE NO AVAIL");
    hdrRamDot->setStyleSheet("color: #555555; font-size: 14px; font-weight: bold;");
}

QWidget* MainWindow::createFrame1() {
    auto *view = new QWidget(this);
    view->setObjectName("MainBackground");
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(40, 40, 40, 40);

    diagnosticConsole = new QTermWidget(0, view);
    diagnosticConsole->setWorkingDirectory(QDir::homePath());
    diagnosticConsole->setColorScheme("WhiteOnBlack");
    diagnosticConsole->setTerminalFont(QFont("Consolas", 11));
    diagnosticConsole->setScrollBarPosition(QTermWidget::ScrollBarRight);
    diagnosticConsole->installEventFilter(this);
    diagnosticConsole->setFocusPolicy(Qt::NoFocus);
    diagnosticConsole->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    layout->addWidget(diagnosticConsole);
    diagnosticConsole->startShellProgram();

    QTimer::singleShot(300, this, [this]() {
        diagnosticConsole->sendText("system_diagnose() { echo 'SYSTEM DIAGNOSTICS'; echo 'AWAITING SERVICE DRIVE INSERTION...'; while true; do sleep 1000; done; }\n");
        QTimer::singleShot(100, this, [this]() {
            diagnosticConsole->sendText("clear\n");
            QTimer::singleShot(100, this, [this]() { diagnosticConsole->sendText("system_diagnose\n"); });
        });
    });
    return view;
}

QWidget* MainWindow::createAndroid11() {
    auto *view = new QWidget(this);
    auto *mainLayout = new QHBoxLayout(view);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *sidebar = new QWidget(view);
    sidebar->setObjectName("Sidebar");
    sidebar->setFixedWidth(270);
    auto *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 15, 0, 15);
    sidebarLayout->setSpacing(10);
    sidebarLayout->setAlignment(Qt::AlignTop);

    auto *menuToggleBtn = new QPushButton("≡  SYSTEM NAVIGATION", sidebar);
    menuToggleBtn->setObjectName("MenuToggleBtn");
    menuToggleBtn->setFlat(true);
    sidebarLayout->addWidget(menuToggleBtn);
    sidebarLayout->addSpacing(5);

    auto *sidebarContent = new QWidget(sidebar);
    auto *sidebarContentLayout = new QVBoxLayout(sidebarContent);
    sidebarContentLayout->setContentsMargins(0, 0, 0, 0);
    sidebarContentLayout->setSpacing(15);
    sidebarContentLayout->setAlignment(Qt::AlignTop);

    auto *operatorCard = new QFrame(sidebarContent);
    operatorCard->setObjectName("OperatorCard");
    auto *cardLayout = new QVBoxLayout(operatorCard);
    cardLayout->setContentsMargins(15, 12, 15, 12);
    cardLayout->setSpacing(4);

    auto *cardHeader = new QLabel("AUTHENTICATED OPERATOR", operatorCard);
    cardHeader->setObjectName("CardHeaderStyle");
    techNameLabel = new QLabel("● AWAITING DATA...", operatorCard);
    techNameLabel->setObjectName("TechNameStyle");
    techTitleDivisionLabel = new QLabel("Title: --\nDiv: --", operatorCard);
    techTitleDivisionLabel->setObjectName("TechDetailStyle");

    cardLayout->addWidget(cardHeader);
    cardLayout->addWidget(techNameLabel);
    cardLayout->addWidget(techTitleDivisionLabel);
    sidebarContentLayout->addWidget(operatorCard);

    auto *contentArea = new QWidget(view);
    contentArea->setObjectName("MainBackground");
    auto *contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    auto *maintBanner = new QLabel(" MAINTENANCE MODE ACTIVE️", contentArea);
    maintBanner->setStyleSheet("background-color: #D85A00; color: #111111; font-weight: 900; font-family: 'Segoe UI', sans-serif; padding: 6px; font-size: 12px; letter-spacing: 2px;");
    maintBanner->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(maintBanner);

    auto *innerContentStack = new QStackedWidget(contentArea);

    nativeConsole = new QTermWidget(0, innerContentStack);
    nativeConsole->setWorkingDirectory(QDir::homePath());
    nativeConsole->setColorScheme("WhiteOnBlack");
    nativeConsole->setTerminalFont(QFont("Consolas", 11));
    nativeConsole->setScrollBarPosition(QTermWidget::ScrollBarRight);
    nativeConsole->startShellProgram();

    QTimer::singleShot(500, this, [this]() {
        nativeConsole->sendText("clear && echo -e '\\033[1;33m KERNEL ACTIVE\\033[0m'\n");
    });
    innerContentStack->addWidget(nativeConsole);

    QWidget* opcManagerPage = createOpcUaManager();
    innerContentStack->addWidget(opcManagerPage);

    QWidget* settingsPage = createSettingsPage();
    innerContentStack->addWidget(settingsPage);

    contentLayout->addWidget(innerContentStack);

    auto *navContainer = new QWidget(sidebarContent);
    auto *navLayout = new QVBoxLayout(navContainer);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(0);
    navLayout->setAlignment(Qt::AlignTop);

    QStringList navItems = {"TERMINAL", "SYSTEM MANAGER", "SETTINGS", "<< BACK TO OP MODE"};
    for (const QString &itemText : navItems) {
        auto *btn = new QPushButton(itemText, navContainer);
        btn->setFlat(true);
        navLayout->addWidget(btn);

        if (itemText == "TERMINAL") {
            btn->setObjectName("NavButton");
            connect(btn, &QPushButton::clicked, [=]() { innerContentStack->setCurrentIndex(0); });
        } else if (itemText == "SYSTEM MANAGER") {
            btn->setObjectName("NavButton");
            connect(btn, &QPushButton::clicked, [=]() { innerContentStack->setCurrentIndex(1); });
        } else if (itemText == "SETTINGS") {
            btn->setObjectName("NavButton");
            connect(btn, &QPushButton::clicked, [=]() { innerContentStack->setCurrentIndex(2); });
        } else if (itemText == "<< BACK TO OP MODE") {
            btn->setObjectName("NavButtonExit");
            connect(btn, &QPushButton::clicked, this, [this]() {
                if (m_isUsbAuthenticated || m_mqttMaintState) {
                    appendLog("[SECURITY] Cannot exit Maintenance Mode while physical USB or Key is inserted.", true);
                } else {
                    mainStack->setCurrentIndex(0);
                }
            });
        }
    }

    sidebarContentLayout->addWidget(navContainer);
    sidebarLayout->addWidget(sidebarContent);

    connect(menuToggleBtn, &QPushButton::clicked, [=]() {
        if (sidebarContent->isVisible()) {
            sidebarContent->setVisible(false);
            sidebar->setFixedWidth(50);
            menuToggleBtn->setText("≡");
            menuToggleBtn->setStyleSheet("text-align: center; padding: 10px 0px;");
        } else {
            sidebarContent->setVisible(true);
            sidebar->setFixedWidth(270);
            menuToggleBtn->setText("≡  SYSTEM NAVIGATION");
            menuToggleBtn->setStyleSheet("");
        }
    });

    mainLayout->addWidget(sidebar);
    mainLayout->addWidget(contentArea);

    return view;
}

QWidget* MainWindow::createOpcUaManager() {
    auto *view = new QWidget(this);
    view->setObjectName("OpcManagerBg");
    auto *mainLayout = new QVBoxLayout(view);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    auto *connectBar = new QWidget(view);
    connectBar->setObjectName("ConnectBar");
    connectBar->setFixedHeight(40);
    auto *cbLayout = new QHBoxLayout(connectBar);
    cbLayout->setContentsMargins(10, 0, 10, 0);

    opcEndpointInput = new QLineEdit(connectBar);
    opcEndpointInput->setObjectName("EndpointInput");
    opcEndpointInput->setText("opc.tcp://192.168.0.1:4840");

    btnOpcConnect = new QPushButton("Connect", connectBar);
    btnOpcConnect->setObjectName("BtnOpcConnect");

    btnOpcDisconnect = new QPushButton("Disconnect", connectBar);
    btnOpcDisconnect->setObjectName("BtnOpcDisconnect");
    btnOpcDisconnect->setEnabled(false);

    cbLayout->addWidget(opcEndpointInput, 1);
    cbLayout->addWidget(btnOpcConnect);
    cbLayout->addWidget(btnOpcDisconnect);

    auto *splitter = new QSplitter(Qt::Horizontal, view);
    auto *treeWidgetContainer = new QWidget(splitter);
    auto *treeLayout = new QVBoxLayout(treeWidgetContainer);
    treeLayout->setContentsMargins(0, 0, 0, 0);

    opcBrowseTree = new QTreeView(treeWidgetContainer);
    opcBrowseTree->setObjectName("OpcBrowseTree");
    opcTreeModel = new QStandardItemModel(0, 2, this);
    opcTreeModel->setHeaderData(0, Qt::Horizontal, "DisplayName");
    opcTreeModel->setHeaderData(1, Qt::Horizontal, "BrowseName");
    opcBrowseTree->setModel(opcTreeModel);
    opcBrowseTree->header()->setDefaultSectionSize(180);

    connect(opcBrowseTree, &QTreeView::expanded, this, &MainWindow::treeItemExpanded);
    connect(opcBrowseTree, &QTreeView::clicked, this, &MainWindow::treeItemClicked);

    treeLayout->addWidget(opcBrowseTree);

    auto *attrWidgetContainer = new QWidget(splitter);
    auto *attrLayout = new QVBoxLayout(attrWidgetContainer);
    attrLayout->setContentsMargins(0, 0, 0, 0);

    auto *attrHeader = new QLabel("Attributes", attrWidgetContainer);
    attrHeader->setObjectName("AttrHeader");

    opcAttributeTable = new QTableWidget(0, 2, attrWidgetContainer);
    opcAttributeTable->setObjectName("OpcAttributeTable");
    opcAttributeTable->setHorizontalHeaderLabels(QStringList() << "Attribute" << "Value");
    opcAttributeTable->horizontalHeader()->setStretchLastSection(true);
    opcAttributeTable->verticalHeader()->setVisible(false);
    opcAttributeTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    attrLayout->addWidget(attrHeader);
    attrLayout->addWidget(opcAttributeTable);

    splitter->addWidget(treeWidgetContainer);
    splitter->addWidget(attrWidgetContainer);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    opcLogPanel = new QTextEdit(view);
    opcLogPanel->setObjectName("OpcLogPanel");
    opcLogPanel->setFixedHeight(120);
    opcLogPanel->setReadOnly(true);
    opcLogPanel->setText("System Manager Initialized. Awaiting OPC UA connection...");

    mainLayout->addWidget(connectBar);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addWidget(opcLogPanel);

    connect(btnOpcConnect, &QPushButton::clicked, this, &MainWindow::onConnectOpcUa);
    connect(btnOpcDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectOpcUa);

    return view;
}

QWidget* MainWindow::createSettingsPage() {
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background-color: #2A2C2F; }");

    auto *view = new QWidget(scrollArea);
    view->setObjectName("OpcManagerBg");
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(20);

    auto *headerTitle = new QLabel(" PLC MAPPING", view);
    headerTitle->setStyleSheet("color: #D85A00; font-size: 14px; font-weight: bold; font-family: 'Segoe UI';");
    layout->addWidget(headerTitle);

    auto *headerSettingsLayout = new QHBoxLayout();
    headerSettingsLayout->setSpacing(10);
    QString nodeStyle = "background-color: #111111; color: #00FF00; border: 1px solid #444444; padding: 5px; font-family: 'Consolas'; width: 75px;";

    inputPlcNode = new QLineEdit(view);
    inputPlcNode->setStyleSheet(nodeStyle);

    inputRailNode = new QLineEdit(view);
    inputRailNode->setStyleSheet(nodeStyle);

    inputInterlockNode = new QLineEdit(view);
    inputInterlockNode->setStyleSheet(nodeStyle);

    inputRamPressureNode = new QLineEdit(view);
    inputRamPressureNode->setStyleSheet(nodeStyle);

    inputHeartbeatNode = new QLineEdit(view);
    inputHeartbeatNode->setStyleSheet(nodeStyle);

    // =====================================
    // LOAD HEADERS FROM SQLITE
    // =====================================
    QSqlQuery qHead("SELECT * FROM HeaderSettings WHERE id = 1");
    if (qHead.next()) {
        inputPlcNode->setText(qHead.value("plcNode").toString());
        inputRailNode->setText(qHead.value("railNode").toString());
        inputInterlockNode->setText(qHead.value("interlockNode").toString());
        inputRamPressureNode->setText(qHead.value("ramNode").toString());
        inputHeartbeatNode->setText(qHead.value("heartbeatNode").toString());
    } else {
        // Database is empty, inject defaults
        inputPlcNode->setText("ns=2;i=20");
        inputRailNode->setText("ns=2;i=21");
        inputInterlockNode->setText("ns=2;i=22");
        inputRamPressureNode->setText("ns=2;i=23");
        inputHeartbeatNode->setText("ns=4;i=16");
    }

    activePlcNode = inputPlcNode->text();
    activeRailNode = inputRailNode->text();
    activeInterlockNode = inputInterlockNode->text();
    activeRamNode = inputRamPressureNode->text();
    activeHeartbeatNode = inputHeartbeatNode->text();

    headerSettingsLayout->addWidget(new QLabel("PLC Node:", view));
    headerSettingsLayout->addWidget(inputPlcNode);

    headerSettingsLayout->addSpacing(15);
    headerSettingsLayout->addWidget(new QLabel("Rail Node:", view));
    headerSettingsLayout->addWidget(inputRailNode);

    headerSettingsLayout->addSpacing(15);
    headerSettingsLayout->addWidget(new QLabel("Interlock Node:", view));
    headerSettingsLayout->addWidget(inputInterlockNode);

    headerSettingsLayout->addSpacing(15);
    headerSettingsLayout->addWidget(new QLabel("RAM Pres Node:", view));
    headerSettingsLayout->addWidget(inputRamPressureNode);

    headerSettingsLayout->addSpacing(15);
    headerSettingsLayout->addWidget(new QLabel("HMI Heartbeat:", view));
    headerSettingsLayout->addWidget(inputHeartbeatNode);

    headerSettingsLayout->addStretch();
    layout->addLayout(headerSettingsLayout);

    auto *titleLabel = new QLabel("MASSIVE OUTPUT MAPPING", view);
    titleLabel->setStyleSheet("color: #D85A00; font-size: 14px; font-weight: bold; font-family: 'Segoe UI'; margin-top: 20px;");
    layout->addWidget(titleLabel);

    m_tagsFormLayout = new QFormLayout();
    m_tagsFormLayout->setVerticalSpacing(15);
    m_tagsFormLayout->setHorizontalSpacing(20);

    // =====================================
    // LOAD DYNAMIC TAGS FROM SQLITE
    // =====================================
    QSqlQuery qTags("SELECT * FROM DynamicTags ORDER BY id ASC");
    bool hasTags = false;
    while(qTags.next()) {
        hasTags = true;
        addSettingRow(
            qTags.value("nodeId").toString(),
            qTags.value("title").toString(),
            qTags.value("subtitle").toString(),
            qTags.value("bgColor").toString(),
            qTags.value("textColor").toString()
        );
    }

    if (!hasTags) {
        // Factory defaults if SQLite is empty
        addSettingRow("ns=4;i=9", "READY TO LAUNCH", "[ LINK OK ]   [ GPS LOCK ]   [ IMU OK ]", "#E69B00", "#111111");
        addSettingRow("ns=4;i=10", "SYSTEM ARM", "Awaiting physical arm actuation", "#E69B00", "#111111");
        addSettingRow("ns=4;i=11", "SAFETY OFF", "Ordnance / actuation path live — restrict personnel", "#8B0000", "#111111");
        addSettingRow("ns=4;i=12", "ABORT", "ALL ACTUATION HALTED — HOLD FOR OPERATOR INSTRUCTION", "#FF0000", "#FFFFFF");
        addSettingRow("ns=4;i=13", "PRESS ACKNOWLEDGE", "[ ACK ]", "#E69B00", "#111111");
        addSettingRow("ns=4;i=14", "PLATFORM CLEAR", "Safe to approach — safety confirmed off", "#1CA600", "#111111");
        addSettingRow("ns=2;i=10", "MAINT MODE", "Insert maintenance USB to enable diagnostics", "#E69B00", "#111111");
    }

    layout->addLayout(m_tagsFormLayout);

    auto *btnAddTag = new QPushButton("ADD NEW TAG", view);
    btnAddTag->setStyleSheet("background-color: transparent; color: #D85A00; border: 1px dashed #D85A00; padding: 10px; font-weight: bold; font-family: 'Segoe UI'; border-radius: 4px;");
    btnAddTag->setCursor(Qt::PointingHandCursor);
    layout->addWidget(btnAddTag);

    connect(btnAddTag, &QPushButton::clicked, this, [this]() {
        addSettingRow("", "NEW ALARM", "Custom description", "#333333", "#FFFFFF");
    });

    btnSaveSettings = new QPushButton("Apply Configuration", view);
    btnSaveSettings->setObjectName("BtnOpcConnect");
    btnSaveSettings->setFixedWidth(250);
    layout->addWidget(btnSaveSettings);
    layout->addStretch();

    connect(btnSaveSettings, &QPushButton::clicked, this, &MainWindow::onSaveSettings);

    scrollArea->setWidget(view);

    // Auto-setup network if tags were successfully loaded from memory
    setupMultiNodeMonitoring();

    return scrollArea;
}

void MainWindow::addSettingRow(const QString& defNode, const QString& defText, const QString& defSub, const QString& defBg, const QString& defTextCol) {
    auto *uiStruct = new StateMapUI();
    auto *rowLayout = new QHBoxLayout();
    rowLayout->setSpacing(8);

    QString inputStyle = "background-color: #111111; color: #00FF00; border: 1px solid #444444; padding: 7px; font-family: 'Consolas'; border-radius: 3px;";

    uiStruct->nodeIdInput = new QLineEdit(defNode);
    uiStruct->nodeIdInput->setPlaceholderText("Node ID");
    uiStruct->nodeIdInput->setStyleSheet(inputStyle);
    uiStruct->nodeIdInput->setFixedWidth(90);

    uiStruct->textInput = new QLineEdit(defText);
    uiStruct->textInput->setPlaceholderText("Title Text");
    uiStruct->textInput->setStyleSheet(inputStyle);

    uiStruct->subtitleInput = new QLineEdit(defSub);
    uiStruct->subtitleInput->setPlaceholderText("Subtitle Text");
    uiStruct->subtitleInput->setStyleSheet(inputStyle);

    uiStruct->bgColorHex = defBg;
    uiStruct->textColorHex = defTextCol;

    uiStruct->bgColorBtn = new QPushButton("BG");
    uiStruct->textColorBtn = new QPushButton("TXT");
    uiStruct->bgColorBtn->setFixedWidth(40);
    uiStruct->textColorBtn->setFixedWidth(40);

    uiStruct->previewLabel = new QLabel(defText);
    uiStruct->previewLabel->setAlignment(Qt::AlignCenter);
    uiStruct->previewLabel->setMinimumWidth(160);
    uiStruct->previewLabel->setFixedHeight(32);

    auto *deleteBtn = new QPushButton("✖");
    deleteBtn->setFixedWidth(30);
    deleteBtn->setStyleSheet("background-color: #331111; color: #FF3333; border: 1px solid #FF3333; border-radius: 3px; font-weight: bold; font-family: 'Segoe UI';");
    deleteBtn->setCursor(Qt::PointingHandCursor);

    auto updateLivePreview = [uiStruct]() {
        uiStruct->previewLabel->setStyleSheet(QString(
            "background-color: %1; color: %2; font-family: 'Segoe UI', sans-serif; font-size: 11px; font-weight: 900; letter-spacing: 1px; border-radius: 4px;"
        ).arg(uiStruct->bgColorHex, uiStruct->textColorHex));
        uiStruct->previewLabel->setText(uiStruct->textInput->text());
    };

    auto updateBtnStyle = [](QPushButton* btn, const QString& hexColor) {
        QColor col(hexColor);
        QString contrast = (col.lightness() < 128) ? "#FFFFFF" : "#000000";
        btn->setStyleSheet(QString("background-color: %1; color: %2; border: 1px solid #444444; font-weight: bold; border-radius: 3px;")
                           .arg(hexColor, contrast));
    };

    updateBtnStyle(uiStruct->bgColorBtn, uiStruct->bgColorHex);
    updateBtnStyle(uiStruct->textColorBtn, uiStruct->textColorHex);
    updateLivePreview();

    connect(uiStruct->textInput, &QLineEdit::textChanged, this, [updateLivePreview]() { updateLivePreview(); });

    connect(uiStruct->bgColorBtn, &QPushButton::clicked, this, [this, uiStruct, updateBtnStyle, updateLivePreview]() {
        QColor color = QColorDialog::getColor(QColor(uiStruct->bgColorHex), this, "Select Background Color");
        if (color.isValid()) {
            uiStruct->bgColorHex = color.name();
            updateBtnStyle(uiStruct->bgColorBtn, uiStruct->bgColorHex);
            updateLivePreview();
        }
    });

    connect(uiStruct->textColorBtn, &QPushButton::clicked, this, [this, uiStruct, updateBtnStyle, updateLivePreview]() {
        QColor color = QColorDialog::getColor(QColor(uiStruct->textColorHex), this, "Select Text Color");
        if (color.isValid()) {
            uiStruct->textColorHex = color.name();
            updateBtnStyle(uiStruct->textColorBtn, uiStruct->textColorHex);
            updateLivePreview();
        }
    });

    rowLayout->addWidget(uiStruct->nodeIdInput);
    rowLayout->addWidget(uiStruct->textInput, 2);
    rowLayout->addWidget(uiStruct->subtitleInput, 3);
    rowLayout->addWidget(uiStruct->bgColorBtn);
    rowLayout->addWidget(uiStruct->textColorBtn);
    rowLayout->addWidget(uiStruct->previewLabel);
    rowLayout->addWidget(deleteBtn);

    int tagCount = m_dynamicTags.size() + 1;
    QString labelText = QString("Tag %1:").arg(tagCount);
    auto *label = new QLabel(labelText);
    label->setStyleSheet("color: #CCCCCC; font-weight: bold; font-family: 'Segoe UI';");

    m_tagsFormLayout->addRow(label, rowLayout);

    uiStruct->deleteBtn = deleteBtn;
    uiStruct->rowLabel = label;
    uiStruct->rowLayout = rowLayout;

    m_dynamicTags.append(uiStruct);

    connect(deleteBtn, &QPushButton::clicked, this, [this, uiStruct]() {
        this->removeSettingRow(uiStruct);
    });
}

void MainWindow::removeSettingRow(StateMapUI* uiStruct) {
    if (!uiStruct) return;

    m_dynamicTags.removeOne(uiStruct);

    uiStruct->rowLabel->deleteLater();

    QLayoutItem *child;
    while ((child = uiStruct->rowLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
    uiStruct->rowLayout->deleteLater();
    delete uiStruct;

    for (int i = 0; i < m_dynamicTags.size(); ++i) {
        m_dynamicTags[i]->rowLabel->setText(QString("Tag %1:").arg(i + 1));
    }
}

void MainWindow::onSaveSettings() {
    activePlcNode = inputPlcNode->text();
    activeRailNode = inputRailNode->text();
    activeInterlockNode = inputInterlockNode->text();
    activeRamNode = inputRamPressureNode->text();
    activeHeartbeatNode = inputHeartbeatNode->text();

    // WRITE TO SQLITE
    QSqlQuery qHead;
    qHead.prepare("INSERT OR REPLACE INTO HeaderSettings (id, plcNode, railNode, interlockNode, ramNode, heartbeatNode) VALUES (1, ?, ?, ?, ?, ?)");
    qHead.addBindValue(activePlcNode);
    qHead.addBindValue(activeRailNode);
    qHead.addBindValue(activeInterlockNode);
    qHead.addBindValue(activeRamNode);
    qHead.addBindValue(activeHeartbeatNode);
    qHead.exec();

    QSqlQuery qClear;
    qClear.exec("DELETE FROM DynamicTags");

    for (StateMapUI* tag : m_dynamicTags) {
        if(tag->nodeIdInput->text().trimmed().isEmpty()) continue;
        QSqlQuery q;
        q.prepare("INSERT INTO DynamicTags (nodeId, title, subtitle, bgColor, textColor) VALUES (?, ?, ?, ?, ?)");
        q.addBindValue(tag->nodeIdInput->text());
        q.addBindValue(tag->textInput->text());
        q.addBindValue(tag->subtitleInput->text());
        q.addBindValue(tag->bgColorHex);
        q.addBindValue(tag->textColorHex);
        q.exec();
    }

    resetHeaderToNoAvail();
    setupMultiNodeMonitoring();

    appendLog("[CONFIG] Operation Mode configurations saved to local SQL database.", false, "SYSTEM");
}

QWidget* MainWindow::createOperationModeDisplay() {
    operationModeContainer = new QWidget(this);
    operationModeContainer->setStyleSheet("background-color: #333333;");

    auto *layout = new QVBoxLayout(operationModeContainer);
    layout->setContentsMargins(0, 0, 0, 0);

    layout->addStretch(1);

    massiveStatusLabel = new QLabel("STANDBY", operationModeContainer);
    massiveStatusLabel->setAlignment(Qt::AlignCenter);
    massiveStatusLabel->setStyleSheet("color: #FFFFFF; font-family: 'Segoe UI', sans-serif; font-size: 110px; font-weight: 900; letter-spacing: 4px;");
    layout->addWidget(massiveStatusLabel);

    massiveSubtitleLabel = new QLabel("System idle - awaiting operator", operationModeContainer);
    massiveSubtitleLabel->setAlignment(Qt::AlignCenter);
    massiveSubtitleLabel->setStyleSheet("color: #888888; font-family: 'Consolas', monospace; font-size: 20px; letter-spacing: 2px; margin-top: 10px;");
    layout->addWidget(massiveSubtitleLabel);

    layout->addStretch(1);

    auto *brandingLabel = new QLabel("FUSCHWOLF SYSTEM", operationModeContainer);
    brandingLabel->setAlignment(Qt::AlignCenter);
    brandingLabel->setStyleSheet("color: #4A4A4A; background: transparent; font-family: 'Segoe UI', sans-serif; font-size: 16px; font-weight: 900; letter-spacing: 12px; margin-bottom: 25px;");
    layout->addWidget(brandingLabel);

    return operationModeContainer;
}

void MainWindow::setToStandby() {
    alertFlashTimer->stop();
    operationModeContainer->setStyleSheet("background-color: #333333;");
    massiveStatusLabel->setText("STANDBY");
    massiveStatusLabel->setStyleSheet("color: #E0E0E0; font-family: 'Segoe UI', sans-serif; font-size: 110px; font-weight: 900; letter-spacing: 4px;");

    massiveSubtitleLabel->setText("System idle - awaiting operator");
    massiveSubtitleLabel->setStyleSheet("color: #888888; font-family: 'Consolas', monospace; font-size: 20px; letter-spacing: 2px; margin-top: 10px;");

    headerMiniModeLabel->setText("STANDBY");
    headerMiniModeLabel->setStyleSheet("background-color: #333333; color: white; padding: 4px 10px; border-radius: 3px; font-weight: bold; font-family: 'Segoe UI'; font-size: 12px; margin-right: 15px;");
}

void MainWindow::toggleAlertFlash() {
    abortFlashState = !abortFlashState;
    QString bgHex = abortFlashState ? currentAlertBgHex : "#4A0000";
    operationModeContainer->setStyleSheet(QString("background-color: %1;").arg(bgHex));
}

void MainWindow::applyDisplayState(const OutputStateConfig& config, bool isFlashing) {
    massiveStatusLabel->setText(config.displayText);
    massiveStatusLabel->setStyleSheet(QString("color: %1; font-family: 'Segoe UI', sans-serif; font-size: 110px; font-weight: 900; letter-spacing: 4px;").arg(config.textColorHex));

    massiveSubtitleLabel->setText(config.subtitleText);
    massiveSubtitleLabel->setStyleSheet(QString("color: %1; font-family: 'Consolas', monospace; font-size: 20px; letter-spacing: 2px; margin-top: 10px; opacity: 0.8;").arg(config.textColorHex));

    headerMiniModeLabel->setText(config.displayText);
    headerMiniModeLabel->setStyleSheet(QString("background-color: %1; color: %2; padding: 4px 10px; border-radius: 3px; font-weight: bold; font-family: 'Segoe UI'; font-size: 12px; margin-right: 15px;")
                                       .arg(config.bgColorHex, config.textColorHex));

    if (isFlashing) {
        currentAlertBgHex = config.bgColorHex;
        currentAlertTextHex = config.textColorHex;
        if (!alertFlashTimer->isActive()) alertFlashTimer->start();
    } else {
        alertFlashTimer->stop();
        operationModeContainer->setStyleSheet(QString("background-color: %1;").arg(config.bgColorHex));
    }
}

void MainWindow::evaluateMassiveDisplay() {
    bool anyActive = false;
    OutputStateConfig activeConfig;
    QString activeNodeId;

    for (auto it = mNodeToStateConfigMap.cbegin(); it != mNodeToStateConfigMap.cend(); ++it) {
        if (m_activeAlarms.value(it.key(), false) && it.value().displayText == "ABORT") {
            anyActive = true;
            activeConfig = it.value();
            activeNodeId = it.key();
            break;
        }
    }

    if (!anyActive) {
        for (auto it = mNodeToStateConfigMap.cbegin(); it != mNodeToStateConfigMap.cend(); ++it) {
            if (m_activeAlarms.value(it.key(), false)) {
                if (m_timedOutAlarms.value(it.key(), false)) {
                    continue;
                }
                anyActive = true;
                activeConfig = it.value();
                activeNodeId = it.key();
                break;
            }
        }
    }

    if (anyActive) {
        standbyTimer->stop();
        bool isFlashing = (activeConfig.displayText == "ABORT" || activeConfig.displayText == "SAFETY OFF");
        applyDisplayState(activeConfig, isFlashing);

        if (activeConfig.displayText != "ABORT") {
            if (m_currentlyDisplayedNodeId != activeNodeId) {
                m_currentlyDisplayedNodeId = activeNodeId;
                generalTimeoutTimer->start(5000);
            }
        } else {
            generalTimeoutTimer->stop();
            m_currentlyDisplayedNodeId = activeNodeId;
        }

    } else {
        generalTimeoutTimer->stop();
        m_currentlyDisplayedNodeId.clear();
        setToStandby();
    }
}

void MainWindow::onGeneralAlarmTimeout() {
    if (!m_currentlyDisplayedNodeId.isEmpty()) {
        m_timedOutAlarms[m_currentlyDisplayedNodeId] = true;
        evaluateMassiveDisplay();
    }
}

void MainWindow::onMonitoredValueChanged(QOpcUa::NodeAttributes attr) {
    if (!(attr & QOpcUa::NodeAttribute::Value)) return;

    auto *senderNode = qobject_cast<QOpcUaNode*>(sender());
    if (!senderNode) return;

    QString nodeId = senderNode->nodeId();
    QVariant nodeValue = senderNode->attribute(QOpcUa::NodeAttribute::Value);

    if (nodeId == activePlcNode) {
        bool isOnline = nodeValue.toBool();
        hdrPlcText->setText(isOnline ? "PLC ONLINE" : "PLC OFFLINE");
        hdrPlcDot->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;").arg(isOnline ? "#00FF00" : "#FF0000"));
        return;
    } else if (nodeId == activeRailNode) {
        bool isLocked = nodeValue.toBool();
        hdrRailText->setText(isLocked ? "RAIL POS LOCKED" : "RAIL POS UNLOCKED");
        hdrRailDot->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;").arg(isLocked ? "#00FF00" : "#FFA500"));
        return;
    } else if (nodeId == activeInterlockNode) {
        bool isEngaged = nodeValue.toBool();
        hdrIntLockText->setText(isEngaged ? "INTERLOCK ENGAGED" : "INTERLOCK DISENGAGED");
        hdrIntLockDot->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;").arg(isEngaged ? "#00FF00" : "#FF0000"));
        return;
    } else if (nodeId == activeRamNode) {
        if (!nodeValue.isValid() || nodeValue.isNull()) {
            hdrRamText->setText("RAM PRESSURE NO AVAIL");
            hdrRamDot->setStyleSheet("color: #555555; font-size: 14px; font-weight: bold;");
            return;
        }

        QString valStr = nodeValue.toString();
        bool ok;
        double val = valStr.toDouble(&ok);
        if(ok) {
            hdrRamText->setText(QString("RAM PRESSURE %1 BAR").arg(val, 0, 'f', 1));
        } else {
            hdrRamText->setText(QString("RAM PRESSURE %1 BAR").arg(valStr));
        }
        hdrRamDot->setStyleSheet("color: #FFA500; font-size: 14px; font-weight: bold;");
        return;
    }

    bool isTriggered = nodeValue.toBool();
    m_activeAlarms[nodeId] = isTriggered;
    m_timedOutAlarms[nodeId] = false;

    evaluateMassiveDisplay();
}

void MainWindow::checkForServiceDrive() {
    QString userDir = QDir::homePath();
    QStringList searchPaths = {
        QString("/run/media/%1").arg(QFileInfo(userDir).fileName()),
        QString("/media/%1").arg(QFileInfo(userDir).fileName()),
        "/run/media"
    };

    bool driveFound = false;
    QString loginFilePathToParse = "";

    for (const QString &basePath : searchPaths) {
        QDir baseDir(basePath);
        if (!baseDir.exists()) continue;

        QFileInfoList usbDrives = baseDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &drive : usbDrives) {
            QString loginFilePath = drive.absoluteFilePath() + "/login.txt";
            if (QFile::exists(loginFilePath)) {
                driveFound = true;
                loginFilePathToParse = loginFilePath;
                break;
            }
        }
        if (driveFound) break;
    }

    if (driveFound) {
        if (!m_isUsbAuthenticated) {
            m_isUsbAuthenticated = true;
            parseLoginFile(loginFilePathToParse);
        }
    } else {
        if (m_isUsbAuthenticated) {
            m_isUsbAuthenticated = false;
            techNameLabel->setText("● AWAITING DATA...");
            techTitleDivisionLabel->setText("Title: --\nDiv: --");

            appendLog("Operator physical key removed. System locked.", false, "AUTH");

            if (mainStack->currentIndex() == 1) {
                mainStack->setCurrentIndex(0);
            }
        }
    }
}

void MainWindow::parseLoginFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&file);
    QString name = "UNKNOWN";
    QString title = "Technician";
    QString division = "Fuschwolf";

    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.contains("Name:", Qt::CaseInsensitive)) {
            name = line.section(':', 1).trimmed();
        } else if (line.contains("Title:", Qt::CaseInsensitive)) {
            title = line.section(':', 1).trimmed();
        } else if (line.contains("Division:", Qt::CaseInsensitive)) {
            division = line.section(':', 1).trimmed();
        }
    }
    file.close();

    techNameLabel->setText("● " + name.toUpper());
    techTitleDivisionLabel->setText(QString("Title: %1\nDiv: %2").arg(title, division));

    appendLog(QString("Operator Authenticated: %1").arg(name.toUpper()), false, "AUTH");

    mainStack->setCurrentIndex(1);
}

void MainWindow::onMqttMessageReceived(const QByteArray &message, const QMqttTopicName &topic) {
    if (topic.name() == "fw3011/maint/key") {
        bool isUnlocked = (message == "true" || message == "1" || message.toLower() == "on");
        if (m_mqttMaintState != isUnlocked) {
            m_mqttMaintState = isUnlocked;
            handleMaintKeySignal(isUnlocked);
        }
    }
}

void MainWindow::handleMaintKeySignal(bool isInserted) {
    if (isInserted) {
        appendLog("Remote MQTT Maintenance Key Inserted.", false, "MAINT");
        mainStack->setCurrentIndex(1);
    } else {
        appendLog("Remote MQTT Maintenance Key Removed. System Locked.", false, "MAINT");
        mainStack->setCurrentIndex(0);
    }
}

void MainWindow::setupPkiConfiguration() {
    if (!mOpcUaClient) return;

    QString pkiDir = QDir::homePath() + "/.larc_system/pki";
    QDir().mkpath(pkiDir + "/trusted/certs");
    QDir().mkpath(pkiDir + "/rejected");

    QOpcUaPkiConfiguration pki;
    pki.setTrustListDirectory(pkiDir + "/trusted/certs");
    mOpcUaClient->setPkiConfiguration(pki);
}

void MainWindow::setupMultiNodeMonitoring() {
    mNodeToStateConfigMap.clear();
    m_activeAlarms.clear();
    m_timedOutAlarms.clear();
    m_currentlyDisplayedNodeId.clear();
    generalTimeoutTimer->stop();

    auto mapConfig = [this](StateMapUI* uiStruct) {
        if(uiStruct->nodeIdInput->text().trimmed().isEmpty()) return;
        OutputStateConfig config;
        config.displayText = uiStruct->textInput->text();
        config.subtitleText = uiStruct->subtitleInput->text();
        config.bgColorHex = uiStruct->bgColorHex;
        config.textColorHex = uiStruct->textColorHex;
        mNodeToStateConfigMap.insert(uiStruct->nodeIdInput->text(), config);
    };

    for(StateMapUI* tag : m_dynamicTags) {
        mapConfig(tag);
    }

    if (!mOpcUaClient || mOpcUaClient->state() != QOpcUaClient::ClientState::Connected) return;

    for (QOpcUaNode* node : mMonitoredNodes) {
        if (node) {
            node->disableMonitoring(QOpcUa::NodeAttribute::Value);
            node->deleteLater();
        }
    }
    mMonitoredNodes.clear();

    QOpcUaMonitoringParameters params;
    params.setPublishingInterval(10);
    params.setSamplingInterval(10);
    params.setQueueSize(1);
    params.setDiscardOldest(true);

    auto subscribeNode = [this, params](const QString& nodeId, const QString& logName) {
        if (nodeId.isEmpty()) return;
        QOpcUaNode *node = mOpcUaClient->node(nodeId);
        if (node) {
            mMonitoredNodes.insert(nodeId, node);
            connect(node, &QOpcUaNode::attributeUpdated, this, &MainWindow::onMonitoredValueChanged);
            node->enableMonitoring(QOpcUa::NodeAttribute::Value, params);
            appendLog(QString("[MONITOR] Subscribed to tag: %1 -> [%2]").arg(nodeId, logName));
        }
    };

    for (auto it = mNodeToStateConfigMap.cbegin(); it != mNodeToStateConfigMap.cend(); ++it) {
        subscribeNode(it.key(), it.value().displayText);
    }

    subscribeNode(activePlcNode, "HEADER: PLC Status");
    subscribeNode(activeRailNode, "HEADER: Rail Status");
    subscribeNode(activeInterlockNode, "HEADER: Interlock Status");
    subscribeNode(activeRamNode, "HEADER: RAM Pressure");
}

void MainWindow::onConnectOpcUa() {
    if (m_manualDisconnect) {
        m_manualDisconnect = false;
    }

    QString endpoint = opcEndpointInput->text();
    if (endpoint.isEmpty()) return;

    btnOpcConnect->setEnabled(false);
    opcEndpointInput->setEnabled(false);
    appendLog("[INFO] Querying PLC for endpoints and certificates...");

    QOpcUaProvider provider;
    if (provider.availableBackends().isEmpty()) {
        appendLog("[ERROR] No OPC UA backends available (e.g. open62541).", true);
        btnOpcConnect->setEnabled(true);
        opcEndpointInput->setEnabled(true);
        return;
    }

    if (!mOpcUaClient) {
        mOpcUaClient = provider.createClient(provider.availableBackends()[0]);
        if (!mOpcUaClient) {
            appendLog("[ERROR] Failed to instantiate OPC UA Client.", true);
            return;
        }

        connect(mOpcUaClient, &QOpcUaClient::stateChanged, this, &MainWindow::opcUaStateChanged);
        connect(mOpcUaClient, &QOpcUaClient::errorChanged, this, &MainWindow::opcUaError);
        connect(mOpcUaClient, &QOpcUaClient::endpointsRequestFinished, this, &MainWindow::onEndpointsRequestFinished);
    }

    mOpcUaClient->requestEndpoints(endpoint);
}

void MainWindow::onEndpointsRequestFinished(const QList<QOpcUaEndpointDescription> &endpoints, QOpcUa::UaStatusCode statusCode) {
    if (endpoints.isEmpty() || statusCode != QOpcUa::UaStatusCode::Good) {
        appendLog("[ERROR] Failed to discover endpoints. Retrying...");
        btnOpcConnect->setEnabled(true);
        opcEndpointInput->setEnabled(true);
        return;
    }

    QOpcUaEndpointDescription targetEndpoint = endpoints.first();
    for (const auto &ep : endpoints) {
        if (ep.securityPolicy() == "http://opcfoundation.org/UA/SecurityPolicy#None") {
            targetEndpoint = ep;
            break;
        }
    }

    QByteArray serverCert = targetEndpoint.serverCertificate();

    QString pkiDir = QDir::homePath() + "/.larc_system/pki";
    QDir().mkpath(pkiDir + "/trusted/certs");

    if (!serverCert.isEmpty()) {
        QString certHash = QString(QCryptographicHash::hash(serverCert, QCryptographicHash::Sha1).toHex());
        QString trustedPath = pkiDir + "/trusted/certs/" + certHash + ".der";

        if (!QFile::exists(trustedPath)) {
            CertValidationDialog dialog(serverCert, "PLC_Certificate", this);
            if (dialog.exec() == QDialog::Accepted && dialog.isPermanentlyTrusted()) {
                QFile f(trustedPath);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(serverCert);
                    f.close();
                    appendLog("[SECURITY] SIMATIC Certificate securely trusted.");
                }
            } else {
                appendLog("[SECURITY] Certificate rejected by operator. Connection aborted.");
                btnOpcConnect->setEnabled(true);
                opcEndpointInput->setEnabled(true);
                return;
            }
        }
    }

    setupPkiConfiguration();

    QOpcUaUserTokenPolicy anonymousToken;
    anonymousToken.setTokenType(QOpcUaUserTokenPolicy::TokenType::Anonymous);
    targetEndpoint.setUserIdentityTokens({anonymousToken});

    appendLog("[INFO] Initiating Secure Channel...");
    mOpcUaClient->connectToEndpoint(targetEndpoint);
}

void MainWindow::onDisconnectOpcUa() {
    if (mOpcUaClient) {
        clearHmiHeartbeat();
        m_manualDisconnect = true;
        reconnectTimer->stop();
        hmiHeartbeatTimer->stop();
        appendLog("[INFO] Disconnecting from server...");
        mOpcUaClient->disconnectFromEndpoint();
    }
}

void MainWindow::opcUaStateChanged(QOpcUaClient::ClientState state) {
    if (state == QOpcUaClient::ClientState::Connected) {
        btnOpcDisconnect->setEnabled(true);
        reconnectTimer->stop();
        hmiHeartbeatTimer->start();
        appendLog("[SUCCESS] Connected to PLC. Address space loaded.");

        opcTreeModel->removeRows(0, opcTreeModel->rowCount());

        auto *rootName = new QStandardItem("Root");
        auto *rootBrowse = new QStandardItem("0:Root");
        rootName->setData("ns=0;i=84", NodeIdRole);
        rootName->appendRow(new QStandardItem("Loading..."));

        opcTreeModel->appendRow({rootName, rootBrowse});
        opcBrowseTree->expand(opcTreeModel->index(0, 0));

        setupMultiNodeMonitoring();
    }
    else if (state == QOpcUaClient::ClientState::Disconnected) {
        btnOpcConnect->setEnabled(true);
        btnOpcDisconnect->setEnabled(false);
        opcEndpointInput->setEnabled(true);
        hmiHeartbeatTimer->stop();

        opcTreeModel->removeRows(0, opcTreeModel->rowCount());
        opcAttributeTable->setRowCount(0);
        appendLog("[INFO] Server disconnected.");

        resetHeaderToNoAvail();

        for (QOpcUaNode* node : mMonitoredNodes) {
            if (node) node->deleteLater();
        }
        mMonitoredNodes.clear();
        m_activeAlarms.clear();
        m_timedOutAlarms.clear();
        m_currentlyDisplayedNodeId.clear();
        generalTimeoutTimer->stop();

        if (!m_manualDisconnect) {
            if (!reconnectTimer->isActive()) {
                appendLog("[WARNING] Connection lost. Auto-reconnect enabled (5s)...", true);
                reconnectTimer->start(5000);
            }
        }
    }
}

void MainWindow::opcUaError(QOpcUaClient::ClientError error) {
    QString errStr;
    if (error == QOpcUaClient::ClientError::ConnectionError) {
        errStr = "Connection Error / Timeout";
    } else {
        errStr = QString("Code %1").arg(error);
    }
    appendLog(QString("[ERROR] OPC UA Client Error detected: %1").arg(errStr), true);
    btnOpcConnect->setEnabled(true);
    opcEndpointInput->setEnabled(true);
}

void MainWindow::treeItemExpanded(const QModelIndex &index) {
    QStandardItem *item = opcTreeModel->itemFromIndex(index);
    if (!item) return;

    if (item->rowCount() == 1 && item->child(0, 0)->text() == "Loading...") {
        QString nodeId = item->data(NodeIdRole).toString();
        item->removeRow(0);
        browseNode(nodeId, item);
    }
}

void MainWindow::browseNode(const QString &nodeId, QStandardItem *parentItem) {
    if (!mOpcUaClient) return;

    QOpcUaNode *tempNode = mOpcUaClient->node(nodeId);
    if (!tempNode) return;

    connect(tempNode, &QOpcUaNode::browseFinished, this, [tempNode, parentItem](const QList<QOpcUaReferenceDescription> &children, QOpcUa::UaStatusCode statusCode) {
        if (statusCode == QOpcUa::UaStatusCode::Good) {
            for (const QOpcUaReferenceDescription &ref : children) {
                if (ref.isForwardReference()) {
                    auto *dispNameItem = new QStandardItem(ref.browseName().name());
                    dispNameItem->setData(ref.targetNodeId().nodeId(), NodeIdRole);

                    auto *browseNameItem = new QStandardItem(QString("%1:%2").arg(ref.browseName().namespaceIndex()).arg(ref.browseName().name()));

                    dispNameItem->appendRow(new QStandardItem("Loading..."));
                    parentItem->appendRow({dispNameItem, browseNameItem});
                }
            }
        }
        tempNode->deleteLater();
    });

    tempNode->browseChildren();
}

void MainWindow::treeItemClicked(const QModelIndex &index) {
    if (index.column() != 0) return;

    QStandardItem *item = opcTreeModel->itemFromIndex(index);
    if (!item) return;

    QString nodeId = item->data(NodeIdRole).toString();
    if (nodeId.isEmpty()) return;

    if (mActiveNode) {
        mActiveNode->deleteLater();
        mActiveNode = nullptr;
    }

    mActiveNode = mOpcUaClient->node(nodeId);
    if (!mActiveNode) return;

    opcAttributeTable->setRowCount(0);

    connect(mActiveNode, &QOpcUaNode::attributeRead, this, [this](QOpcUa::NodeAttributes attr) {
        if (!mActiveNode) return;

        int row = 0;
        opcAttributeTable->setRowCount(5);

        auto addAttribute = [this, &row](const QString &name, const QString &val) {
            opcAttributeTable->setItem(row, 0, new QTableWidgetItem(name));
            opcAttributeTable->setItem(row, 1, new QTableWidgetItem(val));
            row++;
        };

        addAttribute("NodeId", mActiveNode->nodeId());
        QVariant nodeClass = mActiveNode->attribute(QOpcUa::NodeAttribute::NodeClass);
        if (nodeClass.isValid()) addAttribute("NodeClass", nodeClass.toString());
        QVariant browseName = mActiveNode->attribute(QOpcUa::NodeAttribute::BrowseName);
        if (browseName.isValid()) addAttribute("BrowseName", browseName.toString());
        QVariant val = mActiveNode->attribute(QOpcUa::NodeAttribute::Value);
        if (val.isValid()) addAttribute("Value", val.toString());
        QVariant dataType = mActiveNode->attribute(QOpcUa::NodeAttribute::DataType);
        if (dataType.isValid()) addAttribute("DataType", dataType.toString());
    });

    mActiveNode->readAttributes(
        QOpcUa::NodeAttribute::Value |
        QOpcUa::NodeAttribute::NodeClass |
        QOpcUa::NodeAttribute::BrowseName |
        QOpcUa::NodeAttribute::DataType
    );
}

void MainWindow::setupTheme() {
    QString qss = R"(
        QMainWindow { background-color: #171717; }
        #MainBackground { background-color: #121212; }

        #TopHeaderBar { background-color: #1A1A1A; border-bottom: 2px solid #000000; }

        #Sidebar { background-color: #1A1A1A; border-right: 1px solid #222222; }
        #OperatorCard { background-color: #262626; border-left: 3px solid #D85A00; margin: 0px 15px; }
        #CardHeaderStyle { color: #888888; font-size: 9px; font-weight: 800; letter-spacing: 1px; }
        #TechNameStyle { color: #FFFFFF; font-weight: bold; font-size: 11px; font-family: 'Segoe UI'; letter-spacing: 0.5px; }
        #TechDetailStyle { color: #8A8A8E; font-size: 10px; font-family: 'Consolas', monospace; line-height: 1.4; }

        #NavButton {
            color: #8A8A8E; text-align: left; font-size: 11px; font-weight: 700;
            font-family: 'Segoe UI', Arial, sans-serif; letter-spacing: 1px;
            padding: 14px 20px; background-color: transparent; border: none;
            border-left: 3px solid transparent;
        }
        #NavButton:hover { color: #FFFFFF; background-color: #262626; border-left: 3px solid #D85A00; }
        #NavButton:pressed { background-color: #333333; }

        #NavButtonExit {
            color: #C22929; text-align: left; font-size: 11px; font-weight: 900;
            font-family: 'Segoe UI', Arial, sans-serif; letter-spacing: 1px;
            padding: 14px 20px; background-color: transparent; border: none;
            border-left: 3px solid transparent;
        }
        #NavButtonExit:hover { color: #FFFFFF; background-color: #262626; border-left: 3px solid #C22929; }
        #NavButtonExit:pressed { background-color: #333333; }

        #MenuToggleBtn {
            color: #888888; text-align: left; font-size: 12px; font-weight: 800;
            background-color: transparent; padding: 10px 20px; border: none; letter-spacing: 2px;
        }
        #MenuToggleBtn:hover { color: #D85A00; }

        #OpcManagerBg { background-color: #2A2C2F; }

        #ConnectBar { background-color: #1F2124; border-bottom: 1px solid #111111; }
        #EndpointInput { background-color: #111111; color: #FFFFFF; border: 1px solid #444444; padding: 5px; font-family: Consolas; }
        #BtnOpcConnect, #BtnOpcDisconnect { background-color: #3B3F44; color: white; border: 1px solid #111111; padding: 5px 15px; }
        #BtnOpcConnect:hover { background-color: #555A60; }
        #BtnOpcDisconnect:hover { background-color: #C22929; }

        #OpcBrowseTree { background-color: #2A2C2F; color: #E0E0E0; border: none; font-size: 13px; }
        #OpcBrowseTree::item:selected { background-color: #2D6EB5; color: white; }

        #AttrHeader { color: #FFFFFF; font-weight: bold; background-color: #3B3F44; padding: 5px; }

        #OpcAttributeTable { background-color: #2A2C2F; color: #E0E0E0; gridline-color: #444444; border: none; font-size: 13px; }
        #OpcAttributeTable::item:selected { background-color: #2D6EB5; color: white; }

        QHeaderView::section { background-color: #1F2124; color: #AAAAAA; padding: 4px; border: 1px solid #111111; }

        #OpcLogPanel {
            background-color: #111111; color: #CCCCCC;
            font-family: 'Consolas', monospace; font-size: 12px;
            border: none; border-top: 2px solid #1F2124; padding: 10px;
        }
    )";
    this->setStyleSheet(qss);
}