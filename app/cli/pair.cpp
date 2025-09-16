#include "pair.h"

#include "backend/computermanager.h"
#include "backend/computerseeker.h"
#include "streaming/session.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include "Core/connectionmanager.h"
#include "Core/uiStateMachine.h"

#define COMPUTER_SEEK_TIMEOUT 10000000

namespace CliPair {

enum State {
  StateInit,
  StateSeekComputer,
  StatePairing,
  StateFailure,
  StateComplete,
  StateCompleteAfteConnect,
};

class Event {
public:
  enum Type {
    ComputerFound,
    Executed,
    ComputerUpdated,
    PairingCompleted,
    Timedout,
    AppQuitCompleted,
  };

  Event(Type type) : type(type), computerManager(nullptr), computer(nullptr), flags(0){}

  Type type;
  ComputerManager *computerManager;
  NvComputer *computer;
  int flags;//used to add more control data
  QString errorMessage;
};

class LauncherPrivate {
  Q_DECLARE_PUBLIC(Launcher)

public:
  LauncherPrivate(Launcher *q) : q_ptr(q) { m_AutoPair = false; }

  void handleEvent(Event event) {
    Q_Q(Launcher);
    NvApp app;
    static int pairRetryCnt = 0;
    static int s_iPairOpId = UIStateMachine::I().genOpId();
    switch (event.type) {
    // Occurs when CliPair becomes visible and the UI calls launcher's execute()
        qDebug()<<" Event::handleEvent event.type = "<<event.type <<"m_State = "<<m_State;
    case Event::Executed:
      if (m_State == StateInit) {
        qDebug()<<" Event::Executed m_State = "<<m_State;
        m_State = StateSeekComputer;
        m_ComputerManager = event.computerManager;
        q->connect(m_ComputerManager, &ComputerManager::pairingCompleted, q, &Launcher::onPairingCompleted);

        // If we weren't provided a predefined PIN, generate one now
        if (m_PredefinedPin.isEmpty()) {
          m_PredefinedPin = m_ComputerManager->generatePinString();
        }
        auto* pComputerSeeker = new ComputerSeeker(m_ComputerManager, m_ComputerName, q);
        if (event.flags == 1)
        {
            pComputerSeeker->SetSeekSelfOnly(true);
        }
        q->connect(pComputerSeeker, &ComputerSeeker::computerFound, q, &Launcher::onComputerFound);
        q->connect(pComputerSeeker, &ComputerSeeker::errorTimeout, q, &Launcher::onTimeout);

        pComputerSeeker->start(COMPUTER_SEEK_TIMEOUT);

        q->connect(m_ComputerManager, &ComputerManager::quitAppCompleted, q, &Launcher::onQuitAppCompleted);
        qDebug()<<"pair pin = "<<m_PredefinedPin;
        pComputerSeeker->deleteLater();
      }
      break;
    // Occurs when searched computer is found
    case Event::ComputerFound:
      if (m_State == StateSeekComputer) {
        qDebug()<<" Event::ComputerFound m_State = "<<m_State<<" event.computer->pairState = "<<event.computer->pairState;
        if (event.computer->pairState == NvComputer::PS_PAIRED ) {
                m_Computer = event.computer;
                QString m_AppName = "Desktop";
                int index = -1;
                int size = m_Computer->appList.length();
                for (int i = 0; i < size; i++) {
                  QString strGet = m_Computer->appList[i].name;
                  if (strGet == m_AppName) {
                    index = i;
                    break;
                  }
                }
                if (-1 != index) {
                  app = m_Computer->appList[index];
                  m_TimeoutTimer->stop();
                    m_State = StateCompleteAfteConnect; // change status.
                    qDebug()<<"Event::ComputerUpdated Connect";
                    ConnectionManager::I().pushEvent(new CMConnectEvent(m_Computer));
                } else {
                  m_State = StateFailure;
                }
        } else {
          Q_ASSERT(!m_PredefinedPin.isEmpty());

          m_State = StatePairing;
          // send auto pair https req
          if (m_PredefinedPin.length() == 0) {
              UIStateMachine::I().push(s_iPairOpId, MsgCode::NO_CODE, MsgType::Information, "Pair pin empty, please exit retry.");
              return;
          }
          pairRetryCnt = 0;
          UIStateMachine::I().push(s_iPairOpId, MsgCode::StartPair, MsgType::Progress, "Pairing ...");
          m_ComputerManager->pairHost(event.computer, m_PredefinedPin, m_Token, m_InstanceId, m_PHost);
        }
      }
      break;
    // Occurs when pairing operation completes
    case Event::PairingCompleted:
        qDebug()<<" Event::PairingCompleted m_State = "<<m_State
                <<" event.computer->pairState = "<<event.computer->pairState
                <<"event.errorMessage = "<<event.errorMessage;
      if (m_State == StatePairing && event.errorMessage.isEmpty())
        {
          m_State = StateComplete;
          m_Computer = event.computer;
          m_TimeoutTimer->stop();

          if (event.computer->pairState == NvComputer::PS_PAIRED)
          {
            m_Computer = event.computer;
            ConnectionManager::I().pushEvent(new CMConnectEvent(m_Computer));
          }
          else
          {
            qDebug()<<"  q->connect to m_ComputerManager = "<<m_ComputerManager;
            q->connect(m_ComputerManager, &ComputerManager::computerStateChanged, q, &Launcher::onComputerUpdated);
          }
        }
        else
      {
          pairRetryCnt++;
          UIStateMachine::I().push(s_iPairOpId, MsgCode::StartPair, MsgType::Progress,
            "Pairing ...[retry time : " + std::to_string(pairRetryCnt)+ " ] ");
          m_ComputerManager->pairHost(event.computer, m_PredefinedPin, m_Token, m_InstanceId, m_PHost);
        }

      break;
    case Event::AppQuitCompleted: {
      break;
    }
    // Occurs when computer search timed out
    case Event::Timedout:
      if (m_State == StateSeekComputer) {
        m_State = StateFailure;
      }
      break;
    }
  }

  Launcher *q_ptr;
  bool m_OnlySeekMyself = false;
  QString m_ComputerName;
  QString m_PredefinedPin;
  QString m_InstanceId;
  QString m_Token;
  QString m_HttpJsonStr;
  QString m_Arch;
  QString m_PHost;
  QString m_ClientUUid;
  QString m_PrivateKeyStr;
  bool m_FullScreen;
  ComputerManager *m_ComputerManager = nullptr;
  NvComputer *m_Computer = nullptr;
  State m_State;
  QTimer *m_TimeoutTimer = nullptr;
  bool m_AutoPair;
};

Launcher::Launcher(QString computer, QString predefinedPin, QString InstanceId, QString Token, QString HttpJsonStr,
                   QString ArchStr, QString host, QString ClientUUid, QString privateKeyStr, bool FullScreen,
                   QObject *parent)
    : QObject(parent), m_DPtr(new LauncherPrivate(this)) {
  Q_D(Launcher);
  d->m_ComputerName = computer;
  d->m_PredefinedPin = predefinedPin;
  d->m_InstanceId = InstanceId;
  d->m_Token = Token;
  d->m_HttpJsonStr = HttpJsonStr;
  d->m_Arch = ArchStr;
  d->m_PHost = host;
  d->m_ClientUUid = ClientUUid;
  d->m_PrivateKeyStr = privateKeyStr;
  d->m_FullScreen = FullScreen;
  d->m_State = StateInit;
  d->m_TimeoutTimer = new QTimer(this);
  d->m_TimeoutTimer->setSingleShot(true);
  qDebug()<<"m_ComputerName"<<d->m_ComputerName;
  qDebug()<<"d->m_PHost"<<d->m_PHost;
  connect(d->m_TimeoutTimer, &QTimer::timeout, this, &Launcher::onTimeout);

#ifdef Q_OS_WIN32
  // QProcess::execute(" taskkill /f /im gws_proxy.exe");
  QProcess p1(NULL);
  p1.start("cmd", QStringList() << "/c"
                                << "taskkill /f /im Tip.exe");
  p1.waitForStarted();
  p1.waitForFinished();
  p1.close();

#else
  QProcess p;
  p.start("pkill", QStringList() << "-f"
                                 << "Tip");
  p.waitForFinished(); // 等待命令执行完成
#endif
}

Launcher::~Launcher() {}
void Launcher::SetSeekSelfOnly(bool bFlag) {
    Q_D(Launcher);
    d->m_OnlySeekMyself = bFlag;
}
void Launcher::Reset()
{
    Q_D(Launcher);
    d->m_State = StateInit;
}
void Launcher::execute(ComputerManager *manager) {
  Q_D(Launcher);
  Event event(Event::Executed);
  event.computerManager = manager;
  event.flags = d->m_OnlySeekMyself?1:0;
  d->handleEvent(event);
}

bool Launcher::isExecuted() const {
  Q_D(const Launcher);
  return d->m_State != StateInit;
}

void Launcher::onComputerFound(NvComputer *computer) {
  Q_D(Launcher);
if(computer->pairState != NvComputer::PS_PAIRED)
{
    Event event(Event::ComputerFound);
    event.computer = computer;
    d->handleEvent(event);
}
else
{
    ConnectionManager::I().pushEvent(new CMConnectEvent(computer));
}
}

void Launcher::onTimeout() {
  Q_D(Launcher);
  Event event(Event::Timedout);
  d->handleEvent(event);
}

void Launcher::onPairingCompleted(NvComputer *computer, QString error) {
  Q_D(Launcher);
  Event event(Event::PairingCompleted);
  event.computer = computer;
  event.errorMessage = error;
  d->handleEvent(event);
}
void Launcher::onComputerUpdated(NvComputer *computer) {
  Q_D(Launcher);
  Event event(Event::ComputerUpdated);
  event.computer = computer;
  d->handleEvent(event);
}
void Launcher::onQuitAppCompleted(QVariant error) {
  Q_D(Launcher);
  Event event(Event::AppQuitCompleted);
  event.errorMessage = error.toString();
  d->handleEvent(event);
}
} // namespace CliPair
