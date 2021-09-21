/***************************************************************************

    dialogs/paths.cpp

    Paths dialog

***************************************************************************/

#include <QPushButton>
#include <QFileDialog>
#include <QTextStream>
#include <QStringListModel>

#include "dialogs/paths.h"
#include "ui_paths.h"
#include "assetfinder.h"
#include "prefs.h"
#include "utility.h"


//**************************************************************************
//  TYPES
//**************************************************************************

class PathsDialog::PathListModel : public QAbstractListModel
{
private:
	struct Entry;

public:
	PathListModel(QObject &parent, std::function<QString(const QString &)> &&applySubstitutionsFunc);

	// accessors
	bool isExpandable() const;

	// setting/getting full paths
	void setPaths(const QString &paths, bool applySubstitutions, Preferences::PathCategory pathCategory);
	QString paths() const;

	// setting/getting individual paths
	void setPath(int index, QString &&path);
	const QString &path(int index) const;
	int pathCount() const;
	void insert(int index);
	void erase(int index);

	// virtuals
	virtual int rowCount(const QModelIndex &parent) const;
	virtual QVariant data(const QModelIndex &index, int role) const;
	virtual bool setData(const QModelIndex &index, const QVariant &value, int role);
	virtual Qt::ItemFlags flags(const QModelIndex &index) const;

private:
	struct Entry
	{
		Entry(QString &&path = "", bool isValid = true);

		QString			m_path;
		bool			m_isValid;
	};

	std::function<QString(const QString &)>	m_applySubstitutionsFunc;
	bool									m_applySubstitutions;
	Preferences::PathCategory				m_pathCategory;
	std::vector<Entry>						m_entries;

	// private methods
	bool validateAndCanonicalize(QString &path) const;
	bool hasExpandEntry() const;
};


//**************************************************************************
//  IMPLEMENTATION
//**************************************************************************

const QStringList PathsDialog::s_combo_box_strings = buildComboBoxStrings();


//-------------------------------------------------
//  ctor
//-------------------------------------------------

PathsDialog::PathsDialog(QWidget &parent, Preferences &prefs)
	: QDialog(&parent)
	, m_prefs(prefs)
	, m_listViewModelCurrentPath({ })
{
	m_ui = std::make_unique<Ui::PathsDialog>();
	m_ui->setupUi(this);

	// path data
	for (size_t i = 0; i < PATH_COUNT; i++)
		m_pathLists[i] = m_prefs.getGlobalPath(static_cast<Preferences::global_path_type>(i));

	// list view
	PathListModel &model = *new PathListModel(*this, [this](const QString &path) { return m_prefs.applySubstitutions(path); });
	m_ui->listView->setModel(&model);

	// combo box
	QStringListModel &comboBoxModel = *new QStringListModel(s_combo_box_strings, this);
	m_ui->comboBox->setModel(&comboBoxModel);

	// listen to selection changes
	connect(m_ui->listView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection &, const QItemSelection &)
	{
		updateButtonsEnabled();
	});
	connect(&model, &QAbstractItemModel::modelReset, this, [this]()
	{
		updateButtonsEnabled();
	});
	updateButtonsEnabled();
}


//-------------------------------------------------
//  dtor
//-------------------------------------------------

PathsDialog::~PathsDialog()
{
}


//-------------------------------------------------
//  persist
//-------------------------------------------------

std::vector<Preferences::global_path_type> PathsDialog::persist()
{
	// first we want to make sure that m_pathLists is up to data
	extractPathsFromListView();

	// we want to return a vector identifying the paths that got changed
	std::vector<Preferences::global_path_type> changedPaths;
	for (Preferences::global_path_type type : util::all_enums<Preferences::global_path_type>())
	{
		QString &path = m_pathLists[static_cast<size_t>(type)];

		// has this path changed?
		if (path != m_prefs.getGlobalPath(type))
		{
			// if so, record that it changed
			m_prefs.setGlobalPath(type, std::move(path));
			changedPaths.push_back(type);
		}
	}
	return changedPaths;
}


//-------------------------------------------------
//  on_comboBox_currentIndexChanged
//-------------------------------------------------

void PathsDialog::on_comboBox_currentIndexChanged(int index)
{
	updateCurrentPathList();
}


//-------------------------------------------------
//  on_browseButton_clicked
//-------------------------------------------------

void PathsDialog::on_browseButton_clicked()
{
	int item = getSingularSelection();
	browseForPath(item);
}


//-------------------------------------------------
//  on_insertButton_clicked
//-------------------------------------------------

void PathsDialog::on_insertButton_clicked()
{
	// identify the correct item to edit
	int item = getSingularSelection();

	// if we're not appending, insert an item
	if (item < listModel().pathCount())
		listModel().insert(item);

	// and tell the list view to start editing
	QModelIndex index = listModel().index(item);
	m_ui->listView->edit(index);
}


//-------------------------------------------------
//  on_deleteButton_clicked
//-------------------------------------------------

void PathsDialog::on_deleteButton_clicked()
{
	QModelIndexList selectedIndexes = m_ui->listView->selectionModel()->selectedIndexes();
	for (const QModelIndex &selectedIndex : selectedIndexes)
	{
		// is this a "real" row?
		if (selectedIndex.row() < listModel().pathCount())
		{
			// if so, delete it
			listModel().erase(selectedIndex.row());
		}
	}
}


//-------------------------------------------------
//  on_listView_activated
//-------------------------------------------------

void PathsDialog::on_listView_activated(const QModelIndex &index)
{
	browseForPath(index.row());
}


//-------------------------------------------------
//  updateButtonsEnabled
//-------------------------------------------------

void PathsDialog::updateButtonsEnabled()
{	
	// get the selection
	QModelIndexList selectedIndexes = m_ui->listView->selectionModel()->selectedIndexes();
	int selectedCount = selectedIndexes.count();

	// are we selecting any bonafide paths?
	bool selectedAnyPaths = false;
	for (const QModelIndex &selectedIndex : selectedIndexes)
	{
		// only real paths count
		if (selectedIndex.row() < listModel().pathCount())
		{
			selectedAnyPaths = true;
			break;
		}
	}

	// set the buttons accordingly
	m_ui->browseButton->setEnabled(selectedCount <= 1);
	m_ui->insertButton->setEnabled(selectedCount <= 1 && (listModel().isExpandable() || listModel().pathCount() == 0));
	m_ui->deleteButton->setEnabled(selectedAnyPaths);
}


//-------------------------------------------------
//  getSingularSelection - identify the "one"
//	selected item, or the final ghost element if
//	there is none
//-------------------------------------------------

int PathsDialog::getSingularSelection() const
{
	int selectedItem;
	if (listModel().isExpandable())
	{
		// if we're expandable, we choose the selection only if there is just one; otherwise
		// use the ghost append item
		QModelIndexList selectedIndexes = m_ui->listView->selectionModel()->selectedIndexes();
		selectedItem = selectedIndexes.count() == 1
			? selectedIndexes[0].row()
			: listModel().pathCount();
	}
	else
	{
		// non expandable, we're always selecting zero
		selectedItem = 0;
	}
	return selectedItem;
}


//-------------------------------------------------
//  browseForPath
//-------------------------------------------------

bool PathsDialog::browseForPath(int index)
{
	// show the file dialog
	QString path = browseForPathDialog(
		*this,
		getCurrentPath(),
		index < listModel().pathCount() ? listModel().path(index) : "");
	if (path.isEmpty())
		return false;

	// specify it
	listModel().setPath(index, std::move(path));
	return true;
}


//-------------------------------------------------
//  extractPathsFromListView
//-------------------------------------------------

void PathsDialog::extractPathsFromListView()
{
	if (m_listViewModelCurrentPath.has_value())
	{
		// reflect changes on the m_current_path_list back into m_pathLists 
		QString paths = listModel().paths();
		m_pathLists[(int)m_listViewModelCurrentPath.value()] = std::move(paths);
	}
}


//-------------------------------------------------
//  updateCurrentPathList
//-------------------------------------------------

void PathsDialog::updateCurrentPathList()
{
	const Preferences::global_path_type currentPathType = getCurrentPath();
	bool applySubstitutions = currentPathType != Preferences::global_path_type::EMU_EXECUTABLE;
	Preferences::PathCategory category = Preferences::getPathCategory(currentPathType);
	
	listModel().setPaths(
		m_pathLists[(int)currentPathType],
		applySubstitutions,
		category);

	m_listViewModelCurrentPath = currentPathType;
}


//-------------------------------------------------
//  buildComboBoxStrings
//-------------------------------------------------

QStringList PathsDialog::buildComboBoxStrings()
{
	std::array<QString, PATH_COUNT> paths;
	paths[(size_t)Preferences::global_path_type::EMU_EXECUTABLE]	= "MAME Executable";
	paths[(size_t)Preferences::global_path_type::ROMS]				= "ROMs";
	paths[(size_t)Preferences::global_path_type::SAMPLES]			= "Samples";
	paths[(size_t)Preferences::global_path_type::CONFIG]			= "Config Files";
	paths[(size_t)Preferences::global_path_type::NVRAM]				= "NVRAM Files";
	paths[(size_t)Preferences::global_path_type::HASH]				= "Hash Files";
	paths[(size_t)Preferences::global_path_type::ARTWORK]			= "Artwork Files";
	paths[(size_t)Preferences::global_path_type::ICONS]				= "Icons";
	paths[(size_t)Preferences::global_path_type::PLUGINS]			= "Plugins";
	paths[(size_t)Preferences::global_path_type::PROFILES]			= "Profiles";
	paths[(size_t)Preferences::global_path_type::CHEATS]			= "Cheats";
	paths[(size_t)Preferences::global_path_type::SNAPSHOTS]			= "Snapshots";

	QStringList result;
	for (QString &str : paths)
		result.push_back(std::move(str));
	return result;
}


//-------------------------------------------------
//  getCurrentPath
//-------------------------------------------------

Preferences::global_path_type PathsDialog::getCurrentPath() const
{
	int selection = m_ui->comboBox->currentIndex();
	return static_cast<Preferences::global_path_type>(selection);
}


//-------------------------------------------------
//  isDirPathType
//-------------------------------------------------

bool PathsDialog::isDirPathType(Preferences::global_path_type type)
{
	return type == Preferences::global_path_type::ROMS
		|| type == Preferences::global_path_type::SAMPLES
		|| type == Preferences::global_path_type::CONFIG
		|| type == Preferences::global_path_type::NVRAM
		|| type == Preferences::global_path_type::HASH
		|| type == Preferences::global_path_type::ARTWORK
		|| type == Preferences::global_path_type::ICONS
		|| type == Preferences::global_path_type::PLUGINS
		|| type == Preferences::global_path_type::PROFILES
		|| type == Preferences::global_path_type::CHEATS
		|| type == Preferences::global_path_type::SNAPSHOTS;
}


//-------------------------------------------------
//  browseForPathDialog
//-------------------------------------------------

QString PathsDialog::browseForPathDialog(QWidget &parent, Preferences::global_path_type type, const QString &default_path)
{
	QString caption, filter;
	switch (type)
	{
	case Preferences::global_path_type::EMU_EXECUTABLE:
		caption = "Specify MAME Path";
#ifdef Q_OS_WIN32
		filter = "EXE files (*.exe);*.exe";
#endif
		break;

	default:
		caption = "Specify Path";
		break;
	};

	// determine the FileMode
	QFileDialog::FileMode fileMode;
	switch (Preferences::getPathCategory(type))
	{
	case Preferences::PathCategory::SingleFile:
		fileMode = QFileDialog::FileMode::ExistingFile;
		break;

	case Preferences::PathCategory::SingleDirectory:
	case Preferences::PathCategory::MultipleDirectories:
	case Preferences::PathCategory::MultipleDirectoriesOrArchives:
		fileMode = QFileDialog::FileMode::Directory;
		break;

	default:
		throw false;
	}

	// show the dialog
	QFileDialog dialog(&parent, caption, default_path, filter);
	dialog.setFileMode(fileMode);
	dialog.setAcceptMode(QFileDialog::AcceptMode::AcceptOpen);
	dialog.exec();
	return dialog.result() == QDialog::DialogCode::Accepted
		? dialog.selectedFiles().first()
		: QString();
}


//-------------------------------------------------
//  listModel
//-------------------------------------------------

PathsDialog::PathListModel &PathsDialog::listModel()
{
	return *dynamic_cast<PathListModel *>(m_ui->listView->model());
}


//-------------------------------------------------
//  listModel
//-------------------------------------------------

const PathsDialog::PathListModel &PathsDialog::listModel() const
{
	return *dynamic_cast<const PathListModel *>(m_ui->listView->model());
}


//-------------------------------------------------
//  PathsDialog ctor
//-------------------------------------------------

PathsDialog::PathListModel::PathListModel(QObject &parent, std::function<QString(const QString &)> &&applySubstitutionsFunc)
	: QAbstractListModel(&parent)
	, m_applySubstitutionsFunc(std::move(applySubstitutionsFunc))
{
}


//-------------------------------------------------
//  PathListModel::setPaths
//-------------------------------------------------

void PathsDialog::PathListModel::setPaths(const QString &paths, bool applySubstitutions, Preferences::PathCategory pathCategory)
{
	beginResetModel();

	m_applySubstitutions = applySubstitutions;
	m_pathCategory = pathCategory;
	m_entries.clear();
	if (!paths.isEmpty())
	{
		for (const QString &nativePath : util::string_split(paths, [](auto ch) { return ch == ';'; }))
		{
			QString path = QDir::fromNativeSeparators(nativePath);
			Entry &entry = m_entries.emplace_back();
			entry.m_isValid = validateAndCanonicalize(path);
			entry.m_path = std::move(path);
		}
	}

	endResetModel();
}


//-------------------------------------------------
//  PathListModel::isExpandable
//-------------------------------------------------

bool PathsDialog::PathListModel::isExpandable() const
{
	return m_pathCategory == Preferences::PathCategory::MultipleDirectories
		|| m_pathCategory == Preferences::PathCategory::MultipleDirectoriesOrArchives;
}


//-------------------------------------------------
//  PathListModel::paths
//-------------------------------------------------

QString PathsDialog::PathListModel::paths() const
{
	QString result;
	{
		bool isFirst = true;
		QTextStream textStream(&result);
		for (const Entry &entry : m_entries)
		{
			// ignore empty paths
			if (!entry.m_path.isEmpty())
			{
				if (isFirst)
					isFirst = false;
				else
					textStream << ';';
				textStream << QDir::toNativeSeparators(entry.m_path);
			}
		}
	}
	return result;
}


//-------------------------------------------------
//  PathListModel::setPath
//-------------------------------------------------

void PathsDialog::PathListModel::setPath(int index, QString &&path)
{
	// sanity check
	int maxIndex = util::safe_static_cast<int>(m_entries.size()) + (hasExpandEntry() ? 1 : 0);
	if (index < 0 || index >= maxIndex)
		throw false;

	if (path.isEmpty())
	{
		// setting an empty path clears it out
		erase(index);
	}
	else
	{
		// we're updating or inserting something - tell Qt that something is changing
		beginResetModel();

		// are we appending?  if so create a new entry
		if (index == m_entries.size())
		{
			assert(hasExpandEntry());
			m_entries.insert(m_entries.end(), Entry("", true));
		}

		// set the path
		m_entries[index].m_isValid = validateAndCanonicalize(path);
		m_entries[index].m_path = std::move(path);

		// we're done!
		endResetModel();
	}
}


//-------------------------------------------------
//  PathListModel::path
//-------------------------------------------------

const QString &PathsDialog::PathListModel::path(int index) const
{
	assert(index >= 0 && index < m_entries.size());
	return m_entries[index].m_path;
}


//-------------------------------------------------
//  PathListModel::pathCount
//-------------------------------------------------

int PathsDialog::PathListModel::pathCount() const
{
	return util::safe_static_cast<int>(m_entries.size());
}


//-------------------------------------------------
//  PathListModel::insert
//-------------------------------------------------

void PathsDialog::PathListModel::insert(int position)
{
	assert(position >= 0 && position <= m_entries.size());

	beginResetModel();
	m_entries.insert(m_entries.begin() + position, Entry("", true));
	endResetModel();
}


//-------------------------------------------------
//  PathListModel::erase
//-------------------------------------------------

void PathsDialog::PathListModel::erase(int position)
{
	assert(position >= 0 && position < m_entries.size());

	beginResetModel();
	m_entries.erase(m_entries.begin() + position);
	endResetModel();
}


//-------------------------------------------------
//  PathListModel::rowCount
//-------------------------------------------------

int PathsDialog::PathListModel::rowCount(const QModelIndex &parent) const
{
	return pathCount() + (hasExpandEntry() ? 1 : 0);
}


//-------------------------------------------------
//  PathListModel::data
//-------------------------------------------------

QVariant PathsDialog::PathListModel::data(const QModelIndex &index, int role) const
{
	// identify the entry
	const Entry *entry = index.row() < m_entries.size()
		? &m_entries[index.row()]
		: nullptr;

	QVariant result;
	switch (role)
	{
	case Qt::DisplayRole:
		if (entry)
			result = QDir::toNativeSeparators(entry->m_path);
		else if (hasExpandEntry() && index.row() == m_entries.size())
			result = "<               >";
		break;

	case Qt::ForegroundRole:
		result = !entry || entry->m_isValid
			? QColorConstants::Black
			: QColorConstants::Red;
		break;

	case Qt::EditRole:
		if (entry)
			result = QDir::toNativeSeparators(entry->m_path);
		break;
	}
	return result;
}


//-------------------------------------------------
//  PathListModel::setData
//-------------------------------------------------

bool PathsDialog::PathListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
	bool success = false;

	switch (role)
	{
	case Qt::EditRole:
		// convert the variant data to the format we want it in
		QString pathString = QDir::fromNativeSeparators(value.toString());

		// and set it
		setPath(index.row(), std::move(pathString));

		// success!
		success = true;
		break;
	}
	return success;
}


//-------------------------------------------------
//  PathListModel::flags
//-------------------------------------------------

Qt::ItemFlags PathsDialog::PathListModel::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return Qt::ItemIsEnabled;

	return QAbstractListModel::flags(index) | Qt::ItemIsEditable;
}


//-------------------------------------------------
//  PathListModel::validateAndCanonicalize
//-------------------------------------------------

bool PathsDialog::PathListModel::validateAndCanonicalize(QString &path) const
{
	// apply substitutions (e.g. - $(MAMEPATH) with actual MAME path)
	QString pathAfterSubstitutions = m_applySubstitutions
		? m_applySubstitutionsFunc(path)
		: path;

	// check the file
	bool isValid = false;
	QFileInfo fi(pathAfterSubstitutions);
	if (fi.exists())
	{
		switch (m_pathCategory)
		{
		case Preferences::PathCategory::SingleFile:
			isValid = fi.isFile();
			break;

		case Preferences::PathCategory::SingleDirectory:
		case Preferences::PathCategory::MultipleDirectories:
			isValid = fi.isDir();
			break;

		case Preferences::PathCategory::MultipleDirectoriesOrArchives:
			isValid = fi.isDir()
				|| (fi.isFile() && AssetFinder::isValidArchive(pathAfterSubstitutions));
			break;

		default:
			throw false;
		}

		// ensure a trailing slash if we're a directory
		if (fi.isDir() && !path.endsWith('/'))
			path += '/';
	}
	return isValid;
}


//-------------------------------------------------
//  PathListModel::hasExpandEntry
//-------------------------------------------------

bool PathsDialog::PathListModel::hasExpandEntry() const
{
	return m_entries.size() == 0
		|| (isExpandable() && !m_entries[m_entries.size() - 1].m_path.isEmpty());
}


//-------------------------------------------------
//  PathListModel::Entry ctor
//-------------------------------------------------

PathsDialog::PathListModel::Entry::Entry(QString &&path, bool isValid)
	: m_path(std::move(path))
	, m_isValid(isValid)
{
}
