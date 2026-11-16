#include "ui/pro_prompt.h"

#include "ui/icons.h"
#include "ui/style.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace omnidict::ui {

namespace {
constexpr int kWidth = 520;
} // namespace

DictionaryProPrompt::DictionaryProPrompt(services::ProStatus& pro, const QString& onName,
                                         const QString& newName, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(u"dictionaryProPrompt"_s);
    setWindowTitle(titleWithApp(tr("Omnidict Pro")));
    setWindowModality(Qt::WindowModal);
    setFixedWidth(kWidth);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto* body = new QVBoxLayout;
    body->setContentsMargins(20, 20, 20, 16);
    body->setSpacing(12);
    auto* title = new QLabel(tr("Search more than one dictionary with Pro"), this);
    title->setProperty("heading", true);
    title->setWordWrap(true);
    body->addWidget(title);
    auto* text =
        new QLabel(tr("Omnidict Free searches one dictionary at a time, and %1 is on. Use %2 instead, or "
                      "get Pro to search all of them at once.")
                       .arg(onName, newName),
                   this);
    text->setObjectName(u"dictionaryProPromptText"_s);
    text->setWordWrap(true);
    body->addWidget(text);
    auto* kept = new QLabel(tr("%1 stays installed either way.").arg(newName), this);
    kept->setProperty("muted", true);
    kept->setProperty("small", true);
    kept->setWordWrap(true);
    body->addWidget(kept);
    root->addLayout(body, 1);

    root->addWidget(buildFooter(pro, newName));
}

QWidget* DictionaryProPrompt::buildFooter(services::ProStatus& pro, const QString& newName)
{
    auto* foot = new QFrame(this);
    foot->setProperty("sheetFoot", true);
    auto* layout = new QHBoxLayout(foot);
    layout->setContentsMargins(20, 12, 20, 12);
    layout->setSpacing(8);
    auto* whatPro = new QPushButton(tr("What Pro adds"), foot);
    whatPro->setObjectName(u"promptWhatPro"_s);
    whatPro->setProperty("linkButton", true);
    whatPro->setCursor(Qt::PointingHandCursor);
    connect(whatPro, &QPushButton::clicked, this, [this] {
        m_choice = Choice::WhatProAdds;
        accept();
    });
    layout->addWidget(whatPro);
    layout->addStretch(1);
    auto* swap = new QPushButton(tr("Use %1 instead").arg(newName), foot);
    swap->setObjectName(u"promptSwap"_s);
    connect(swap, &QPushButton::clicked, this, [this] {
        m_choice = Choice::Swap;
        accept();
    });
    layout->addWidget(swap);
    auto* plan = new QPushButton(icons::themed(u"external"_s, Tokens::current().accentText),
                                 tr("Choose a plan"), foot);
    plan->setObjectName(u"promptPlan"_s);
    plan->setProperty("primary", true);
    plan->setDefault(true);
    connect(plan, &QPushButton::clicked, this, [this, &pro] {
        m_choice = Choice::Plan;
        QDesktopServices::openUrl(pro.checkoutUrl(services::ProStatus::Source::Dictionary));
        accept();
    });
    layout->addWidget(plan);
    return foot;
}

} // namespace omnidict::ui
