#pragma once

#include <QLineEdit>
#include <QString>

namespace omnidict::ui {

/// Records a key combination for quick lookup where Omnidict registers the key itself
/// (Windows, mocks/windows.html note 2): shows the modifiers held so far ("Ctrl + Shift +
/// ..."), and takes a key once Ctrl, Alt or Win is held with a letter, a digit, F1 to F24 or
/// a named key. Escape cancels; Backspace alone turns the shortcut off.
class KeyRecorder : public QLineEdit
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(KeyRecorder)

public:
    explicit KeyRecorder(QWidget* parent = nullptr);
    ~KeyRecorder() override = default;

    /// The portable text ("Ctrl+Alt+D") for a press, or empty when it is not a key Omnidict
    /// can register: without Ctrl, Alt or Win, or a key outside the list above.
    [[nodiscard]] static QString keyFor(Qt::KeyboardModifiers modifiers, int key);

Q_SIGNALS:
    /// A key was recorded; empty when the user turned the shortcut off (Backspace).
    void recorded(const QString& key);
    void cancelled();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    void showModifiers(Qt::KeyboardModifiers modifiers);
};

} // namespace omnidict::ui
