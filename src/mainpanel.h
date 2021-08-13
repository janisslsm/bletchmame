/***************************************************************************

	mainpanel.h

	Main BletchMAME panel

***************************************************************************/

#ifndef MAINPANEL_H
#define MAINPANEL_H

#include <QTreeView>
#include <QTableView>

#include "iconloader.h"
#include "profile.h"
#include "prefs.h"
#include "softwarelist.h"


QT_BEGIN_NAMESPACE
namespace Ui { class MainPanel; }
class QLineEdit;
class QTableWidgetItem;
class QAbstractItemModel;
class QTableView;
class QSortFilterProxyModel;
QT_END_NAMESPACE

class info::database;
class MachineFolderTreeModel;
class ProfileListItemModel;
class SessionBehavior;
class SoftwareListItemModel;


// ======================> MainWindow

class MainPanel : public QWidget
{
	Q_OBJECT

public:
	MainPanel(info::database &infoDb, Preferences &prefs, std::function<void(const info::machine &, std::unique_ptr<SessionBehavior> &&)> &&runCallback, QWidget *parent = nullptr);
	~MainPanel();

	void pathsChanged(const std::vector<Preferences::global_path_type> &changedPaths);

private slots:
	void on_machinesFolderTreeView_customContextMenuRequested(const QPoint &pos);
	void on_machinesTableView_activated(const QModelIndex &index);
	void on_machinesTableView_customContextMenuRequested(const QPoint &pos);
	void on_softwareTableView_activated(const QModelIndex &index);
	void on_softwareTableView_customContextMenuRequested(const QPoint &pos);
	void on_profilesTableView_activated(const QModelIndex &index);
	void on_profilesTableView_customContextMenuRequested(const QPoint &pos);
	void on_tabWidget_currentChanged(int index);
	void on_machinesSplitter_splitterMoved(int pos, int index);

private:
	class SnapshotViewEventFilter;

	// variables configured at startup
	std::unique_ptr<Ui::MainPanel>													m_ui;
	Preferences &																	m_prefs;
	std::function<void(const info::machine &, std::unique_ptr<SessionBehavior> &&)>	m_runCallback;
	SoftwareListItemModel *															m_softwareListItemModel;
	ProfileListItemModel *															m_profileListItemModel;

	// information retrieved by -listxml
	info::database &																m_infoDb;

	// other
	QString																			m_currentSoftwareList;
	software_list_collection														m_softwareListCollection;
	IconLoader																		m_iconLoader;
	QPixmap																			m_currentSnapshot;
	std::vector<QString>															m_expandedTreeItems;

	// methods
	void run(const info::machine &machine, const software_list::software *software = nullptr);
	void run(std::shared_ptr<profiles::profile> &&profile);
	void run(const info::machine &machine, std::unique_ptr<SessionBehavior> &&sessionBehavior);
	void updateSoftwareList();
	void LaunchingListContextMenu(const QPoint &pos, const software_list::software *software = nullptr);
	void createProfile(const info::machine &machine, const software_list::software *software);
	static bool DirExistsOrMake(const QString &path);
	void duplicateProfile(const profiles::profile &profile);
	void deleteProfile(const profiles::profile &profile);
	void focusOnNewProfile(QString &&new_profile_path);
	void editSelection(QAbstractItemView &itemView);
	QString currentlySelectedCustomFolder() const;
	void deleteSelectedFolder();
	void showInGraphicalShell(const QString &path) const;
	info::machine machineFromModelIndex(const QModelIndex &index) const;
	const MachineFolderTreeModel &machineFolderTreeModel() const;
	MachineFolderTreeModel &machineFolderTreeModel();
	const QSortFilterProxyModel &sortFilterProxyModel(const QTableView &tableView) const;
	void machineFoldersTreeViewSelectionChanged(const QItemSelection &newSelection, const QItemSelection &oldSelection);
	void persistMachineSplitterSizes();
	void updateInfoPanel(const QString &machineName);
	void updateSnapshot();
	void identifyExpandedFolderTreeItems();
	static void iterateItemModelIndexes(QAbstractItemModel &model, const std::function<void(const QModelIndex &)> &func, const QModelIndex &index = QModelIndex());
};


#endif // MAINPANEL_H
