// Start at login on Windows: the MSIX package's startup task (ADR-022, mocks/windows.html
// note 4), declared in packaging/windows/AppxManifest.xml.in with the id below.

#include "services/autostart.h"
#include "services/logging.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>

namespace omnidict::services {

namespace {

constexpr wchar_t kTaskId[] = L"OmnidictStartup";

namespace wam = winrt::Windows::ApplicationModel;
namespace wf = winrt::Windows::Foundation;

} // namespace

void Autostart::requestStartupTask(bool request, bool enabled)
{
    // Windows answers on a thread of its own; the answer comes back to this object's thread.
    const QPointer<Autostart> self(this);
    const auto deliver = [self, request, enabled](int state) {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, state, request, enabled] {
                if (self) {
                    self->takeTaskState(state, request, enabled);
                }
            },
            Qt::QueuedConnection);
    };
    try {
        wam::StartupTask::GetAsync(kTaskId).Completed(
            [deliver, request, enabled](const wf::IAsyncOperation<wam::StartupTask>& operation,
                                        wf::AsyncStatus status) {
                int state = -1;
                try {
                    if (status == wf::AsyncStatus::Completed) {
                        const wam::StartupTask task = operation.GetResults();
                        if (request && enabled) {
                            // No question for a desktop app; DisabledByUser stays as it is.
                            state = static_cast<int>(task.RequestEnableAsync().get());
                        } else {
                            if (request) {
                                task.Disable();
                            }
                            state = static_cast<int>(task.State());
                        }
                    }
                } catch (const winrt::hresult_error& error) {
                    qCWarning(lcBackground)
                        << "autostart: startup task:" << QString::fromWCharArray(error.message().c_str());
                }
                deliver(state);
            });
    } catch (const winrt::hresult_error& error) {
        // Not running from the package (a plain build): there is no startup task.
        qCInfo(lcBackground) << "autostart: no startup task:"
                             << QString::fromWCharArray(error.message().c_str());
        deliver(-1);
    }
}

bool Autostart::launchedAtLogin()
{
    try {
        const wam::Activation::IActivatedEventArgs args = wam::AppInstance::GetActivatedEventArgs();
        return args && args.Kind() == wam::Activation::ActivationKind::StartupTask;
    } catch (const winrt::hresult_error&) {
        return false; // not packaged
    }
}

} // namespace omnidict::services
