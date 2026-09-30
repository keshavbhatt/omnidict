#include "ui/quick_lookup.h"

#include "core/entry.h"
#include "core/settings.h"
#include "ui/entry_view.h"
#include "ui/icons.h"
#include "ui/search_field.h"
#include "ui/style.h"

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {

constexpr int kWidth = 470;
constexpr int kHeight = 440;
constexpr int kTypingPauseMs = 150;
/// Closing (Escape, clicking elsewhere, the close button) rather than hiding: after a
/// cold `omnidict --popup` the popup is the only window, and closing it ends the app.
/// Waiting this long for the popup to become active before reading the selection
/// anyway: on X11 (and headless) the selection can be read without being active.
constexpr int kActivationWaitMs = 300;
constexpr int kMaxWordChars = 64;
constexpr int kMaxWords = 5;
constexpr int kAlsoInMax = 3;
constexpr int kSuggestions = 5;

/// Request ids of the popup's own: far above the main window's, which count up from 1.
quint64 nextRequestId()
{
    static quint64 next = quint64{1} << 62U;
    return ++next;
}

QString link(const QString& href, const QString& text)
{
    return u"<a href=\"%1\">%2</a>"_s.arg(href.toHtmlEscaped(), text.toHtmlEscaped());
}

} // namespace

QuickLookup::QuickLookup(services::LookupService* lookup, core::Settings& settings, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_lookup(lookup)
    , m_settings(settings)
    , m_typingPause(new QTimer(this))
{
    setObjectName(u"quickLookup"_s);
    setWindowTitle(titleWithApp(tr("Quick lookup")));
    setAttribute(Qt::WA_TranslucentBackground); // the frame below draws the rounded window
    resize(kWidth, kHeight);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* frame = new QFrame(this);
    frame->setProperty("quickPopup", true);
    root->addWidget(frame);
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildHeader(frame));

    auto* body = new QVBoxLayout;
    body->setContentsMargins(18, 4, 18, 10);
    body->setSpacing(6);
    m_dictionaryName = new QLabel(frame);
    m_dictionaryName->setObjectName(u"quickDictionary"_s);
    makeSectionLabel(m_dictionaryName);
    body->addWidget(m_dictionaryName);
    m_entry = new EntryView(frame);
    m_entry->setObjectName(u"quickEntry"_s);
    m_entry->setCompact(true);
    body->addWidget(m_entry, 1);
    m_message = new QLabel(frame);
    m_message->setObjectName(u"quickMessage"_s);
    m_message->setWordWrap(true);
    m_message->setTextFormat(Qt::RichText);
    m_message->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    connect(m_message, &QLabel::linkActivated, this, &QuickLookup::openLink);
    body->addWidget(m_message, 1);
    m_also = new QLabel(frame);
    m_also->setObjectName(u"quickAlso"_s);
    m_also->setProperty("muted", true);
    m_also->setTextFormat(Qt::RichText);
    connect(m_also, &QLabel::linkActivated, this, &QuickLookup::openLink);
    body->addWidget(m_also);
    layout->addLayout(body, 1);
    layout->addWidget(buildFooter(frame));
    m_message->hide();

    m_typingPause->setSingleShot(true);
    m_typingPause->setInterval(kTypingPauseMs);
    connect(m_typingPause, &QTimer::timeout, this, &QuickLookup::search);
    connect(m_search, &QLineEdit::textChanged, m_typingPause, qOverload<>(&QTimer::start));
    connect(m_search, &QLineEdit::returnPressed, this,
            [this] { Q_EMIT openInMain(m_search->text().trimmed()); });
    connect(m_entry, &EntryView::headwordActivated, this, &QuickLookup::lookUp);

    connect(lookup, &services::LookupService::searchFinished, this, &QuickLookup::onSearchFinished);
    connect(lookup, &services::LookupService::entryLoaded, this, &QuickLookup::onEntryLoaded);
}

QWidget* QuickLookup::buildHeader(QWidget* frame)
{
    auto* header = new QWidget(frame);
    header->setObjectName(u"quickHeader"_s);
    header->installEventFilter(this); // dragging its empty part moves the frameless window
    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);
    m_search = new SearchField(header);
    m_search->setObjectName(u"quickSearch"_s);
    m_search->setPlaceholderText(tr("Look up a word"));
    layout->addWidget(m_search, 1);
    auto* close = new QToolButton(header);
    close->setObjectName(u"quickClose"_s);
    close->setAutoRaise(true);
    close->setIcon(icons::themed(u"x"_s, Tokens::current().text));
    close->setToolTip(tr("Close (Escape)"));
    close->setAccessibleName(tr("Close"));
    connect(close, &QToolButton::clicked, this, &QWidget::close);
    layout->addWidget(close);
    return header;
}

QWidget* QuickLookup::buildFooter(QWidget* frame)
{
    auto* foot = new QFrame(frame);
    foot->setProperty("sheetFoot", true);
    auto* layout = new QHBoxLayout(foot);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(6);
    const auto hint = [foot, layout](const QString& key, const QString& text) {
        layout->addWidget(makeKeyCaps(key, foot));
        auto* label = new QLabel(text, foot);
        label->setProperty("muted", true);
        label->setProperty("small", true);
        layout->addWidget(label);
        layout->addSpacing(6);
    };
    hint(tr("Enter"), tr("open in Omnidict"));
    hint(tr("Esc"), tr("close"));
    layout->addStretch(1);
    auto* open = new QPushButton(tr("Open in Omnidict"), foot);
    open->setObjectName(u"quickOpen"_s);
    open->setProperty("small", true);
    connect(open, &QPushButton::clicked, this, [this] { Q_EMIT openInMain(m_search->text().trimmed()); });
    layout->addWidget(open);
    return foot;
}

void QuickLookup::setDictionaries(const QList<services::DictionaryInfo>& dictionaries)
{
    m_dictionaries = dictionaries;
}

void QuickLookup::present(const QString& word, const QByteArray& activationToken)
{
    m_wasActive = false;
    m_pickWordOnActivation = word.trimmed().isEmpty();
    if (!m_pickWordOnActivation) {
        lookUp(word.trimmed());
    }
    if (!activationToken.isEmpty()) {
        // Qt reads it once when the window asks to be activated (Wayland xdg-activation).
        qputenv("XDG_ACTIVATION_TOKEN", activationToken);
    }
    show();
    raise();
    activateWindow();
    m_search->setFocus();
    if (m_pickWordOnActivation) {
        // On Wayland the selection is only offered to the active window: wait for that,
        // but not forever (X11 and headless give it without).
        QTimer::singleShot(kActivationWaitMs, this, [this] {
            if (m_pickWordOnActivation) {
                pickInitialWord();
            }
        });
    }
}

void QuickLookup::lookUp(const QString& word)
{
    m_search->setText(word);
    m_search->selectAll(); // typing replaces it
    m_typingPause->stop();
    search();
}

QString QuickLookup::wordFrom(const QString& text)
{
    QString line = text.section(u'\n', 0, 0).simplified();
    if (line.isEmpty() || line.size() > kMaxWordChars || line.count(u' ') >= kMaxWords) {
        return {};
    }
    return line;
}

void QuickLookup::pickInitialWord()
{
    m_pickWordOnActivation = false;
    const QClipboard* clipboard = QGuiApplication::clipboard();
    QString word;
    if (clipboard->supportsSelection()) {
        word = wordFrom(clipboard->text(QClipboard::Selection));
    }
    if (word.isEmpty()) {
        word = wordFrom(clipboard->text(QClipboard::Clipboard)); // GNOME: copy, then the shortcut
    }
    if (!word.isEmpty()) {
        lookUp(word);
    }
}

void QuickLookup::search()
{
    const QString text = m_search->text().trimmed();
    m_searchRequest = nextRequestId();
    m_entryRequest = 0;
    if (text.isEmpty() || m_lookup == nullptr) {
        m_dictionaryName->clear();
        m_entry->showEntry({}, {});
        m_also->clear();
        return;
    }
    const core::SearchQuery query{.text = text,
                                  .perDictionary = 1,
                                  .fullTextPerDictionary = 0,
                                  .suggestions = m_settings.suggestSpellings() ? kSuggestions : 0,
                                  .order = m_settings.dictionaryOrder(),
                                  .excluded = m_settings.disabledDictionaries()};
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::search, Qt::QueuedConnection,
                              m_searchRequest, query);
}

void QuickLookup::onSearchFinished(quint64 requestId, const core::SearchResults& results,
                                   const QStringList& /*favorites*/)
{
    if (requestId != m_searchRequest) {
        return; // the main window's, or typed on since
    }
    if (results.headwords.isEmpty() || results.headwords.first().rows.isEmpty()) {
        showNothingFound(results.text.trimmed(), results.suggestions);
        return;
    }
    const core::ResultGroup& best = results.headwords.first();
    m_message->hide();
    m_entry->show();
    m_dictionaryName->setText(best.dictName);
    showOtherDictionaries(best.rows.first().headword, results);
    m_entryRequest = nextRequestId();
    QMetaObject::invokeMethod(m_lookup, &services::LookupService::loadEntry, Qt::QueuedConnection,
                              m_entryRequest, best.dictId, best.rows.first().id);
}

void QuickLookup::showOtherDictionaries(const QString& headword, const core::SearchResults& results)
{
    QStringList links;
    for (qsizetype i = 1; i < results.headwords.size() && links.size() < kAlsoInMax; ++i) {
        const core::ResultGroup& group = results.headwords.at(i);
        if (!group.rows.isEmpty() &&
            group.rows.first().headword.compare(headword, Qt::CaseInsensitive) == 0) {
            links << link(u"entry:%1/%2"_s.arg(group.dictId).arg(group.rows.first().id), group.dictName);
        }
    }
    m_also->setText(links.isEmpty() ? QString() : tr("Also in %1").arg(links.join(u", "_s)));
}

void QuickLookup::showNothingFound(const QString& text, const QList<core::SuggestedWord>& suggestions)
{
    m_dictionaryName->clear();
    m_also->clear();
    m_entry->hide();
    QString html = u"<p>%1</p>"_s.arg(tr("\"%1\" is not in your dictionaries.").arg(text.toHtmlEscaped()));
    if (!suggestions.isEmpty()) {
        QStringList words;
        for (const core::SuggestedWord& suggestion : suggestions) {
            words << link(u"word:%1"_s.arg(suggestion.entry.headword), suggestion.entry.headword);
        }
        words.removeDuplicates();
        html += u"<p>%1</p>"_s.arg(tr("Did you mean %1?").arg(words.join(u", "_s)));
    }
    m_message->setText(html);
    m_message->show();
}

void QuickLookup::onEntryLoaded(quint64 requestId, const QString& dictId, const core::Entry& entry,
                                const QString& html, bool /*favorite*/)
{
    if (requestId != m_entryRequest) {
        return;
    }
    const services::DictionaryInfo* info = dictionary(dictId);
    if (info != nullptr) {
        m_dictionaryName->setText(info->name);
    }
    m_entry->showEntry(html, info != nullptr ? info->attribution : QString());
    Q_EMIT entryShown(dictId, entry.headword);
}

void QuickLookup::openLink(const QString& href)
{
    if (href.startsWith(u"word:"_s)) {
        lookUp(href.mid(5));
        return;
    }
    if (href.startsWith(u"entry:"_s)) {
        // entry:<dictId>/<entryId>: that dictionary's entry for the same word.
        const QString rest = href.mid(6);
        const QString dictId = rest.section(u'/', 0, 0);
        const qint64 entryId = rest.section(u'/', 1, 1).toLongLong();
        m_entryRequest = nextRequestId();
        QMetaObject::invokeMethod(m_lookup, &services::LookupService::loadEntry, Qt::QueuedConnection,
                                  m_entryRequest, dictId, entryId);
    }
}

const services::DictionaryInfo* QuickLookup::dictionary(const QString& dictId) const
{
    const auto it = std::ranges::find_if(
        m_dictionaries, [&](const services::DictionaryInfo& info) { return info.dictId == dictId; });
    return it != m_dictionaries.end() ? &(*it) : nullptr;
}

void QuickLookup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void QuickLookup::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::ActivationChange) {
        return;
    }
    if (isActiveWindow()) {
        m_wasActive = true;
        if (m_pickWordOnActivation) {
            pickInitialWord(); // Wayland offers the selection now
        }
        return;
    }
    if (m_wasActive && isVisible()) {
        close(); // clicked elsewhere: a popup, not a window to keep around
    }
}

bool QuickLookup::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        if (QWindow* window = windowHandle()) {
            window->startSystemMove(); // no title bar to drag by
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace omnidict::ui
