/***************************************************************************

    dialogs/confdev.cpp

    Configurable Devices dialog

***************************************************************************/

#include <QAbstractItemDelegate>
#include <QAction>
#include <QBitmap>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStringListModel>
#include <QStyledItemDelegate>

#include "ui_confdev.h"
#include "dialogs/choosesw.h"
#include "dialogs/confdev.h"


//**************************************************************************
//  TYPES
//**************************************************************************

class ConfigurableDevicesDialog::ConfigurableDevicesItemDelegate : public QStyledItemDelegate
{
public:
    ConfigurableDevicesItemDelegate(ConfigurableDevicesDialog &parent)
        : QStyledItemDelegate(&parent)
    {
    }

    virtual QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        // create a button
        QPushButton &button = *new QPushButton("...");
        button.setMaximumSize(30, 17);  // there is probably a better way to do this
        connect(&button, &QPushButton::clicked, this, [this, &button, index]() { buttonClicked(button, index); });

        // and wrap it up into a widget
        QWidget &widget = *new QWidget(parent);
        QHBoxLayout &layout = *new QHBoxLayout(&widget);
        layout.setSpacing(0);
        layout.setMargin(0);
        layout.setAlignment(Qt::AlignRight);
        layout.addWidget(&button);
        layout.setSizeConstraint(QLayout::SetMinimumSize);
        return &widget;
    }

private:
    void buttonClicked(QPushButton &button, const QModelIndex &index) const
    {
        ConfigurableDevicesDialog &dialog = *dynamic_cast<ConfigurableDevicesDialog *>(parent());
        dialog.deviceMenu(button, index);
    }
};


//**************************************************************************
//  IMPLEMENTATION
//**************************************************************************

//-------------------------------------------------
//  ctor
//-------------------------------------------------

ConfigurableDevicesDialog::ConfigurableDevicesDialog(QWidget &parent, IConfigurableDevicesDialogHost &host, bool cancellable)
    : QDialog(&parent)
    , m_host(host)
    , m_canChangeSlotOptions(false)
{
    // set up UI
    m_ui = std::make_unique<Ui::ConfigurableDevicesDialog>();
    m_ui->setupUi(this);

    // we may or may not be cancellable
    QDialogButtonBox::StandardButtons standardButtons = cancellable
        ? QDialogButtonBox::StandardButton::Ok | QDialogButtonBox::StandardButton::Cancel
        : QDialogButtonBox::StandardButton::Ok;
    m_ui->buttonBox->setStandardButtons(standardButtons);

    // set up warning icons
    setupWarningIcons({ *m_ui->warningHashPathIcon, *m_ui->warningDeviceChangesRequireResetIcon });

    // warnings
    m_ui->warningHashPathIcon->setVisible(!host.startedWithHashPaths());
    m_ui->warningHashPathLabel->setVisible(!host.startedWithHashPaths());

    // find a software list collection, if possible
    software_list_collection software_col;
    software_col.load(m_host.getPreferences(), m_host.getMachine());

    // set up tree view
    ConfigurableDevicesModel &model = *new ConfigurableDevicesModel(this, m_host.getMachine(), std::move(software_col));
    m_ui->treeView->setModel(&model);
    m_ui->treeView->setItemDelegateForColumn(1, new ConfigurableDevicesItemDelegate(*this));

    // set up the model reset event
    connect(&model, &QAbstractItemModel::modelReset, [this]() { onModelReset(); });

    // host interactions
    m_slotsEventSubscription = m_host.getSlots().subscribe_and_call([this] { updateSlots(); });
    m_imagesEventSubscription = m_host.getImages().subscribe_and_call([this] { updateImages(); });
}


//-------------------------------------------------
//  dtor
//-------------------------------------------------

ConfigurableDevicesDialog::~ConfigurableDevicesDialog()
{
}


//-------------------------------------------------
//  model
//-------------------------------------------------

ConfigurableDevicesModel &ConfigurableDevicesDialog::model()
{
    return *dynamic_cast<ConfigurableDevicesModel *>(m_ui->treeView->model());
}

const ConfigurableDevicesModel &ConfigurableDevicesDialog::model() const
{
    return *dynamic_cast<ConfigurableDevicesModel *>(m_ui->treeView->model());
}


//-------------------------------------------------
//  onModelReset
//-------------------------------------------------

void ConfigurableDevicesDialog::onModelReset()
{
    // expand all tree items (not really the correct thing to do, but good enough for now)
    m_ui->treeView->expandRecursively(QModelIndex());

    // determine if we have any pending changes
    bool hasPendingDeviceChanges = model().getChanges().size() > 0;

    // and update the UI accordingly
    m_ui->warningDeviceChangesRequireResetIcon->setVisible(hasPendingDeviceChanges);
    m_ui->warningDeviceChangesRequireResetLabel->setVisible(hasPendingDeviceChanges);
    m_ui->applyChangesButton->setEnabled(hasPendingDeviceChanges);
}


//-------------------------------------------------
//  on_applyChangesButton_clicked
//-------------------------------------------------

void ConfigurableDevicesDialog::on_applyChangesButton_clicked()
{
    m_host.changeSlots(model().getChanges());
}


//-------------------------------------------------
//  setupWarningIcons
//-------------------------------------------------

void ConfigurableDevicesDialog::setupWarningIcons(std::initializer_list<std::reference_wrapper<QLabel>> iconLabels)
{
    QSize size(24, 24);
    QPixmap warningIconPixmap = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(size);

    for (QLabel &iconLabel : iconLabels)
    {
        iconLabel.setPixmap(warningIconPixmap);
        iconLabel.setMask(warningIconPixmap.mask());
    }
}


//-------------------------------------------------
//  accept
//-------------------------------------------------

void ConfigurableDevicesDialog::accept()
{
    bool aborting = false;

    // do we have any pending changes?
    std::map<QString, QString> pendingDeviceChanges = model().getChanges();
    if (pendingDeviceChanges.size() > 0)
    {
        // we do - prompt the user
        QMessageBox msgBox(this);
        msgBox.setText("There are pending device configuration changes.  Do you want to apply them?  This will reset the emulation.");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        switch (msgBox.exec())
        {
        case QMessageBox::StandardButton::Yes:
            // apply changes and accept
            m_host.changeSlots(std::move(pendingDeviceChanges));
            break;

        case QMessageBox::StandardButton::No:
            // don't apply changes, just proceed accepting
            break;

        case QMessageBox::StandardButton::Cancel:
            // abort; don't accept
            aborting = true;
            break;

        default:
            throw false;
        }
    }

    // continue up the chain, unless we're cancelling
    if (!aborting)
        QDialog::accept();
}


//-------------------------------------------------
//  updateSlots
//-------------------------------------------------

void ConfigurableDevicesDialog::updateSlots()
{
    // get the slots status
    const std::vector<status::slot> &devslots = m_host.getSlots().get();

    // if we have act least one slot, we have affirmed that we are on a
    // version of MAME that can handle slot device changes
    if (!devslots.empty())
        m_canChangeSlotOptions = true;

    // update the model
    model().setSlots(devslots);
}


//-------------------------------------------------
//  updateImages
//-------------------------------------------------

void ConfigurableDevicesDialog::updateImages()
{
    // get the image status
    const std::vector<status::image> &images = m_host.getImages().get();

    // update the model
    model().setImages(images);

    // are there any mandatory images missing?
    auto iter = std::find_if(
        images.cbegin(),
        images.cend(),
        [](const status::image &image) { return image.m_must_be_loaded && image.m_file_name.isEmpty(); });
    bool anyMandatoryImagesMissing = iter != images.cend();

    // set the ok button enabled property
    m_ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!anyMandatoryImagesMissing);
}


//-------------------------------------------------
//  deviceMenu
//-------------------------------------------------

void ConfigurableDevicesDialog::deviceMenu(QPushButton &button, const QModelIndex &index)
{
    // get info for the device
    ConfigurableDevicesModel::DeviceInfo devInfo = model().getDeviceInfo(index);

    // we're going to be building a popup menu
    QMenu popupMenu;

    // is this device slotted?
    if (devInfo.slot().has_value())
    {
        // if so, append items for slot devices
        buildDeviceMenuSlotItems(popupMenu, devInfo.tag(), devInfo.slot().value(), devInfo.slotOption());
    }

    // is this device "imaged"?
    if (devInfo.image().has_value())
    {
        // need a separator if we have both
        if (devInfo.slot().has_value())
            popupMenu.addSeparator();

        // and append items for image devices
        buildImageMenuSlotItems(popupMenu, devInfo.tag(), devInfo.image().value());
    }

    // and execute the popup menu
    QPoint popupPos = globalPositionBelowWidget(button);
    popupMenu.exec(popupPos);
}


//-------------------------------------------------
//  buildDeviceMenuSlotItems
//-------------------------------------------------

void ConfigurableDevicesDialog::buildDeviceMenuSlotItems(QMenu &popupMenu, const QString &tag, const info::slot &slot, const QString &currentSlotOption)
{
    struct SlotOptionInfo
    {
        SlotOptionInfo(std::optional<info::slot_option> slotOption = std::nullopt)
            : m_slotOption(slotOption)
        {
        }

        std::optional<info::slot_option>    m_slotOption;
        QString                             m_text;
    };

    // if so, prepare a list of options (and the initial item)
    std::vector<SlotOptionInfo> slotOptions;
    slotOptions.emplace_back();

    // and the other options
    for (info::slot_option opt : slot.options())
        slotOptions.emplace_back(opt);

    // and get the human-readable text
    for (SlotOptionInfo &soi : slotOptions)
        soi.m_text = ConfigurableDevicesModel::getSlotOptionText(slot, soi.m_slotOption).value_or(TEXT_NONE);

    // and sort them
    std::sort(
        slotOptions.begin(),
        slotOptions.end(),
        [this](const SlotOptionInfo &a, const SlotOptionInfo &b)
        {
            int rc = QString::localeAwareCompare(a.m_text, b.m_text);
            return rc != 0
                ? rc < 0
                : QString::compare(a.m_text, b.m_text) < 0;
        });

    // now put the devices on the popup menu
    for (const SlotOptionInfo &soi : slotOptions)
    {
        // identify the name
        const QString &slotOptionName = soi.m_slotOption.has_value()
            ? soi.m_slotOption->name()
            : util::g_empty_string;

        // create the action
        QAction &action = *popupMenu.addAction(soi.m_text, [this, &tag, slotOptionName]
        {
            model().changeSlotOption(tag, slotOptionName);
        });
        action.setCheckable(true);

        // we only enable the action is we can handle slot device changes
        action.setEnabled(m_canChangeSlotOptions);

        // and check it accordingly
        bool isChecked = currentSlotOption == slotOptionName;
        action.setChecked(isChecked);
    }
}


//-------------------------------------------------
//  buildImageMenuSlotItems
//-------------------------------------------------

void ConfigurableDevicesDialog::buildImageMenuSlotItems(QMenu &popupMenu, const QString &tag, const DeviceImage &image)
{
    // create image
    if (image.m_isCreatable)
        popupMenu.addAction("Create Image...", [this, &tag]() { createImage(tag); });

    // load image
    popupMenu.addAction("Load Image...", [this, &tag]() { loadImage(tag); });

    // load software list part
    if (!model().softwareListCollection().software_lists().empty())
    {
        std::optional<info::device> device = m_host.getMachine().find_device(tag);
        const QString *devInterface = device.has_value()
            ? &device->devinterface()
            : nullptr;
        if (devInterface && !devInterface->isEmpty())
        {
            popupMenu.addAction("Load Software List Part...", [this, &tag, devInterface]()
            {
                loadSoftwareListPart(model().softwareListCollection(), tag, *devInterface);
            });
        }
    }

    // unload
    QAction &unloadAction = *popupMenu.addAction("Unload", [this, &tag]() { unloadImage(tag); });
    unloadAction.setEnabled(!image.m_fileName.isEmpty());

    // recent files
    const std::vector<QString> &recent_files = m_host.getRecentFiles(tag);
    if (!recent_files.empty())
    {
        QString pretty_buffer;
        popupMenu.addSeparator();
        for (const QString &recentFile : recent_files)
        {
            QString prettyRecentFile = model().prettifyImageFileName(tag, recentFile, false);
            popupMenu.addAction(prettyRecentFile, [this, &tag, &recentFile]() { m_host.loadImage(tag, QString(recentFile)); });
        }
    }
}


//-------------------------------------------------
//  createImage
//-------------------------------------------------

bool ConfigurableDevicesDialog::createImage(const QString &tag)
{
    // show the fialog
    QFileDialog dialog(
        this,
        "Create Image",
        m_host.getWorkingDirectory(),
        getWildcardString(tag, false));
    dialog.setFileMode(QFileDialog::FileMode::AnyFile);
    dialog.exec();
    if (dialog.result() != QDialog::DialogCode::Accepted)
        return false;

    // get the result from the dialog
    QString path = QDir::toNativeSeparators(dialog.selectedFiles().first());

    // update our host's working directory
    updateWorkingDirectory(path);

    // and load the image
    m_host.createImage(tag, std::move(path));
    return true;
}


//-------------------------------------------------
//  loadImage
//-------------------------------------------------

bool ConfigurableDevicesDialog::loadImage(const QString &tag)
{
    // show the fialog
    QFileDialog dialog(
        this,
        "Load Image",
        m_host.getWorkingDirectory(),
        getWildcardString(tag, true));
    dialog.setFileMode(QFileDialog::FileMode::ExistingFile);
    dialog.exec();
    if (dialog.result() != QDialog::DialogCode::Accepted)
        return false;

    // get the result from the dialog
    QString path = QDir::toNativeSeparators(dialog.selectedFiles().first());

    // update our host's working directory
    updateWorkingDirectory(path);

    // and load the image
    m_host.loadImage(tag, std::move(path));
    return true;
}


//-------------------------------------------------
//  loadSoftwareListPart
//-------------------------------------------------

bool ConfigurableDevicesDialog::loadSoftwareListPart(const software_list_collection &software_col, const QString &tag, const QString &dev_interface)
{
    ChooseSoftlistPartDialog dialog(this, m_host.getPreferences(), software_col, dev_interface);
    dialog.exec();
    if (dialog.result() != QDialog::DialogCode::Accepted)
        return false;

    m_host.loadImage(tag, dialog.selection());
    return true;
}


//-------------------------------------------------
//  unloadImage
//-------------------------------------------------

void ConfigurableDevicesDialog::unloadImage(const QString &tag)
{
    m_host.unloadImage(tag);
}


//-------------------------------------------------
//  getWildcardString
//-------------------------------------------------

QString ConfigurableDevicesDialog::getWildcardString(const QString &tag, bool support_zip) const
{
    // get the list of extensions
    std::vector<QString> extensions = m_host.getExtensions(tag);

    // append zip if appropriate
    if (support_zip)
        extensions.push_back("zip");

    // figure out the "general" wildcard part for all devices
    QString all_extensions = util::string_join(QString(";"), extensions, [](QString ext) { return QString("*.%1").arg(ext); });
    QString result = QString("Device files (%1)").arg(all_extensions);

    // now break out each extension
    for (const QString &ext : extensions)
    {
        result += QString(";;%1 files (*.%2s)").arg(
            ext.toUpper(),
            ext);
    }

    // and all files
    result += ";;All files (*.*)";
    return result;
}


//-------------------------------------------------
//  updateWorkingDirectory
//-------------------------------------------------

void ConfigurableDevicesDialog::updateWorkingDirectory(const QString &path)
{
    QString dir;
    wxFileName::SplitPath(path, &dir, nullptr, nullptr);
    m_host.setWorkingDirectory(std::move(dir));
}
