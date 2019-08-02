/***************************************************************************

    dialogs/paths.cpp

    Paths dialog

***************************************************************************/

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/filename.h>
#include <wx/combobox.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/listctrl.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/dir.h>
#include <wx/dirdlg.h>

#include "dialogs/paths.h"
#include "prefs.h"
#include "utility.h"
#include "virtuallistview.h"
#include "validity.h"


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

namespace
{
	class PathsDialog : public wxDialog
	{
	public:
		PathsDialog(wxWindow &parent, Preferences &prefs);

		void Persist();
		static void ValidityChecks();

	private:
		enum
		{
			// user IDs
			ID_NEW_DIR_ENTRY = wxID_HIGHEST + 1,
			ID_LAST
		};

		static const size_t PATH_COUNT = (size_t)Preferences::path_type::count;
		static const std::array<wxString, PATH_COUNT> s_combo_box_strings;

		Preferences &						m_prefs;
		std::array<wxString, PATH_COUNT>	m_path_lists;
		std::vector<wxString>				m_current_path_list;
		std::vector<bool>					m_current_path_valid_list;
		wxComboBox *						m_combo_box;
		VirtualListView *					m_list_view;
		wxListItemAttr						m_list_item_attr;

		template<typename TControl, typename... TArgs> TControl &AddControl(wxBoxSizer *sizer, int proportion, int flags, TArgs&&... args);
		static std::array<wxString, PATH_COUNT> BuildComboBoxStrings();

		void UpdateCurrentPathList();
		void RefreshListView();
		void CurrentPathListChanged();
		bool IsMultiPath() const;
		static bool IsMultiPath(Preferences::path_type path_type);
		bool IsSelectingPath() const;
		Preferences::path_type GetCurrentPath() const;
		bool BrowseForPath();
		bool BrowseForPath(long item);
		bool BrowseForPath(size_t item);
		void OnListBeginLabelEdit(long item);
		void OnListEndLabelEdit(long item);
		void OnInsert();
		void OnDelete();
		void SetPathValue(size_t item, wxString &&value);
		wxString GetListItemText(size_t item) const;
		wxListItemAttr *GetListItemAttr(size_t item);
	};
};


//**************************************************************************
//  IMPLEMENTATION
//**************************************************************************

const std::array<wxString, PathsDialog::PATH_COUNT> PathsDialog::s_combo_box_strings = BuildComboBoxStrings();


//-------------------------------------------------
//  ctor
//-------------------------------------------------

PathsDialog::PathsDialog(wxWindow &parent, Preferences &prefs)
	: wxDialog(&parent, wxID_ANY, "Paths", wxDefaultPosition, wxDefaultSize, wxCAPTION | wxSYSTEM_MENU | wxCLOSE_BOX | wxRESIZE_BORDER)
	, m_prefs(prefs)
	, m_combo_box(nullptr)
	, m_list_view(nullptr)
{
	int id = ID_LAST;

	// path data
	for (size_t i = 0; i < PATH_COUNT; i++)
		m_path_lists[i] = m_prefs.GetPath(static_cast<Preferences::path_type>(i));

	// Left column
	wxBoxSizer *vbox_left = new wxBoxSizer(wxVERTICAL);
	AddControl<wxStaticText>(vbox_left, 0, wxALL, id++, "Show Paths For:");
	m_combo_box = &AddControl<wxComboBox>(
		vbox_left,
		0,
		wxALL,
		id++,
		wxEmptyString,
		wxDefaultPosition,
		wxDefaultSize,
		s_combo_box_strings.size(),
		s_combo_box_strings.data(),
		wxCB_READONLY);
	AddControl<wxStaticText>(vbox_left, 0, wxALL, id++, "Directories:");
	m_list_view = &AddControl<VirtualListView>(vbox_left, 1, wxALL | wxEXPAND, id++, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_NO_HEADER
		| wxLC_EDIT_LABELS | wxLC_VIRTUAL);
	m_list_view->SetOnGetItemText([this](long item, long)	{ return GetListItemText(static_cast<size_t>(item)); });
	m_list_view->SetOnGetItemAttr([this](long item)			{ return GetListItemAttr(static_cast<size_t>(item)); });

	// Right column
	wxBoxSizer *vbox_right = new wxBoxSizer(wxVERTICAL);
	wxButton &ok_button		= AddControl<wxButton>(vbox_right, 0, wxALL, wxID_OK,		"OK");
	wxButton &cancel_button = AddControl<wxButton>(vbox_right, 0, wxALL, wxID_CANCEL,	"Cancel");
	wxButton &browse_button = AddControl<wxButton>(vbox_right, 0, wxALL, id++,			"Browse");
	wxButton &insert_button = AddControl<wxButton>(vbox_right, 0, wxALL, id++,			"Insert");
	wxButton &delete_button	= AddControl<wxButton>(vbox_right, 0, wxALL, id++,			"Delete");

	// Combo box
	m_combo_box->Select(0);

	// List view
	m_list_view->ClearAll();
	m_list_view->AppendColumn(wxEmptyString, wxLIST_FORMAT_LEFT, m_list_view->GetSize().GetWidth());
	UpdateCurrentPathList();	

	// Overall layout
	wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
	hbox->Add(vbox_left, 1, wxEXPAND);
	hbox->Add(vbox_right, 0, wxALIGN_TOP | wxTOP | wxBOTTOM, 10);
	SetSizerAndFit(hbox);

	// bind events
	Bind(wxEVT_COMBOBOX,				[this](auto &)		{ UpdateCurrentPathList(); });
	Bind(wxEVT_BUTTON,					[this](auto &)		{ BrowseForPath();									}, browse_button.GetId());
	Bind(wxEVT_BUTTON,					[this](auto &)		{ OnInsert();										}, insert_button.GetId());
	Bind(wxEVT_BUTTON,					[this](auto &)		{ OnDelete();										}, delete_button.GetId());
	Bind(wxEVT_UPDATE_UI,				[this](auto &event) { event.Enable(IsMultiPath());						}, insert_button.GetId());
	Bind(wxEVT_UPDATE_UI,				[this](auto &event) { event.Enable(IsMultiPath() && IsSelectingPath());	}, delete_button.GetId());
	Bind(wxEVT_LIST_ITEM_ACTIVATED,		[this](auto &event) { BrowseForPath(event.GetIndex());					});
	Bind(wxEVT_LIST_BEGIN_LABEL_EDIT,	[this](auto &event) { OnListBeginLabelEdit(event.GetIndex());			});
	Bind(wxEVT_LIST_END_LABEL_EDIT,		[this](auto &event) { OnListEndLabelEdit(event.GetIndex());				});

	// appease compiler
	(void)ok_button;
	(void)cancel_button;
}


//-------------------------------------------------
//  Persist
//-------------------------------------------------

void PathsDialog::Persist()
{
	for (size_t i = 0; i < PATH_COUNT; i++)
		m_prefs.SetPath((Preferences::path_type) i, std::move(m_path_lists[i]));
}


//-------------------------------------------------
//  BrowseForPath
//-------------------------------------------------

bool PathsDialog::BrowseForPath()
{
	long item = m_list_view->GetFocusedItem();
	return BrowseForPath(static_cast<size_t>(item));
}


bool PathsDialog::BrowseForPath(long item)
{
	return BrowseForPath(static_cast<size_t>(item));
}


bool PathsDialog::BrowseForPath(size_t item)
{
	// should really be a sanity check
	if (item > m_current_path_list.size())
		return false;

	// show the file dialog
	wxString path = show_specify_single_path_dialog(
		*this,
		GetCurrentPath(),
		item < m_current_path_list.size() ? m_current_path_list[item] : "");
	if (path.IsEmpty())
		return false;

	// specify it
	SetPathValue(item, std::move(path));
	return true;
}


//-------------------------------------------------
//  OnListBeginLabelEdit
//-------------------------------------------------

void PathsDialog::OnListBeginLabelEdit(long item)
{
	// is this the "extension" item?  if so we need to empty out the text
	if (static_cast<size_t>(item) == m_current_path_list.size())
	{
		wxTextCtrl *edit_control = m_list_view->GetEditControl();
		edit_control->ChangeValue("");
	}
}


//-------------------------------------------------
//  OnListEndLabelEdit
//-------------------------------------------------

void PathsDialog::OnListEndLabelEdit(long item)
{
	// get the value entered in
	wxTextCtrl *edit_control = m_list_view->GetEditControl();
	const wxString value = edit_control->GetValue();

	// is this value not empty?  if so, we need to canonicalize
	wxString canonicalized_value;
	if (!value.IsEmpty())
	{
		// canonicalize it (for some reason, on Windows this seems to only canonicalize the
		// leaf filename, so more to do)
		wxFileName file_name(value);
		canonicalized_value = file_name.GetLongPath();
	}

	// and specify it
	SetPathValue(static_cast<size_t>(item), std::move(canonicalized_value));
}


//-------------------------------------------------
//  SetPathValue
//-------------------------------------------------

void PathsDialog::SetPathValue(size_t item, wxString &&value)
{
	// sanity checks, and calling with 'item == m_current_path_list.size()' appends
	assert(item >= 0);
	assert(item <= m_current_path_list.size());

	if (!value.IsEmpty())
	{
		// we have a value - modify it or append it if we're extending the list
		if (item == m_current_path_list.size())
			m_current_path_list.push_back(std::move(value));
		else
			m_current_path_list[item] = std::move(value);
	}
	else
	{
		// we do not have a value - delete it
		if (item < m_current_path_list.size())
			m_current_path_list.erase(m_current_path_list.begin() + item);
	}
	CurrentPathListChanged();
}


//-------------------------------------------------
//  OnInsert
//-------------------------------------------------

void PathsDialog::OnInsert()
{
	long focused_item = m_list_view->GetFocusedItem();
	long item_to_edit = focused_item >= 0
		? focused_item
		: m_list_view->GetItemCount() - 1;

	m_current_path_list.insert(m_current_path_list.begin() + item_to_edit, wxEmptyString);
	m_list_view->EditLabel(item_to_edit);
	m_list_view->Refresh();
}


//-------------------------------------------------
//  OnDelete
//-------------------------------------------------

void PathsDialog::OnDelete()
{
	size_t item = static_cast<size_t>(m_list_view->GetFocusedItem());
	if (item >= m_current_path_list.size())
		return;

	// delete it!
	m_current_path_list.erase(m_current_path_list.begin() + item);
	CurrentPathListChanged();
}


//-------------------------------------------------
//  CurrentPathListChanged
//-------------------------------------------------

void PathsDialog::CurrentPathListChanged()
{
	// reflect changes on the m_current_path_list back into m_path_lists 
	wxString path_list = util::string_join(wxString(";"), m_current_path_list);
	m_path_lists[m_combo_box->GetSelection()] = std::move(path_list);
	
	// because the list view may have changed, we need to refresh it
	RefreshListView();
}


//-------------------------------------------------
//  GetListItemText
//-------------------------------------------------

wxString PathsDialog::GetListItemText(size_t item) const
{
	return item < m_current_path_list.size()
		? m_current_path_list[item]
		: "<               >";
}


//-------------------------------------------------
//  GetListItemAttr
//-------------------------------------------------

wxListItemAttr *PathsDialog::GetListItemAttr(size_t item)
{
	bool is_valid = item >= m_current_path_valid_list.size() || m_current_path_valid_list[item];
	m_list_item_attr.SetTextColour(is_valid ? *wxBLACK : *wxRED);
	return &m_list_item_attr;
}


//-------------------------------------------------
//  UpdateCurrentPathList
//-------------------------------------------------

void PathsDialog::UpdateCurrentPathList()
{
	int type = m_combo_box->GetSelection();

	if (m_path_lists[type].IsEmpty())
		m_current_path_list.clear();
	else
		m_current_path_list = util::string_split(m_path_lists[type], [](wchar_t ch) { return ch == ';'; });

	RefreshListView();
}


//-------------------------------------------------
//  RefreshListView
//-------------------------------------------------

void PathsDialog::RefreshListView()
{
	// basic info about the type of path we are
	const bool is_emu_executable = GetCurrentPath() == Preferences::path_type::emu_exectuable;
	const bool expect_dir = !is_emu_executable;

	// recalculate m_current_path_valid_list
	m_current_path_valid_list.resize(m_current_path_list.size());
	for (size_t i = 0; i < m_current_path_list.size(); i++)
	{
		// apply substitutions (e.g. - $(MAMEPATH) with actual MAME path), unless this is the executable of course
		wxString current_path_buffer;
		const wxString &current_path = !is_emu_executable
			? (current_path_buffer = m_prefs.ApplySubstitutions(m_current_path_list[i]), current_path_buffer)
			: m_current_path_list[i];

		// and perform the check
		m_current_path_valid_list[i] = expect_dir
			? wxDir::Exists(current_path)
			: wxFile::Exists(current_path);
	}

	// update the item count and refresh all items
	long item_count = m_current_path_list.size() + (IsMultiPath() ? 1 : 0);
	m_list_view->SetItemCount(item_count);
	m_list_view->RefreshItems(0, item_count - 1);
}


//-------------------------------------------------
//  AddControl
//-------------------------------------------------

template<typename TControl, typename... TArgs>
TControl &PathsDialog::AddControl(wxBoxSizer *sizer, int proportion, int flags, TArgs&&... args)
{
	TControl *control = new TControl(this, std::forward<TArgs>(args)...);
	sizer->Add(control, proportion, flags, 4);
	return *control;
}


//-------------------------------------------------
//  BuildComboBoxStrings
//-------------------------------------------------

std::array<wxString, PathsDialog::PATH_COUNT> PathsDialog::BuildComboBoxStrings()
{
	std::array<wxString, PATH_COUNT> result;
	result[(size_t)Preferences::path_type::emu_exectuable]	= "MAME Executable";
	result[(size_t)Preferences::path_type::roms]			= "ROMs";
	result[(size_t)Preferences::path_type::samples]			= "Samples";
	result[(size_t)Preferences::path_type::config]			= "Config Files";
	result[(size_t)Preferences::path_type::nvram]			= "NVRAM Files";
	result[(size_t)Preferences::path_type::hash]			= "Hash Files";
	result[(size_t)Preferences::path_type::artwork]			= "Artwork Files";
	result[(size_t)Preferences::path_type::plugins]			= "Plugins";

	// check to make sure that all values are specified
	for (const wxString &str : result)
		assert(!str.empty());

	return result;
}


//-------------------------------------------------
//  IsMultiPath
//-------------------------------------------------

bool PathsDialog::IsMultiPath() const
{
	return IsMultiPath(GetCurrentPath());
}


bool PathsDialog::IsMultiPath(Preferences::path_type path_type)
{
	bool result;
	switch (path_type)
	{
	case Preferences::path_type::emu_exectuable:
	case Preferences::path_type::config:
	case Preferences::path_type::nvram:
		result = false;
		break;

	case Preferences::path_type::roms:
	case Preferences::path_type::samples:
	case Preferences::path_type::hash:
	case Preferences::path_type::artwork:
	case Preferences::path_type::plugins:
		result = true;
		break;

	default:
		throw false;
	}
	return result;
}


//-------------------------------------------------
//  GetCurrentPath
//-------------------------------------------------

Preferences::path_type PathsDialog::GetCurrentPath() const
{
	int selection = m_combo_box->GetSelection();
	return static_cast<Preferences::path_type>(selection);
}


//-------------------------------------------------
//  IsSelectingPath
//-------------------------------------------------

bool PathsDialog::IsSelectingPath() const
{
	size_t item = static_cast<size_t>(m_list_view->GetFocusedItem());
	return item < m_current_path_list.size();
}


//-------------------------------------------------
//  ValidityChecks
//-------------------------------------------------

void PathsDialog::ValidityChecks()
{
	BuildComboBoxStrings();

	for (int i = 0; i < (int)Preferences::path_type::count; i++)
		IsMultiPath(static_cast<Preferences::path_type>(i));
}


//-------------------------------------------------
//  show_specify_single_path_dialog
//-------------------------------------------------

wxString show_specify_single_path_dialog(wxWindow &parent, Preferences::path_type type, const wxString &default_path)
{
	wxString result;
	if (type == Preferences::path_type::emu_exectuable)
	{
		wxFileDialog dialog(&parent, "Specify MAME Path", "", default_path, "EXE files (*.exe)|*.exe", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dialog.ShowModal() == wxID_OK)
			result = dialog.GetPath();
	}
	else
	{
		wxDirDialog dialog(&parent, "Specify Path", default_path);
		if (dialog.ShowModal() == wxID_OK)
			result = dialog.GetPath();
	}
	return result;
}


//-------------------------------------------------
//  show_paths_dialog
//-------------------------------------------------

bool show_paths_dialog(wxWindow &parent, Preferences &prefs)
{
	PathsDialog dialog(parent, prefs);
	bool result = dialog.ShowModal() == wxID_OK;
	if (result)
	{
		dialog.Persist();
	}
	return result;
}


//-------------------------------------------------
//  validity_checks
//-------------------------------------------------

static validity_check validity_checks[] =
{
	PathsDialog::ValidityChecks
};