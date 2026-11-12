#include "ui/key_recorder.h"

#include <QKeyEvent>
#include <QKeySequence>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

constexpr int kMinimumWidth = 150;

/// The modifier a modifier key stands for; none for any other key. Platforms differ on
/// whether a modifier key's own event lists it, so the recorder adds or drops it itself.
Qt::KeyboardModifier modifierOf(int key)
{
    switch (key) {
    case Qt::Key_Control:
        return Qt::ControlModifier;
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
        return Qt::AltModifier;
    case Qt::Key_Shift:
        return Qt::ShiftModifier;
    case Qt::Key_Meta:
        return Qt::MetaModifier;
    default:
        return Qt::NoModifier;
    }
}

/// The keys the Windows backend knows (services/hotkey_backend.cpp).
bool isRecordableKey(int key)
{
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9) ||
        (key >= Qt::Key_F1 && key <= Qt::Key_F24)) {
        return true;
    }
    switch (key) {
    case Qt::Key_Space:
    case Qt::Key_Insert:
    case Qt::Key_Delete:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Pause:
        return true;
    default:
        return false;
    }
}

constexpr Qt::KeyboardModifiers kUsedModifiers =
    Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier;

} // namespace

KeyRecorder::KeyRecorder(QWidget* parent)
    : QLineEdit(parent)
{
    setObjectName(u"quickLookupRecorder"_s);
    setReadOnly(true);
    setContextMenuPolicy(Qt::NoContextMenu);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(kMinimumWidth);
    setPlaceholderText(tr("Press the keys"));
}

QString KeyRecorder::keyFor(Qt::KeyboardModifiers modifiers, int key)
{
    modifiers &= kUsedModifiers;
    const bool held = modifiers.testAnyFlags(Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (!held || !isRecordableKey(key)) {
        return {};
    }
    return QKeySequence(QKeyCombination(modifiers, static_cast<Qt::Key>(key)))
        .toString(QKeySequence::PortableText);
}

void KeyRecorder::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers modifiers = event->modifiers() & kUsedModifiers;
    event->accept(); // nothing reaches the dialog: Escape here must not close it
    if (modifiers == Qt::NoModifier && key == Qt::Key_Escape) {
        Q_EMIT cancelled();
        return;
    }
    if (modifiers == Qt::NoModifier && key == Qt::Key_Backspace) {
        Q_EMIT recorded({});
        return;
    }
    if (const Qt::KeyboardModifier held = modifierOf(key); held != Qt::NoModifier) {
        showModifiers(modifiers | held);
        return;
    }
    if (const QString recorded = keyFor(modifiers, key); !recorded.isEmpty()) {
        Q_EMIT this->recorded(recorded);
    }
}

void KeyRecorder::keyReleaseEvent(QKeyEvent* event)
{
    event->accept();
    showModifiers(event->modifiers() & kUsedModifiers & ~modifierOf(event->key()));
}

bool KeyRecorder::focusNextPrevChild(bool /*next*/)
{
    return false; // Tab is a key to record, not a way out
}

void KeyRecorder::showModifiers(Qt::KeyboardModifiers modifiers)
{
    QStringList parts;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        parts << tr("Ctrl");
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        parts << tr("Alt");
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        parts << tr("Shift");
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        parts << tr("Win");
    }
    if (!parts.isEmpty()) {
        parts << u"..."_s;
    }
    setText(parts.join(u" + "_s));
}

} // namespace omnidict::ui
