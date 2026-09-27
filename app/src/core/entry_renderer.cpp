#include "core/entry_renderer.h"

#include <QCoreApplication>
#include <QHash>
#include <QStringList>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace omnidict::core {

namespace {

constexpr int kMaxFrequency = 5;
constexpr qsizetype kMaxShownForms = 12;
const QString kRomanization = u"romanization"_s;
const QString kPronSeparator = u" · "_s;
const QString kListSeparator = u", "_s;

QString esc(const QString& text)
{
    return text.toHtmlEscaped();
}

QString tr(const char* text)
{
    return QCoreApplication::translate("omnidict::core::EntryRenderer", text);
}

/// Empty for `other` or an unknown tag: the heading then shows no part of speech.
QString posName(const QString& pos)
{
    static const QHash<QString, const char*> kNames = {
        {u"noun"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "noun")},
        {u"verb"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "verb")},
        {u"adj"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "adjective")},
        {u"adv"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "adverb")},
        {u"pron"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "pronoun")},
        {u"prep"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "preposition")},
        {u"conj"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "conjunction")},
        {u"interj"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "interjection")},
        {u"det"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "determiner")},
        {u"num"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "numeral")},
        {u"particle"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "particle")},
        {u"prefix"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "prefix")},
        {u"suffix"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "suffix")},
        {u"phrase"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "phrase")},
        {u"abbr"_s, QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "abbreviation")},
    };
    const char* name = kNames.value(pos, nullptr);
    return name != nullptr ? tr(name) : QString();
}

struct RelationType
{
    QString type;
    const char* name;
};

/// Display order of relation paragraphs.
const QList<RelationType>& relationTypes()
{
    static const QList<RelationType> kTypes = {
        {.type = u"synonym"_s, .name = QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "Synonyms")},
        {.type = u"antonym"_s, .name = QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "Antonyms")},
        {.type = u"see"_s, .name = QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "See also")},
        {.type = u"derived"_s, .name = QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "Derived terms")},
    };
    return kTypes;
}

/// One `rel` paragraph per type that has relations, in display order.
void appendRelations(QStringList& lines, const QList<Relation>& relations)
{
    for (const RelationType& type : relationTypes()) {
        QStringList links;
        for (const Relation& relation : relations) {
            if (relation.type == type.type) {
                const QString target = esc(relation.target);
                links << u"<a href=\"lex:"_s + target + u"\">"_s + target + u"</a>"_s;
            }
        }
        if (!links.isEmpty()) {
            lines << u"<p class=\"rel\"><span class=\"rel-type\">"_s + tr(type.name) + u"</span> "_s +
                         links.join(kListSeparator) + u"</p>"_s;
        }
    }
}

QString headwordLine(const Entry& entry)
{
    QString line = u"<p class=\"hw-line\"><span class=\"hw\">"_s + esc(entry.headword) + u"</span>"_s;
    for (const Form& form : entry.forms) {
        if (form.tag == kRomanization) {
            line += u" <span class=\"roman\">"_s + esc(form.form) + u"</span>"_s;
            break;
        }
    }
    if (entry.frequency >= 1 && entry.frequency <= kMaxFrequency) {
        line += u" <span class=\"freq\">"_s + QString(entry.frequency, QChar(0x25C6)) +
                QString(kMaxFrequency - entry.frequency, QChar(0x25C7)) + u"</span>"_s;
    }
    return line + u"</p>"_s;
}

void appendPronunciations(QStringList& lines, const QList<Pronunciation>& pronunciations)
{
    QStringList parts;
    for (const Pronunciation& pron : pronunciations) {
        if (pron.ipa.isEmpty()) {
            continue;
        }
        QString part;
        if (!pron.region.isEmpty()) {
            part = u"<span class=\"region\">"_s + esc(pron.region) + u"</span> "_s;
        }
        parts << part + u"<span class=\"ipa\">"_s + esc(pron.ipa) + u"</span>"_s;
    }
    if (!parts.isEmpty()) {
        lines << u"<p class=\"prons\">"_s + parts.join(kPronSeparator) + u"</p>"_s;
    }
}

void appendSense(QStringList& lines, const Sense& sense, const QList<Relation>& relations)
{
    lines << u"<div class=\"sense\">"_s;
    QString head =
        u"<p class=\"sense-head\"><span class=\"num\">"_s + QString::number(sense.ordinal) + u"</span>"_s;
    const QString pos = posName(sense.pos);
    if (!pos.isEmpty()) {
        head += u" <span class=\"pos\">"_s + pos + u"</span>"_s;
    }
    if (!sense.pattern.isEmpty()) {
        head += u" <span class=\"pattern\">["_s + esc(sense.pattern) + u"]</span>"_s;
    }
    lines << head + u"</p>"_s;

    QString definition = u"<p class=\"def\">"_s;
    if (!sense.label.isEmpty()) {
        definition += u"<span class=\"label\">"_s + esc(sense.label) + u"</span> "_s;
    }
    lines << definition + sense.definition + u"</p>"_s;

    if (!sense.examples.isEmpty()) {
        lines << u"<ul class=\"examples\">"_s;
        for (const Example& example : sense.examples) {
            QString item = u"<li><span class=\"ex\">"_s + example.text + u"</span>"_s;
            if (!example.translation.isEmpty()) {
                item += u"<br><span class=\"tr\">"_s + example.translation + u"</span>"_s;
            }
            lines << item + u"</li>"_s;
        }
        lines << u"</ul>"_s;
    }

    QList<Relation> own;
    for (const Relation& relation : relations) {
        if (relation.senseOrdinal == sense.ordinal) {
            own << relation;
        }
    }
    appendRelations(lines, own);
    lines << u"</div>"_s;
}

void appendForms(QStringList& lines, const QList<Form>& forms)
{
    QStringList shown;
    for (const Form& form : forms) {
        if (form.tag == kRomanization) {
            continue;
        }
        QString part = u"<span class=\"form\">"_s + esc(form.form) + u"</span>"_s;
        if (!form.tag.isEmpty()) {
            part += u" <span class=\"form-tag\">("_s + esc(form.tag) + u")</span>"_s;
        }
        shown << part;
        if (shown.size() == kMaxShownForms) {
            break;
        }
    }
    if (!shown.isEmpty()) {
        lines << u"<p class=\"forms\"><span class=\"rel-type\">"_s +
                     tr(QT_TRANSLATE_NOOP("omnidict::core::EntryRenderer", "Forms")) + u"</span> "_s +
                     shown.join(kListSeparator) + u"</p>"_s;
    }
}

} // namespace

QString renderEntry(const Entry& entry)
{
    QStringList lines;
    lines << u"<div class=\"entry\" lang=\""_s + esc(entry.lang) + u"\">"_s;
    lines << headwordLine(entry);
    appendPronunciations(lines, entry.pronunciations);

    for (const Sense& sense : entry.senses) {
        appendSense(lines, sense, entry.relations);
    }

    QList<Relation> entryLevel;
    for (const Relation& relation : entry.relations) {
        const bool matchesSense = std::ranges::any_of(
            entry.senses, [&](const Sense& sense) { return relation.senseOrdinal == sense.ordinal; });
        if (!matchesSense) {
            entryLevel << relation;
        }
    }
    appendRelations(lines, entryLevel);
    appendForms(lines, entry.forms);

    lines << u"</div>"_s;
    return lines.join(u'\n') + u'\n';
}

} // namespace omnidict::core
