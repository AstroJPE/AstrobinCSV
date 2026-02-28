#pragma once
#include <QDialog>
#include "models/targetgroup.h"
class QListWidget;
class QLineEdit;
class QPushButton;
class QLabel;

class ManageTargetsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ManageTargetsDialog(const QStringList &knownTargets,
                                 QWidget *parent = nullptr);
private slots:
    void onNewGroup();
    void onDeleteGroup();
    void onGroupSelected();
    void onAddMember();
    void onRemoveMember();
    void onSave();
    void onGroupNameEdited();
    void onAddKeyword();
    void onRemoveKeyword();
private:
    void populateGroupList();
    void populateAvailable();
    void populateMembers();
    void populateKeywordList();

    QListWidget *m_groupList{nullptr};
    QLineEdit   *m_groupNameEdit{nullptr};
    QListWidget *m_availableList{nullptr};
    QListWidget *m_memberList{nullptr};
    QPushButton *m_addMemberBtn{nullptr};
    QPushButton *m_removeMemberBtn{nullptr};

    // keyword list widgets
    QListWidget *m_keywordList{nullptr};
    QLineEdit   *m_keywordEdit{nullptr};

    QList<TargetGroup> m_groups;
    QStringList        m_keywords;
    QStringList        m_knownTargets;
    int                m_currentGroup{-1};
};
