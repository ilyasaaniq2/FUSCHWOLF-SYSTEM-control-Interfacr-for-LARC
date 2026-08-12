#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QMqttClient>
#include <qtermwidget.h>
#include <QFileSystemWatcher>
#include <QLineEdit>
#include <QTreeView>
#include <QTableWidget>
#include <QSplitter>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QMap>
#include <QColorDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QTimer>
#include <QDateTime>
#include <QCryptographicHash>
#include <QScrollArea>

// QtSql Includes for Black Box Logging & Config
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>

// PKI UI Includes
#include <QDialog>
#include <QSslCertificate>
#include <QTreeWidget>
#include <QCheckBox>
#include <QGroupBox>

// Include Native Qt OPC UA
#include <QOpcUaProvider>
#include <QOpcUaClient>
#include <QOpcUaNode>
#include <QOpcUaEndpointDescription>
#include <QOpcUaUserTokenPolicy>
#include <QOpcUaPkiConfiguration>

// =======================================================================
// CERTIFICATE VALIDATION DIALOG
// =======================================================================
class CertValidationDialog : public QDialog {
    Q_OBJECT
public:
    explicit CertValidationDialog(const QByteArray &derData, const QString &certFileName, QWidget *parent = nullptr);
    [[nodiscard]] bool isPermanentlyTrusted() const { return m_trusted; }

private:
    bool m_trusted = false;
};

// Row Management Pointers
struct StateMapUI {
    QLineEdit *nodeIdInput = nullptr;
    QLineEdit *textInput = nullptr;
    QLineEdit *subtitleInput = nullptr;
    QPushButton *bgColorBtn = nullptr;
    QPushButton *textColorBtn = nullptr;
    QLabel *previewLabel = nullptr;
    QPushButton *deleteBtn = nullptr;
    QLabel *rowLabel = nullptr;
    QHBoxLayout *rowLayout = nullptr;
    QString bgColorHex;
    QString textColorHex;
};

struct OutputStateConfig {
    QString displayText;
    QString subtitleText;
    QString bgColorHex;
    QString textColorHex;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void handleMaintKeySignal(bool isInserted);
    void onMqttMessageReceived(const QByteArray &message, const QMqttTopicName &topic);
    void checkForServiceDrive();

    // OPC UA System Slots
    void onConnectOpcUa();
    void onDisconnectOpcUa();
    void onEndpointsRequestFinished(const QList<QOpcUaEndpointDescription> &endpoints, QOpcUa::UaStatusCode statusCode);
    void opcUaStateChanged(QOpcUaClient::ClientState state);
    void opcUaError(QOpcUaClient::ClientError error);
    void treeItemExpanded(const QModelIndex &index);
    void treeItemClicked(const QModelIndex &index);

    // Operation Mode Multi-Tag Monitor
    void onMonitoredValueChanged(QOpcUa::NodeAttributes attr);
    void onSaveSettings();

    // Operational Timers
    void setToStandby();
    void toggleAlertFlash();
    void onGeneralAlarmTimeout();
    void kickWatchdog();
    void sendHmiHeartbeat();
    void clearHmiHeartbeat();

private:
    void initDatabase();
    void setupUi();
    void setupTheme();
    QWidget* createTopHeader();
    QWidget* createFrame1();
    QWidget* createAndroid11();
    QWidget* createOpcUaManager();
    QWidget* createOperationModeDisplay();
    QWidget* createSettingsPage();

    // Row Logic
    void addSettingRow(const QString& defNode, const QString& defText, const QString& defSub, const QString& defBg, const QString& defTextCol);
    void removeSettingRow(StateMapUI* uiStruct);
    void parseLoginFile(const QString &filePath);
    void applyDisplayState(const OutputStateConfig& config, bool isFlashing = false);
    void evaluateMassiveDisplay();
    QWidget* createHeaderStatusItem(QLabel** dotLabel, QLabel** textLabel, const QString& defaultText, const QString& defaultColor);
    void resetHeaderToNoAvail();

    // OPC UA Core Methods
    void browseNode(const QString &nodeId, QStandardItem *parentItem);
    void setupMultiNodeMonitoring();
    void setupPkiConfiguration();
    void appendLog(const QString &msg, bool isError = false, const QString &logType = "INFO");

    // UI Pointers
    QStackedWidget *mainStack = nullptr;
    QWidget *frame1Page = nullptr;
    QWidget *android11Page = nullptr;

    QTermWidget *diagnosticConsole = nullptr;
    QTermWidget *nativeConsole = nullptr;

    QFileSystemWatcher *usbWatcher = nullptr;
    QLabel *techNameLabel = nullptr;
    QLabel *techTitleDivisionLabel = nullptr;

    QMqttClient *mqttClient = nullptr;

    // OPC UA GUI Elements
    QLineEdit *opcEndpointInput = nullptr;
    QPushButton *btnOpcConnect = nullptr;
    QPushButton *btnOpcDisconnect = nullptr;
    QTreeView *opcBrowseTree = nullptr;
    QStandardItemModel *opcTreeModel = nullptr;
    QTableWidget *opcAttributeTable = nullptr;
    QTextEdit *opcLogPanel = nullptr;

    // Top Header Status GUI
    QLabel *hdrPlcDot = nullptr;
    QLabel *hdrPlcText = nullptr;
    QLabel *hdrRailDot = nullptr;
    QLabel *hdrRailText = nullptr;
    QLabel *hdrIntLockDot = nullptr;
    QLabel *hdrIntLockText = nullptr;
    QLabel *hdrRamDot = nullptr;
    QLabel *hdrRamText = nullptr;
    QLabel *headerMiniModeLabel = nullptr;
    QLabel *headerClockLabel = nullptr;
    QTimer *headerClockTimer = nullptr;

    // Top Header Settings Controls
    QLineEdit *inputPlcNode = nullptr;
    QLineEdit *inputRailNode = nullptr;
    QLineEdit *inputInterlockNode = nullptr;
    QLineEdit *inputRamPressureNode = nullptr;
    QLineEdit *inputHeartbeatNode = nullptr;

    QString activePlcNode;
    QString activeRailNode;
    QString activeInterlockNode;
    QString activeRamNode;
    QString activeHeartbeatNode;

    // Operation Mode GUI Elements
    QWidget *operationModePage = nullptr;
    QWidget *operationModeContainer = nullptr;
    QLabel *massiveStatusLabel = nullptr;
    QLabel *massiveSubtitleLabel = nullptr;

    QTimer *standbyTimer = nullptr;
    QTimer *alertFlashTimer = nullptr;
    QTimer *generalTimeoutTimer = nullptr;
    QTimer *systemHealthWatchdog = nullptr;
    QTimer *hmiHeartbeatTimer = nullptr;

    bool abortFlashState = false;
    QString currentAlertBgHex;
    QString currentAlertTextHex;

    // State Trackers & Watchdogs
    bool m_isUsbAuthenticated = false;
    bool m_mqttMaintState = false;
    bool m_manualDisconnect = false;
    QString m_currentlyDisplayedNodeId;
    QTimer *reconnectTimer = nullptr;

    // Local Alarm State Trackers
    QMap<QString, bool> m_activeAlarms;
    QMap<QString, bool> m_timedOutAlarms;

    // Dynamic Tags
    QFormLayout *m_tagsFormLayout = nullptr;
    QList<StateMapUI*> m_dynamicTags;
    QPushButton *btnSaveSettings = nullptr;

    // OPC UA Backend Pointers
    QOpcUaClient *mOpcUaClient = nullptr;
    QOpcUaNode *mActiveNode = nullptr;

    // Core Maps for Configuration
    QMap<QString, QOpcUaNode*> mMonitoredNodes;
    QMap<QString, OutputStateConfig> mNodeToStateConfigMap;
};

#endif // MAINWINDOW_H