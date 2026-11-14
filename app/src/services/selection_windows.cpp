// The selected text of the app with the focus, through UI Automation (mocks/windows.html note 5,
// DOCS/windows-selection.md). A worker thread asks, so a hung app costs the caller at most the
// timeout; the worker finishes on its own and its late answer is dropped.

#include "services/logging.h"
#include "services/selection.h"

#include <future>
#include <memory>
#include <thread>

// windows.h first: UI Automation's headers build on it.
#include <qt_windows.h>

#include <UIAutomation.h>
#include <wrl/client.h>

namespace omnidict::services {

namespace {

using Microsoft::WRL::ComPtr;

/// Longer selections are not a word to look up; ui::QuickLookup::wordFrom trims further.
constexpr int kMaxChars = 200;
/// How far up from the focused element to look for one that offers its text: in a browser the
/// focus can sit on a link or a span inside the document that holds the selection.
constexpr int kMaxAncestors = 5;

ComPtr<IUIAutomationTextPattern> textPatternOf(IUIAutomation* automation,
                                               ComPtr<IUIAutomationElement> element)
{
    ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(automation->get_ControlViewWalker(walker.ReleaseAndGetAddressOf())) || !walker) {
        return {};
    }
    for (int level = 0; element && level <= kMaxAncestors; ++level) {
        ComPtr<IUIAutomationTextPattern> pattern;
        if (SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId,
                                                   IID_PPV_ARGS(pattern.ReleaseAndGetAddressOf()))) &&
            pattern) {
            return pattern;
        }
        ComPtr<IUIAutomationElement> parent;
        if (FAILED(walker->GetParentElement(element.Get(), parent.ReleaseAndGetAddressOf()))) {
            break;
        }
        element = parent;
    }
    return {};
}

/// Runs on the worker thread, in its own COM apartment.
std::optional<QString> readSelection()
{
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::optional<QString> result;
    {
        ComPtr<IUIAutomation> automation;
        ComPtr<IUIAutomationElement> focused;
        ComPtr<IUIAutomationTextRangeArray> ranges;
        ComPtr<IUIAutomationTextRange> range;
        int count = 0;
        BSTR text = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(automation.ReleaseAndGetAddressOf()))) &&
            SUCCEEDED(automation->GetFocusedElement(focused.ReleaseAndGetAddressOf())) && focused) {
            const ComPtr<IUIAutomationTextPattern> pattern = textPatternOf(automation.Get(), focused);
            if (pattern && SUCCEEDED(pattern->GetSelection(ranges.ReleaseAndGetAddressOf())) && ranges &&
                SUCCEEDED(ranges->get_Length(&count)) && count > 0 &&
                SUCCEEDED(ranges->GetElement(0, range.ReleaseAndGetAddressOf())) && range &&
                SUCCEEDED(range->GetText(kMaxChars, &text)) && text != nullptr) {
                result = QString::fromWCharArray(text, static_cast<qsizetype>(SysStringLen(text)));
                SysFreeString(text);
            }
        }
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
    return result;
}

} // namespace

std::optional<QString> focusedSelection(std::chrono::milliseconds timeout)
{
    // Omnidict's own windows answer UI Automation on the GUI thread, which waits below: skip
    // them rather than wait out the timeout.
    DWORD owner = 0;
    if (const HWND foreground = GetForegroundWindow();
        foreground == nullptr ||
        (GetWindowThreadProcessId(foreground, &owner) != 0 && owner == GetCurrentProcessId())) {
        return std::nullopt;
    }
    auto task = std::make_shared<std::packaged_task<std::optional<QString>()>>(readSelection);
    std::future<std::optional<QString>> answer = task->get_future();
    std::thread([task] { (*task)(); }).detach();
    if (answer.wait_for(timeout) != std::future_status::ready) {
        qCInfo(lcShortcuts) << "quick lookup: the app with the focus did not give its selection in time";
        return std::nullopt;
    }
    return answer.get();
}

} // namespace omnidict::services
