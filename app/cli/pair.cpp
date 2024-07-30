#include "pair.h"

#include "backend/computermanager.h"
#include "backend/computerseeker.h"
#include "streaming/session.h"
#include <QCoreApplication>
#include <QEventloop>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#define COMPUTER_SEEK_TIMEOUT 10000

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

  Event(Type type) : type(type), computerManager(nullptr), computer(nullptr) {}

  Type type;
  ComputerManager *computerManager;
  NvComputer *computer;
  QString errorMessage;
};

class LauncherPrivate {
  Q_DECLARE_PUBLIC(Launcher)

public:
  LauncherPrivate(Launcher *q) : q_ptr(q) { m_AutoPair = false; }

  void handleEvent(Event event) {
    Q_Q(Launcher);
    Session *session;
    NvApp app;
    switch (event.type) {
    // Occurs when CliPair becomes visible and the UI calls launcher's execute()
    case Event::Executed:
      if (m_State == StateInit) {
        m_State = StateSeekComputer;
        m_ComputerManager = event.computerManager;
        q->connect(m_ComputerManager, &ComputerManager::pairingCompleted, q, &Launcher::onPairingCompleted);

        // If we weren't provided a predefined PIN, generate one now
        if (m_PredefinedPin.isEmpty()) {
          m_PredefinedPin = m_ComputerManager->generatePinString();
        }

        m_ComputerSeeker = new ComputerSeeker(m_ComputerManager, m_ComputerName, q);
        q->connect(m_ComputerSeeker, &ComputerSeeker::computerFound, q, &Launcher::onComputerFound);
        q->connect(m_ComputerSeeker, &ComputerSeeker::errorTimeout, q, &Launcher::onTimeout);

        m_ComputerSeeker->start(COMPUTER_SEEK_TIMEOUT);

        q->connect(m_ComputerManager, &ComputerManager::quitAppCompleted, q, &Launcher::onQuitAppCompleted);

        emit q->searchingComputer();
      }
      break;
    // Occurs when searched computer is found
    case Event::ComputerFound:
      if (m_State == StateSeekComputer) {
        if (event.computer->pairState == NvComputer::PS_PAIRED && event.errorMessage.isEmpty()) {
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

                  {
                    m_State = StateCompleteAfteConnect; // change status.
                    session = new Session(m_Computer, app, NULL);
                    if (session) {
                      session->SetFullScren(m_FullScreen);
                      if (!m_HttpJsonStr.isEmpty() && !m_Token.isEmpty()) {
                        session->SetComputerInfor(m_HttpJsonStr.toStdString(), m_Token.toStdString(),
                                                  m_Arch.toStdString(), m_ClientUUid.toStdString(),
                                                  m_PrivateKeyStr.toStdString(), m_FullScreen);
                      }
                      emit q->sessionCreated(app.name, session);
                    }
                  }
                } else {
                  m_State = StateFailure;
                  emit q->failed("desktop name error");
                }
        } else {
          Q_ASSERT(!m_PredefinedPin.isEmpty());

          m_State = StatePairing;
          // send auto pair https req
          if (m_PredefinedPin.length() == 0) {
            emit q->failed("pair id error");
            return;
          }
          m_ComputerManager->pairHost(event.computer, m_PredefinedPin);
          emit q->pairing(event.computer->name, m_PredefinedPin);
          if (m_Token.length() > 0 && m_InstanceId.length() > 0) {
            m_ComputerManager->RequestGwsPair(event.computer, m_PredefinedPin, m_Token, m_InstanceId, m_PHost);
            m_AutoPair = true;
          }
        }
      }
      break;
    // Occurs when pairing operation completes
    case Event::ComputerUpdated: {
      if (StateCompleteAfteConnect != m_State && event.errorMessage.isEmpty()) {
          m_Computer = event.computer;
          if (event.computer->pairState == NvComputer::PS_PAIRED) {
            QString m_AppName = "Desktop"; // warning: desktop name Need to modify later,
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
                m_State = StateCompleteAfteConnect;
                session = new Session(m_Computer, app, NULL);
                if (session) {
                  session->SetFullScren(m_FullScreen);
                  if (!m_HttpJsonStr.isEmpty() && !m_Token.isEmpty()) {
                    session->SetComputerInfor(m_HttpJsonStr.toStdString(), m_Token.toStdString(), m_Arch.toStdString(),
                                              m_ClientUUid.toStdString(), m_PrivateKeyStr.toStdString(), m_FullScreen);
                  }
                  emit q->sessionCreated(app.name, session);
                }
            }
          }
      }
    } break;
    case Event::PairingCompleted:
      if (m_State == StatePairing) {
        if (event.errorMessage.isEmpty()) {
          m_State = StateComplete;
          // emit q->success();
          // q->connect(m_ComputerManager, &ComputerManager::computerStateChanged,
          //                       q, &Launcher::onComputerUpdated);

          m_Computer = event.computer;

          if (event.computer->pairState == NvComputer::PS_PAIRED) {
            m_Computer = event.computer;
          }
          if (m_AutoPair) {
            m_ComputerManager->QuitAutoPair();
          }
          QString ID = m_InstanceId;
          QString m_AppName;
          int index = -1;
          int size = m_Computer->appList.length();
          for (int i = 0; i < size; i++) {
            m_AppName = m_Computer->appList[i].name.toLower();
            // if (m_Computer->appList[i].name.toLower() == m_AppName.toLower()) {
            index = i;
            // }
          }
          if (-1 != index) {
            app = m_Computer->appList[index];
            m_TimeoutTimer->stop();
            if (m_Computer->currentGameId == index || m_Computer->currentGameId == app.id) {

              session = new Session(m_Computer, app, NULL);
              if (session) {
                session->SetFullScren(m_FullScreen);
                if (!m_HttpJsonStr.isEmpty() && !m_Token.isEmpty()) {
                  session->SetComputerInfor(m_HttpJsonStr.toStdString(), m_Token.toStdString(), m_Arch.toStdString(),
                                            m_ClientUUid.toStdString(), m_PrivateKeyStr.toStdString(), m_FullScreen);
                }
                emit q->sessionCreated(app.name, session);
              }

            } else {
              q->connect(m_ComputerManager, &ComputerManager::computerStateChanged, q, &Launcher::onComputerUpdated);
            }
          } else {
            q->connect(m_ComputerManager, &ComputerManager::computerStateChanged, q, &Launcher::onComputerUpdated);
          }

        } else {
          m_State = StateFailure;
          emit q->failed(event.errorMessage);
        }
      }
      break;
    case Event::AppQuitCompleted: {
      emit q->failed("Timeout when pairing, please reboot and try again. Contact administrator if fail again.");
      break;
    }
    // Occurs when computer search timed out
    case Event::Timedout:
      if (m_State == StateSeekComputer) {
        m_State = StateFailure;
        emit q->failed(QObject::tr("Failed to connect to %1").arg(m_ComputerName));
      }
      break;
    }
  }

  Launcher *q_ptr;
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
  ComputerManager *m_ComputerManager;
  ComputerSeeker *m_ComputerSeeker;
  NvComputer *m_Computer;
  State m_State;
  QTimer *m_TimeoutTimer;
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

void Launcher::execute(ComputerManager *manager) {
  Q_D(Launcher);
  Event event(Event::Executed);
  event.computerManager = manager;
  d->handleEvent(event);
}

bool Launcher::isExecuted() const {
  Q_D(const Launcher);
  return d->m_State != StateInit;
}

void Launcher::onComputerFound(NvComputer *computer) {
  Q_D(Launcher);
  Event event(Event::ComputerFound);
  event.computer = computer;
  d->handleEvent(event);
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
