#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

#ifndef Q_MOC_RUN
#include <core/gen/libcore.pb.h>
#endif

#include "include/database/entities/Profile.h"
#include "include/engine/tests/TestSessionGate.h"
#include "include/engine/tests/ServiceChecks.h"

namespace Engine {
    class EnginePolicy;
    class TestObserver;

    // Not a QObject: queued signals would reorder the synchronous progress path.
    class TestRunner {
    public:
        // Any thread; the running profile, or null.
        using RunningSnapshot = std::function<std::shared_ptr<Configs::Profile>()>;

        // Test threads, after each stored URL result (a profile may report twice); a late result poll can still call it after onFinished.
        using ResultHook = std::function<void(const std::shared_ptr<Configs::Profile>&)>;

        struct ChainStepResult {
            int hop = -1;
            bool alone = false;
            // Profile::latency semantics: ms, -1 failed, or kLatencyConnectOnly.
            int latency = 0;
            QString error;
        };

        struct ChainStepReport {
            QString error;
            QSet<int> invalidHops;
            bool hasLanding = false;
            bool aborted = false;
            QList<ChainStepResult> results;
        };

        // policy and observer outlive the runner; a null observer gets a no-op stand-in (headless).
        TestRunner(EnginePolicy* policy, TestObserver* observer, RunningSnapshot runningSnapshot);

        TestRunner(const TestRunner&) = delete;
        TestRunner& operator=(const TestRunner&) = delete;

        // The run* calls return whether a session started; onFinished fires either way, on a refusal before they return.
        bool runUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished = {}, bool interactive = true,
                         const ResultHook& onResult = {});

        // Waits for speed tests or overlapping profile IDs; returns at once and is safe from any thread.
        void queueUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished, bool interactive = true);

        bool runIpTests(const QList<int>& profileIDs, const std::function<void()>& onFinished = {}, bool interactive = true);

        // queueUrlTests for the IP test.
        void queueIpTests(const QList<int>& profileIDs, const std::function<void()>& onFinished, bool interactive = true);

        bool runSpeedTests(const QList<int>& profileIDs, bool testCurrent = false,
                           const std::function<void()>& onFinished = {}, bool interactive = true);

        // Never automatic: every profile sends about 20 requests to third-party services.
        static constexpr int kMaxServiceCheckProfiles = 100;
        bool runServiceChecks(const QList<int>& profileIDs, const std::function<void()>& onFinished = {}, bool interactive = true);

        // The running profile, through the running core's own proxy outbound.
        bool runCurrentServiceCheck(const std::function<void()>& onFinished = {}, bool interactive = true);

        // Refused while another test runs. Saves nothing; onDone runs once, on a test thread.
        bool runChainStepTest(const QList<int>& hops, int gid, const std::function<void(const ChainStepReport&)>& onDone);

        void stop();

        bool isRunning();

        bool isTestingCurrent() const { return testingCurrent_.load(); }

        // Whether stop() was called since the latest session started.
        bool stopRequested() const { return gate_.stopRequested.load(); }

    private:
        enum class LatencyKind { Url, Ip };

        struct Target {
            QString coreConfig;
            QString xrayConfig;
            QStringList xrayFullConfigs;
            QStringList outboundTags;
            QMap<QString, int> tag2entID;
            QString xrayDnsStrategy;
            QString qwdttConfig;
            int entID = -1;
            // Not derivable from an empty outboundTags: a test-current run leaves both empty but wants "proxy".
            bool useDefaultOutbound = false;
            bool testCurrent = false;
            ResultHook onResult;
        };

        bool runLatencyGroup(LatencyKind kind, const QList<int>& requestedIDs,
                             const std::function<void()>& onFinished, bool waitForSession = false, bool interactive = true,
                             const ResultHook& onResult = {});

        void runUrlProbe(const Target& target);

        void runIpProbe(const Target& target);

        void runSpeedProbe(const Target& target);

        bool runServiceSession(const QList<int>& requestedIDs, bool testCurrent, const std::function<void()>& onFinished, bool interactive);

        void runServiceProbe(const Target& target, const std::shared_ptr<const ServiceChecks::Definitions>& definitions);

        void applyUrlResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::URLTestResp& res,
                            const QHash<QString, bool>* vpnConnected = nullptr);

        void applyIpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::IPTestRes& res);

        void applyServiceResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::ServiceTestResult& res,
                                const QList<ServiceChecks::Category>& categories);

        QString contextName(int entID) const;

        // -1 while nothing runs.
        int runningProfileId() const;

        bool staleGen(quint64 gen) const { return sessionGen_.load() != gen; }

        void pollSpeedTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

        void pollCountryTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen);

        void creditTraffic(const std::shared_ptr<Configs::Profile>& profile, const QString& tag,
                           qint64 curUp, qint64 curDown);

        EnginePolicy* policy_;
        TestObserver* observer_;
        RunningSnapshot runningSnapshot_;

        // Multiple latency sweeps share ownership; speed tests remain exclusive.
        TestSessionGate gate_;
        // A poll thread is not joined, so a late tick must not drain the next batch.
        std::atomic<quint64> sessionGen_ = 0;
        std::atomic<bool> testingCurrent_ = false;
        // Counts stop() calls; each probe sends the count it launched under (see StopTestReq).
        std::atomic<qint64> stopEpoch_ = 0;
        // The running session's flag: an unattended session never prompts for geo assets.
        std::atomic<bool> interactive_ = true;

        // Tests bypass the clash tracker, so their bytes are counted only here, diffed per tag.
        QMutex creditMu_;
        QHash<QString, QPair<qint64, qint64>> credited_;
        // Streamed URL results: tags of every running shared-box probe (unique per profile across concurrent
        // sweeps, as the gate never tests one profile twice) and the tags already applied from the stream.
        QMutex streamMu_;
        QHash<QString, int> streamTags_;
        QSet<QString> streamed_;
        void pollUrlStream(const ResultHook &onResult);
        // Profiles of running URL sweeps, so a repeated press can restart them.
        QMutex urlMu_;
        QSet<int> urlIds_;
        // Main thread only: restarted rows keep their spinner while the stopped sweep reports its aborts.
        QSet<int> restarting_;
        void clearTestState(const QList<int>& ids);

        // Last, so they are destroyed first, while the members their probes use still exist.
        QThreadPool pool_;
        QThreadPool nativeProbePool_;
        // qWDTT, CSQTT and WDTT Plus: the core queues them per device and per VK call and runs three at a time,
        // so a probe waiting there for its turn must not hold a thread another native profile needs.
        QThreadPool vkProbePool_;
    };
} // namespace Engine
