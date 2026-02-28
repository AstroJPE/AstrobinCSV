#include "managetargets.h"
#include "settings/appsettings.h"
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>

ManageTargetsDialog::ManageTargetsDialog(const QStringList &knownTargets,
                                         QWidget *parent)
    : QDialog(parent), m_knownTargets(knownTargets)
{
    setWindowTitle(tr("Manage Targets"));
    setMinimumSize(600, 420);

    m_groups   = AppSettings::instance().targetGroups();
    m_keywords = AppSettings::instance().targetKeywords();

    auto *outerLay = new QVBoxLayout(this);

    // ── Target Keywords ──────────────────────────────────────────────────────
    auto *kwBox  = new QGroupBox(tr("Target Keywords"));
    auto *kwLay  = new QVBoxLayout(kwBox);

    auto *kwNote = new QLabel(tr(
        "<i>These WBPP Grouping Keywords are searched in the WBPP log to extract "
        "the target name and are used to keep separate integration groups distinct. "
        "When a keyword match is found, it takes priority over the <b>OBJECT</b> "
        "tag in the .xisf / FITS file headers. If no keywords are listed here, "
        "the <b>OBJECT</b> tag is used directly.</i>"));
    kwNote->setWordWrap(true);
    kwLay->addWidget(kwNote);

    m_keywordList = new QListWidget;
    m_keywordList->setFixedHeight(80);
    m_keywordList->setSelectionMode(QAbstractItemView::SingleSelection);
    kwLay->addWidget(m_keywordList);

    auto *kwBtnRow  = new QHBoxLayout;
    m_keywordEdit   = new QLineEdit;
    m_keywordEdit->setPlaceholderText(tr("Keyword, e.g. TARGET"));
    m_keywordEdit->setMaximumWidth(180);
    auto *kwAddBtn  = new QPushButton(tr("Add"));
    auto *kwDelBtn  = new QPushButton(tr("Remove Selected"));
    kwBtnRow->addWidget(m_keywordEdit);
    kwBtnRow->addWidget(kwAddBtn);
    kwBtnRow->addWidget(kwDelBtn);
    kwBtnRow->addStretch();
    kwLay->addLayout(kwBtnRow);

    outerLay->addWidget(kwBox);

    connect(kwAddBtn, &QPushButton::clicked,
            this, &ManageTargetsDialog::onAddKeyword);
    connect(kwDelBtn, &QPushButton::clicked,
            this, &ManageTargetsDialog::onRemoveKeyword);

    populateKeywordList();
    // ─────────────────────────────────────────────────────────────────────────

    auto *lay = new QHBoxLayout;

    auto *leftLay = new QVBoxLayout;
    leftLay->addWidget(new QLabel(tr("Target Groups:")));
    m_groupList = new QListWidget;
    leftLay->addWidget(m_groupList, 1);
    auto *grpBtnRow = new QHBoxLayout;
    auto *newBtn = new QPushButton(tr("+ New Group"));
    auto *delBtn = new QPushButton(tr("− Delete Group"));
    grpBtnRow->addWidget(newBtn);
    grpBtnRow->addWidget(delBtn);
    leftLay->addLayout(grpBtnRow);
    lay->addLayout(leftLay, 1);

    auto *rightLay = new QVBoxLayout;

    auto *nameBox = new QGroupBox(tr("Astrobin Target Name"));
    auto *nameLay = new QHBoxLayout(nameBox);
    m_groupNameEdit = new QLineEdit;
    m_groupNameEdit->setPlaceholderText(tr("e.g. IC 2177"));
    nameLay->addWidget(m_groupNameEdit);
    rightLay->addWidget(nameBox);

    auto *memberBox = new QGroupBox(tr("Member Log Targets"));
    auto *memberLay = new QHBoxLayout(memberBox);

    auto *availLay = new QVBoxLayout;
    availLay->addWidget(new QLabel(tr("Available (from log):")));
    m_availableList = new QListWidget;
    m_availableList->setToolTip(tr("Double-click to add to group"));
    availLay->addWidget(m_availableList, 1);
    memberLay->addLayout(availLay, 1);

    auto *arrowLay = new QVBoxLayout;
    arrowLay->addStretch();
    m_addMemberBtn    = new QPushButton(tr("→ Add"));
    m_removeMemberBtn = new QPushButton(tr("← Remove"));
    arrowLay->addWidget(m_addMemberBtn);
    arrowLay->addWidget(m_removeMemberBtn);
    arrowLay->addStretch();
    memberLay->addLayout(arrowLay);

    auto *membLay = new QVBoxLayout;
    membLay->addWidget(new QLabel(tr("In this group:")));
    m_memberList = new QListWidget;
    m_memberList->setToolTip(tr("Double-click to remove from group"));
    membLay->addWidget(m_memberList, 1);
    memberLay->addLayout(membLay, 1);

    rightLay->addWidget(memberBox, 1);
    lay->addLayout(rightLay, 2);

    outerLay->addLayout(lay, 1);
    auto *bbox = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    outerLay->addWidget(bbox);

    connect(newBtn, &QPushButton::clicked,
            this, &ManageTargetsDialog::onNewGroup);
    connect(delBtn, &QPushButton::clicked,
            this, &ManageTargetsDialog::onDeleteGroup);
    connect(m_groupList, &QListWidget::currentRowChanged,
            this, &ManageTargetsDialog::onGroupSelected);
    connect(m_addMemberBtn, &QPushButton::clicked,
            this, &ManageTargetsDialog::onAddMember);
    connect(m_removeMemberBtn, &QPushButton::clicked,
            this, &ManageTargetsDialog::onRemoveMember);
    connect(m_availableList, &QListWidget::itemDoubleClicked,
            this, &ManageTargetsDialog::onAddMember);
    connect(m_memberList, &QListWidget::itemDoubleClicked,
            this, &ManageTargetsDialog::onRemoveMember);
    connect(m_groupNameEdit, &QLineEdit::textEdited,
            this, &ManageTargetsDialog::onGroupNameEdited);
    connect(bbox, &QDialogButtonBox::accepted,
            this, &ManageTargetsDialog::onSave);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    populateGroupList();
}

void ManageTargetsDialog::populateGroupList() {
    m_groupList->clear();
    for (const auto &tg : m_groups)
        m_groupList->addItem(tg.astrobinName.isEmpty()
                                 ? tr("(unnamed)") : tg.astrobinName);
}

void ManageTargetsDialog::populateAvailable() {
    m_availableList->clear();
    if (m_currentGroup < 0) return;
    const QStringList &members = m_groups[m_currentGroup].memberTargets;
    for (const auto &t : m_knownTargets)
        if (!members.contains(t, Qt::CaseInsensitive))
            m_availableList->addItem(t);
}

void ManageTargetsDialog::populateMembers() {
    m_memberList->clear();
    if (m_currentGroup < 0) return;
    for (const auto &m : m_groups[m_currentGroup].memberTargets)
        m_memberList->addItem(m);
}

void ManageTargetsDialog::onGroupSelected() {
    m_currentGroup = m_groupList->currentRow();
    if (m_currentGroup < 0 || m_currentGroup >= m_groups.size()) {
        m_groupNameEdit->clear();
        m_availableList->clear();
        m_memberList->clear();
        return;
    }
    m_groupNameEdit->setText(m_groups[m_currentGroup].astrobinName);
    populateAvailable();
    populateMembers();
}

void ManageTargetsDialog::onGroupNameEdited() {
    if (m_currentGroup < 0 || m_currentGroup >= m_groups.size()) return;
    m_groups[m_currentGroup].astrobinName =
        m_groupNameEdit->text().trimmed();
    m_groupList->item(m_currentGroup)->setText(
        m_groups[m_currentGroup].astrobinName.isEmpty()
            ? tr("(unnamed)")
            : m_groups[m_currentGroup].astrobinName);
}

void ManageTargetsDialog::onNewGroup() {
    TargetGroup tg;
    m_groups << tg;
    m_groupList->addItem(tr("(unnamed)"));
    m_groupList->setCurrentRow(m_groups.size() - 1);
    m_groupNameEdit->setFocus();
}

void ManageTargetsDialog::onDeleteGroup() {
    int row = m_groupList->currentRow();
    if (row < 0 || row >= m_groups.size()) return;
    m_groups.removeAt(row);
    m_groupList->takeItem(row);
    m_currentGroup = -1;
}

void ManageTargetsDialog::onAddMember() {
    if (m_currentGroup < 0) return;
    auto *item = m_availableList->currentItem();
    if (!item) return;
    m_groups[m_currentGroup].memberTargets << item->text();
    populateAvailable();
    populateMembers();
}

void ManageTargetsDialog::onRemoveMember() {
    if (m_currentGroup < 0) return;
    auto *item = m_memberList->currentItem();
    if (!item) return;
    m_groups[m_currentGroup].memberTargets.removeAll(item->text());
    populateAvailable();
    populateMembers();
}

void ManageTargetsDialog::onSave() {
    AppSettings::instance().setTargetGroups(m_groups);
    AppSettings::instance().setTargetKeywords(m_keywords);
    accept();
}

void ManageTargetsDialog::populateKeywordList()
{
    m_keywordList->clear();
    for (const QString &kw : std::as_const(m_keywords))
        m_keywordList->addItem(kw);
}

void ManageTargetsDialog::onAddKeyword()
{
    QString kw = m_keywordEdit->text().trimmed().toUpper();
    if (kw.isEmpty()) return;
    for (const QString &existing : std::as_const(m_keywords))
        if (existing.compare(kw, Qt::CaseInsensitive) == 0) return;
    m_keywords << kw;
    m_keywordEdit->clear();
    populateKeywordList();
}

void ManageTargetsDialog::onRemoveKeyword()
{
    int row = m_keywordList->currentRow();
    if (row < 0 || row >= m_keywords.size()) return;
    m_keywords.removeAt(row);
    populateKeywordList();
}
