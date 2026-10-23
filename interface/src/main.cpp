//
//  main.cpp
//  interface/src
//
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <thread>

#include <QCommandLineParser>
#include <QtCore/QProcess>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QLocalServer>
#include <QSharedMemory>
#include <QTranslator>

#include <BuildInfo.h>
#include <SharedUtil.h>
#include <StartupProfiler.h>
#include <NetworkAccessManager.h>
#include <gl/GLHelpers.h>

#include "AddressManager.h"
#include "Application.h"
#include "CrashHandler.h"
#include "InterfaceLogging.h"
#include "MainWindow.h"

#include "Profile.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

int main(int argc, const char* argv[]) {
    StartupProfiler::start();
    StartupProfiler::phase("main: surface format, application name, command line");
#ifdef Q_OS_MACOS
    auto format = getDefaultOpenGLSurfaceFormat();
    // Deal with some weirdness in the chromium context sharing on Mac.
    // The primary share context needs to be 3.2, so that the Chromium will
    // succeed in it's creation of it's command stub contexts.  
    format.setVersion(3, 2);
    // This appears to resolve the issues with corrupted fonts on OSX.  No
    // idea why.
    qputenv("QT_ENABLE_GLYPH_CACHE_WORKAROUND", "true");
	// https://i.kym-cdn.com/entries/icons/original/000/008/342/ihave.jpg
    QSurfaceFormat::setDefaultFormat(format);
#endif

#if defined(Q_OS_WIN) 
    // Check the minimum version of 
    if (gl::getAvailableVersion() < gl::getRequiredVersion()) {
        MessageBoxA(nullptr, "Darkride requires OpenGL 4.1 or higher", "Unsupported", MB_OK);
        return -1;
    }
#endif

    setupHifiApplication(BuildInfo::INTERFACE_NAME);

    QStringList arguments;
    for (int i = 0; i < argc; ++i) {
        arguments << argv[i];
    }

    QCommandLineParser parser;
    parser.setApplicationDescription("Darkride");
    QCommandLineOption versionOption = parser.addVersionOption();
    QCommandLineOption helpOption = parser.addHelpOption();

    QCommandLineOption urlOption("url", "", "value");
    QCommandLineOption allowMultipleInstancesOption("allowMultipleInstances", "Allow multiple instances to run");
    QCommandLineOption overrideAppLocalDataPathOption("cache", "set test cache <dir>", "dir");
    QCommandLineOption overrideScriptsPathOption(SCRIPTS_SWITCH, "set scripts <path>", "path");
    QCommandLineOption responseTokensOption("tokens", "set response tokens <json>", "json");
    QCommandLineOption displayNameOption("displayName", "set user display name <string>", "string");
    QCommandLineOption setBookmarkOption("setBookmark", "set bookmark key=value pair", "string");
    QCommandLineOption defaultScriptOverrideOption("defaultScriptsOverride", "override defaultsScripts.js", "string");
    // read by Application (clientLabel): listed here so --help shows it and the parser doesn't call it unknown
    QCommandLineOption clientLabelOption("clientLabel", "set the window title's client label <text>", "text");

    parser.addOption(urlOption);
    parser.addOption(overrideAppLocalDataPathOption);
    parser.addOption(overrideScriptsPathOption);
    parser.addOption(allowMultipleInstancesOption);
    parser.addOption(responseTokensOption);
    parser.addOption(displayNameOption);
    parser.addOption(setBookmarkOption);
    parser.addOption(defaultScriptOverrideOption);
    parser.addOption(clientLabelOption);

    if (!parser.parse(arguments)) {
        std::cout << parser.errorText().toStdString() << std::endl; // Avoid Qt log spam
    }

    if (parser.isSet(versionOption)) {
        parser.showVersion();
        Q_UNREACHABLE();
    }
    if (parser.isSet(helpOption)) {
        QCoreApplication mockApp(argc, const_cast<char**>(argv)); // required for call to showHelp()
        parser.showHelp();
        Q_UNREACHABLE();
    }

    StartupProfiler::phase("main: application path (a temporary QCoreApplication)");
    QString applicationPath;
    // A temporary application instance is needed to get the location of the running executable
    // Tests using high_resolution_clock show that this takes about 30-50 microseconds (on my machine, YMMV)
    // If we wanted to avoid the QCoreApplication, we would need to write our own
    // cross-platform implementation.
    {
        QCoreApplication tempApp(argc, const_cast<char**>(argv));
#ifdef Q_OS_MACOS
        if (QFileInfo::exists(QCoreApplication::applicationDirPath() + "/../../../config.json")) {
            applicationPath = QCoreApplication::applicationDirPath() + "/../../../";
        } else {
            applicationPath = QCoreApplication::applicationDirPath();
        }
#else
        applicationPath = QCoreApplication::applicationDirPath();
#endif
    }
    // Early check for --traceFile argument 
    StartupProfiler::phase("main: tracer");
    auto tracer = DependencyManager::set<tracing::Tracer>();
    const char * traceFile = nullptr;
    const QString traceFileFlag("--traceFile");
    float traceDuration = 0.0f;
    for (int a = 1; a < argc; ++a) {
        if (traceFileFlag == argv[a] && argc > a + 1) {
            traceFile = argv[a + 1];
            if (argc > a + 2) {
                traceDuration = atof(argv[a + 2]);
            }
            break;
        }
    }
    if (traceFile != nullptr) {
        tracer->startTracing();
    }
   
    PROFILE_SYNC_BEGIN(startup, "main startup", "");

    QElapsedTimer startupTime;
    startupTime.start();

    StartupProfiler::phase("main: settings (Setting::init)");
    Setting::init();

    StartupProfiler::phase("main: crash handler");
    auto crashHandlerStarted = startCrashHandler(argv[0]);
    qDebug() << "Crash handler started:" << crashHandlerStarted;


    StartupProfiler::phase("main: single-instance check, scripts path");
    const QString& applicationName = getInterfaceSharedMemoryName();
    bool instanceMightBeRunning = true;
#ifdef Q_OS_WIN
    // Try to create a shared memory block - if it can't be created, there is an instance of
    // interface already running. We only do this on Windows for now because of the potential
    // for crashed instances to leave behind shared memory instances on unix.
    QSharedMemory sharedMemory{ applicationName };
    instanceMightBeRunning = !sharedMemory.create(1, QSharedMemory::ReadOnly);
#endif

    // allow multiple interfaces to run if this environment variable is set.
    bool allowMultipleInstances = parser.isSet(allowMultipleInstancesOption) ||
                                  QProcessEnvironment::systemEnvironment().contains("HIFI_ALLOW_MULTIPLE_INSTANCES");
    if (allowMultipleInstances) {
        instanceMightBeRunning = false;
    }
    // this needs to be done here in main, as the mechanism for setting the
    // scripts directory appears not to work.  See the bug report
    // https://highfidelity.fogbugz.com/f/cases/5759/Issues-changing-scripts-directory-in-ScriptsEngine
    if (parser.isSet(overrideScriptsPathOption)) {
        QDir scriptsPath(parser.value(overrideScriptsPathOption));
        if (scriptsPath.exists()) {
            PathUtils::defaultScriptsLocation(scriptsPath.path());
        }
    }

    if (instanceMightBeRunning) {
        // Try to connect and send message to existing interface instance
        QLocalSocket socket;

        socket.connectToServer(applicationName);

        static const int LOCAL_SERVER_TIMEOUT_MS = 500;

        // Try to connect - if we can't connect, interface has probably just gone down
        if (socket.waitForConnected(LOCAL_SERVER_TIMEOUT_MS)) {
            if (parser.isSet(urlOption)) {
                QUrl url = QUrl(parser.value(urlOption));
                // disabled because we pass domain ids directly into --url
                // if (
                //     url.isValid() && 
                //     (
                //         url.scheme() == URL_SCHEME_HIFI || 
                //         url.scheme() == URL_SCHEME_HIFIAPP ||
                //         url.scheme() == HIFI_URL_SCHEME_HTTP ||
                //         url.scheme() == HIFI_URL_SCHEME_HTTPS ||
                //         url.scheme() == HIFI_URL_SCHEME_FILE
                //     )
                // ) {
                qDebug() << "Writing URL to local socket";
                socket.write(url.toString().toUtf8());
                if (!socket.waitForBytesWritten(5000)) {
                    qDebug() << "Error writing URL to local socket";
                }
                // }
            }

            socket.close();

            qDebug() << "Interface instance appears to be running, exiting";

            return EXIT_SUCCESS;
        }

#ifdef Q_OS_WIN
        return EXIT_SUCCESS;
#endif
    }


    StartupProfiler::phase("main: Application::initPlugins");
    // Oculus initialization MUST PRECEDE OpenGL context creation.
    // The nature of the Application constructor means this has to be either here,
    // or in the main window ctor, before GL startup.
    Application::initPlugins(arguments);

    int exitCode;
    {
        StartupProfiler::phase("main: running marker, arguments");
        RunningMarker runningMarker(RUNNING_MARKER_FILENAME);
        bool runningMarkerExisted = runningMarker.fileExists();
        runningMarker.writeRunningMarkerFile();

        // Extend argv to enable WebGL rendering
        std::vector<const char*> argvExtended(&argv[0], &argv[argc]);
        argvExtended.push_back("--ignore-gpu-blacklist");
#ifdef Q_OS_ANDROID
        // argvExtended.push_back("--suppress-settings-reset");
#endif
        int argcExtended = (int)argvExtended.size();

        PROFILE_SYNC_END(startup, "main startup", "");
        PROFILE_SYNC_BEGIN(startup, "app full ctor", "");
        StartupProfiler::phase("Application: QApplication, main window, file logger");
        Application app(argcExtended, const_cast<char**>(argvExtended.data()), startupTime, runningMarkerExisted);
        PROFILE_SYNC_END(startup, "app full ctor", "");

        StartupProfiler::phase("main: crash hook monitor, local server, system information, translator");
        startCrashHookMonitor(&app);

        QTimer exitTimer;
        if (traceDuration > 0.0f) {
            exitTimer.setSingleShot(true);
            QObject::connect(&exitTimer, &QTimer::timeout, &app, &Application::quit);
            exitTimer.start(int(1000 * traceDuration));
        }


        // Setup local server
        QLocalServer server { &app };

        // We failed to connect to a local server, so we remove any existing servers.
        server.removeServer(applicationName);
        server.listen(applicationName);

        QObject::connect(&server, &QLocalServer::newConnection,
                         &app, &Application::handleLocalServerConnection, Qt::DirectConnection);

        printSystemInformation();

        auto appPointer = dynamic_cast<Application*>(&app);
        if (appPointer) {
            if (parser.isSet(urlOption)) {
                appPointer->overrideEntry();
            }
            if (parser.isSet(displayNameOption)) {
                QString displayName = QString(parser.value(displayNameOption));
                appPointer->forceDisplayName(displayName);
            }
            if (parser.isSet(responseTokensOption)) {
                QString tokens = QString(parser.value(responseTokensOption));
                appPointer->forceLoginWithTokens(tokens);
            }
        }

        QTranslator translator;
        translator.load("i18n/interface_en");
        app.installTranslator(&translator);
        qCDebug(interfaceapp, "Created QT Application.");
        StartupProfiler::logReport();
        exitCode = app.exec();
        server.close();

        if (traceFile != nullptr) {
            tracer->stopTracing();
            tracer->serialize(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + traceFile);
        }
    }

    qCDebug(interfaceapp, "Normal exit.");
#if !defined(DEBUG) && !defined(Q_OS_LINUX)
    // HACK: exit immediately (don't handle shutdown callbacks) for Release build
    _exit(exitCode);
#endif
    return exitCode;
}
